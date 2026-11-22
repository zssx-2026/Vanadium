//! Event renderer.
//!
//! Rules implemented here (see docs/DESIGN.md section 2):
//! * LINE      - permanent line, always printed.
//! * STATUS    - one replaceable line per key, "text." / "text.." / "text..." every 300 ms.
//! * PROGRESS  - "phase [####] 40% 12/30 1.2MB/s ETA:00:00:12", refreshed at most every 100 ms,
//!               one line per phase, at most 8 live lines, overflow folded.
//! * ERROR     - "[error] ..." on stderr, forces a non-zero exit code.
//! * PROMPT    - y/n on stdin, EOF or non-interactive input answers 0 (no).
//! * SUMMARY   - printed only with --json.
//! * STATUS_CLEAR / TITLE / WARN - cleared / console title / "[warn] ..." on stderr.
//!
//! No `sleep` ever runs on the kernel thread: throttling and animation are time
//! based, and the animation thread only repaints.

use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

use crate::ffi::{
    cstr, VdEvent, VD_EV_ERROR, VD_EV_LINE, VD_EV_PROGRESS, VD_EV_PROMPT, VD_EV_STATUS,
    VD_EV_STATUS_CLEAR, VD_EV_SUMMARY, VD_EV_TITLE, VD_EV_WARN,
};
use crate::term;

const STATUS_ANIM_MS: u128 = 300;
const PROGRESS_THROTTLE_MS: u128 = 100;
const PHASE_LINGER_MS: u128 = 500;
const MAX_LIVE_LINES: usize = 8;
const BAR_WIDTH: usize = 10;
/// A kernel supplied i4 below this value is treated as a flag word, not as bytes/s.
const RATE_FROM_I4_MIN: i64 = 1000;

pub static RENDER: OnceLock<Mutex<Renderer>> = OnceLock::new();

pub fn init(json: bool, cons: term::Console) {
    let _ = RENDER.set(Mutex::new(Renderer::new(json, cons)));
}

pub fn with<R>(f: impl FnOnce(&mut Renderer) -> R) -> R {
    let cell = RENDER.get().expect("renderer not initialised");
    let mut guard = cell.lock().unwrap_or_else(|poison| poison.into_inner());
    f(&mut guard)
}

struct Status {
    key: String,
    text: String,
    t0: Instant,
    frame: usize,
}

struct Phase {
    name: String,
    done: i64,
    total: i64,
    bytes: i64,
    rate: f64,
    t0: Instant,
    last_bytes: i64,
    last_bytes_t: Instant,
    last_draw: Instant,
    done_at: Option<Instant>,
    last_print: Instant,
    printed_done: bool,
}

impl Phase {
    fn new(name: String) -> Phase {
        let now = Instant::now();
        let past = |ms: u64| now.checked_sub(Duration::from_millis(ms)).unwrap_or(now);
        Phase {
            name,
            done: 0,
            total: 0,
            bytes: 0,
            rate: 0.0,
            t0: now,
            last_bytes: 0,
            last_bytes_t: now,
            last_draw: past(1_000),
            done_at: None,
            last_print: past(10_000),
            printed_done: false,
        }
    }
}

#[derive(Default)]
struct Live {
    height: usize,
    status: Option<Status>,
    phases: Vec<Phase>,
}

pub struct Renderer {
    json: bool,
    cons: term::Console,
    live: Live,
    dirty: bool,
    next_paint: Instant,
    had_error: bool,
    /// Width of the status line currently overwritten in place on a legacy
    /// console (0 = no such line on screen).
    inline: usize,
}

impl Renderer {
    pub fn new(json: bool, cons: term::Console) -> Renderer {
        Renderer {
            json,
            cons,
            live: Live::default(),
            dirty: false,
            next_paint: Instant::now(),
            had_error: false,
            inline: 0,
        }
    }

    pub fn had_error(&self) -> bool {
        self.had_error
    }

    /// Called from the kernel thread for every event.
    pub fn event(&mut self, ev: &VdEvent) -> i32 {
        match ev.kind {
            VD_EV_LINE => {
                let text = cstr(ev.a);
                self.permanent(&text);
                0
            }
            VD_EV_WARN => {
                let text = cstr(ev.a);
                self.warn(&text);
                0
            }
            VD_EV_ERROR => {
                let text = cstr(ev.a);
                self.error(&text);
                0
            }
            VD_EV_STATUS => {
                let key = cstr(ev.a);
                let text = cstr(ev.b);
                self.status(key, text);
                0
            }
            VD_EV_STATUS_CLEAR => {
                self.status_clear();
                0
            }
            VD_EV_PROGRESS => {
                self.progress(ev);
                0
            }
            VD_EV_PROMPT => self.prompt(ev),
            VD_EV_SUMMARY => {
                let text = cstr(ev.a);
                self.summary(&text);
                0
            }
            VD_EV_TITLE => {
                let text = cstr(ev.a);
                if !text.is_empty() {
                    term::set_title(&text);
                }
                0
            }
            _ => 0,
        }
    }

    /* ---- permanent output ---------------------------------------------- */

    // Permanent output leaves the live region erased; the animation thread
    // restores it on its next tick, so the kernel thread never blocks on a
    // redraw and no region is painted twice for one line.
    fn permanent(&mut self, text: &str) {
        self.erase_live();
        term::write_out(&format!("{}\n", text));
        self.dirty = true;
    }

    fn warn(&mut self, text: &str) {
        self.erase_live();
        term::write_err(&format!("[warn] {}\n", text));
        self.dirty = true;
    }

    fn error(&mut self, text: &str) {
        self.had_error = true;
        self.erase_live();
        term::write_err(&format!("[error] {}\n", text));
        self.dirty = true;
    }

    fn summary(&mut self, json_text: &str) {
        if !self.json || json_text.trim().is_empty() {
            return;
        }
        self.erase_live();
        term::write_out(&format!("{}\n", json_text));
    }

    /* ---- status -------------------------------------------------------- */

    fn status(&mut self, key: String, text: String) {
        if key.is_empty() && text.is_empty() {
            self.status_clear();
            return;
        }
        let changed = match &self.live.status {
            Some(s) => s.key != key || s.text != text,
            None => true,
        };
        if !changed {
            return;
        }
        self.live.status = Some(Status {
            key,
            text: text.clone(),
            t0: Instant::now(),
            frame: 0,
        });
        if self.cons.vt {
            self.dirty = true;
            self.paint();
        } else if self.cons.is_console {
            // Legacy console without VT support: no cursor addressing, but "\r"
            // still rewrites one line in place, so keep the status single-line.
            let width = text.chars().count() + 1;
            let mut buf = String::from("\r");
            buf.push_str(&text);
            for _ in width..self.inline {
                buf.push(' ');
            }
            self.inline = width;
            term::write_out(&buf);
        } else {
            term::write_out(&format!("{}\n", text));
        }
    }

    fn status_clear(&mut self) {
        if self.live.status.take().is_some() {
            self.clear_inline();
            self.dirty = true;
            self.paint();
        }
    }

    /// Wipe the in-place status line of a legacy console.
    fn clear_inline(&mut self) {
        if self.inline > 0 {
            let blank: String = std::iter::repeat(' ').take(self.inline).collect();
            term::write_out(&format!("\r{}\r", blank));
            self.inline = 0;
        }
    }

    /* ---- progress ------------------------------------------------------ */

    fn progress(&mut self, ev: &VdEvent) {
        let name = cstr(ev.a);
        let now = Instant::now();
        if !self.live.phases.iter().any(|p| p.name == name) {
            self.live.phases.push(Phase::new(name.clone()));
        }

        let mut flush = false;
        let mut plain: Option<String> = None;
        let vt = self.cons.vt;
        let utf8 = self.cons.utf8;

        if let Some(p) = self.live.phases.iter_mut().find(|p| p.name == name) {
            // Measured throughput from the byte counter wins; the kernel hint in i4
            // is only used when it cannot be a flag word.
            let dt = now.duration_since(p.last_bytes_t).as_secs_f64();
            if dt >= 0.05 {
                if ev.i3 > p.last_bytes {
                    p.rate = (ev.i3 - p.last_bytes) as f64 / dt;
                }
                p.last_bytes = ev.i3;
                p.last_bytes_t = now;
            }
            if p.rate <= 0.0 && ev.i4 >= RATE_FROM_I4_MIN {
                p.rate = ev.i4 as f64;
            }

            p.done = ev.i1;
            p.total = ev.i2;
            p.bytes = ev.i3;
            if p.total > 0 && p.done >= p.total {
                if p.done_at.is_none() {
                    p.done_at = Some(now);
                }
            } else {
                p.done_at = None;
                p.printed_done = false;
            }

            if vt {
                if now.duration_since(p.last_draw).as_millis() >= PROGRESS_THROTTLE_MS {
                    p.last_draw = now;
                    flush = true;
                }
            } else {
                let finished = p.total > 0 && p.done >= p.total && !p.printed_done;
                let due = now.duration_since(p.last_print).as_secs() >= 2;
                if finished || due {
                    p.last_print = now;
                    if finished {
                        p.printed_done = true;
                    }
                    plain = Some(render_progress_line(p, now, utf8));
                }
            }
        }

        if let Some(line) = plain {
            term::write_out(&format!("{}\n", line));
        }
        self.dirty = true;
        if flush {
            self.paint();
        }
    }

    /* ---- prompt -------------------------------------------------------- */

    fn prompt(&mut self, ev: &VdEvent) -> i32 {
        let question = cstr(ev.a);
        let hint = cstr(ev.b);
        self.erase_live();

        let line = if hint.trim().is_empty() {
            format!("{} [y/n]: ", question)
        } else {
            format!("{} [y/n] ({}): ", question, hint)
        };
        term::write_out(&line);

        let tty = term::stdin_is_tty();
        let answer = read_answer(tty);
        if !tty {
            // Piped input is not echoed by a terminal, so record the decision.
            term::write_out(if answer == 1 { "yes\n" } else { "no\n" });
        }
        self.repaint();
        answer
    }

    /* ---- animation ----------------------------------------------------- */

    /// Driven by the animation thread; never blocks the kernel.
    pub fn tick(&mut self) {
        if !self.cons.vt {
            return;
        }
        let now = Instant::now();

        let before = self.live.phases.len();
        self.live.phases.retain(|p| !expired(p, now));
        if self.live.phases.len() != before {
            self.dirty = true;
        }

        if let Some(s) = &mut self.live.status {
            let frame = ((now.duration_since(s.t0).as_millis() / STATUS_ANIM_MS) % 3) as usize;
            if frame != s.frame {
                s.frame = frame;
                self.dirty = true;
            }
        }

        if self.dirty && now >= self.next_paint {
            self.paint();
        }
    }

    /// Drop the live region; called before the shell returns.
    pub fn finish(&mut self) {
        self.erase_live();
        self.clear_inline();
        self.live.status = None;
        self.live.phases.clear();
    }

    /* ---- drawing ------------------------------------------------------- */

    fn repaint(&mut self) {
        if self.cons.vt {
            self.paint();
        }
    }

    fn build(&self, now: Instant) -> Vec<String> {
        let mut lines: Vec<String> = Vec::new();
        if let Some(s) = &self.live.status {
            lines.push(render_status_line(s));
        }
        let active: Vec<&Phase> = self.live.phases.iter().filter(|p| !expired(p, now)).collect();
        let budget = MAX_LIVE_LINES.saturating_sub(lines.len());
        if active.len() <= budget {
            for p in &active {
                lines.push(render_progress_line(p, now, self.cons.utf8));
            }
        } else {
            let show = budget.saturating_sub(1);
            for p in active.iter().take(show) {
                lines.push(render_progress_line(p, now, self.cons.utf8));
            }
            let folded = active.len() - show;
            lines.push(format!(
                "... +{} more phase{}",
                folded,
                if folded == 1 { "" } else { "s" }
            ));
        }
        lines
    }

    /// Redraw the live region. Invariant: on entry the cursor sits at column 0 of
    /// the last line of the region (or of the current line when height == 0).
    fn paint(&mut self) {
        if !self.cons.vt {
            return;
        }
        let now = Instant::now();
        let lines = self.build(now);
        let mut buf = String::new();

        if self.live.height > 0 {
            if self.live.height > 1 {
                buf.push_str(&format!("\x1b[{}A", self.live.height - 1));
            }
            buf.push('\r');
        }

        let n = lines.len();
        for (i, line) in lines.iter().enumerate() {
            buf.push_str("\x1b[K");
            buf.push_str(line);
            if i + 1 < n {
                buf.push_str("\r\n");
            }
        }
        if self.live.height > n {
            for _ in n..self.live.height {
                buf.push_str("\r\n\x1b[K");
            }
            buf.push_str(&format!("\x1b[{}A\r", self.live.height - n));
        }

        self.live.height = n;
        self.dirty = false;
        self.next_paint = now + Duration::from_millis(PROGRESS_THROTTLE_MS as u64);
        term::write_out(&buf);
    }

    /// Blank the live region and park the cursor at its top-left corner.
    fn erase_live(&mut self) {
        if !self.cons.vt || self.live.height == 0 {
            self.live.height = 0;
            return;
        }
        let h = self.live.height;
        let mut buf = String::new();
        if h > 1 {
            buf.push_str(&format!("\x1b[{}A", h - 1));
        }
        buf.push('\r');
        for i in 0..h {
            buf.push_str("\x1b[K");
            if i + 1 < h {
                buf.push('\n');
            }
        }
        if h > 1 {
            buf.push_str(&format!("\x1b[{}A\r", h - 1));
        }
        self.live.height = 0;
        term::write_out(&buf);
    }
}

/* ---- formatting -------------------------------------------------------- */

fn expired(p: &Phase, now: Instant) -> bool {
    p.done_at
        .map_or(false, |t| now.duration_since(t).as_millis() > PHASE_LINGER_MS)
}

fn render_status_line(s: &Status) -> String {
    let base = s.text.trim_end_matches('.');
    let dots = [".", "..", "..."][s.frame % 3];
    format!("{}{}", base, dots)
}

fn render_progress_line(p: &Phase, now: Instant, utf8: bool) -> String {
    let (filled, pct) = if p.total > 0 {
        let frac = (p.done as f64 / p.total as f64).clamp(0.0, 1.0);
        let filled = (frac * BAR_WIDTH as f64).round() as usize;
        (filled.min(BAR_WIDTH), format!("{:.0}%", frac * 100.0))
    } else {
        // Indeterminate: sweep the fill so the line still shows movement.
        let step = (now.duration_since(p.t0).as_millis() / 120) as usize % (BAR_WIDTH + 1);
        (step, "?%".to_string())
    };

    let (on, off) = if utf8 { ('\u{2588}', '\u{2591}') } else { ('#', '-') };
    let mut bar = String::with_capacity(BAR_WIDTH);
    for i in 0..BAR_WIDTH {
        bar.push(if i < filled { on } else { off });
    }

    let count = if p.total > 0 {
        format!("{}/{}", p.done, p.total)
    } else {
        format!("{}", p.done)
    };
    let rate = if p.rate > 0.0 {
        human_rate(p.rate)
    } else {
        "--".to_string()
    };
    /* Spec: [num]/[all] （done size） [size]/s — fullwidth parens per m02304. */
    format!(
        "{} [{}] {} {} （{}） {} ETA:{}",
        p.name,
        bar,
        pct,
        count,
        human_size(p.done.max(0) as u64),
        rate,
        eta_text(p, now)
    )
}

fn human_size(bytes: u64) -> String {
    const K: f64 = 1024.0;
    let b = bytes as f64;
    if b >= K * K * K {
        format!("{:.3}GB", b / (K * K * K))
    } else if b >= K * K {
        format!("{:.3}MB", b / (K * K))
    } else if b >= K {
        format!("{:.3}KB", b / K)
    } else {
        format!("{}B", bytes)
    }
}

fn human_rate(rate: f64) -> String {
    const K: f64 = 1024.0;
    if rate >= K * K * K {
        format!("{:.1}GB/s", rate / (K * K * K))
    } else if rate >= K * K {
        format!("{:.1}MB/s", rate / (K * K))
    } else if rate >= K {
        format!("{:.1}KB/s", rate / K)
    } else {
        format!("{:.0}B/s", rate)
    }
}

fn eta_text(p: &Phase, now: Instant) -> String {
    if p.total <= 0 || p.done <= 0 {
        return "--:--:--".to_string();
    }
    let frac = (p.done as f64 / p.total as f64).clamp(0.0, 1.0);
    if frac >= 1.0 {
        return hms(0);
    }
    let elapsed = now.duration_since(p.t0).as_secs_f64();
    hms((elapsed * (1.0 - frac) / frac).max(0.0) as u64)
}

fn hms(total: u64) -> String {
    let hours = total / 3600;
    if hours > 99 {
        return ">99:59:59".to_string();
    }
    let minutes = (total % 3600) / 60;
    let seconds = total % 60;
    format!("{:02}:{:02}:{:02}", hours, minutes, seconds)
}

/* ---- stdin ------------------------------------------------------------- */

fn read_answer(tty: bool) -> i32 {
    use std::io::BufRead;
    let mut attempts = 0;
    loop {
        let mut line = String::new();
        match std::io::stdin().lock().read_line(&mut line) {
            Ok(0) => return 0, // EOF
            Ok(_) => {}
            Err(_) => return 0,
        }
        match line.trim().to_ascii_lowercase().as_str() {
            "y" | "yes" => return 1,
            "n" | "no" | "" => return 0,
            _ => {
                attempts += 1;
                if !tty || attempts >= 8 {
                    return 0;
                }
                term::write_out("[y/n]: ");
            }
        }
    }
}
