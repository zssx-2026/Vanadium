//! inv - the Rust shell over the Vanadium kernel.
//!
//! Responsibilities (and nothing else):
//!   * parse the shell flags `--json` / `--lang <code>`,
//!   * locate and load `vanadium.dll`, resolve the C ABI,
//!   * render kernel events, animate the replaceable lines, draw progress bars,
//!   * answer prompts, translate Ctrl+C into a cancel return,
//!   * hand the remaining argv to the kernel and propagate its exit code.
//!
//! All business logic, text and exit codes for commands live in the kernel.

mod ffi;
mod render;
mod term;

use std::ffi::{c_char, c_int, c_void, CString};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

const EXIT_USAGE: i32 = 1;
const EXIT_KERNEL: i32 = 2;
const EXIT_CANCELLED: i32 = 130;

const USAGE: &str =
    "usage: inv [--json] [--lang <code>] <command> [args...]\nrun \"inv help\" for the command list.\n";

struct Args {
    json: bool,
    lang: Option<String>,
    /// Everything the shell does not consume; forwarded to the kernel verbatim.
    rest: Vec<String>,
}

fn main() {
    let code = run();
    std::process::exit(code);
}

fn run() -> i32 {
    let cons = term::Console::detect();

    let args = match parse_args() {
        Ok(a) => a,
        Err(msg) => {
            term::write_err(&format!("[error] {}\n{}", msg, USAGE));
            return EXIT_USAGE;
        }
    };

    // Test hook: dump the parsed shell plan without touching the kernel.
    if env_flag("INV_SHELL_PROBE") {
        print_probe(&args);
        return 0;
    }

    let ctrl_installed = ffi::install_ctrl_handler();
    if env_flag("INV_SHELL_VERBOSE") {
        term::write_err(&format!(
            "[debug] ctrl-c handler: {}\n",
            if ctrl_installed { "installed" } else { "FAILED" }
        ));
    }

    // Test hook: raise the cancel flag after N ms, exactly as Ctrl+C does, so the
    // cancel path can be exercised without a console event.
    if let Some(ms) = env_value("INV_SHELL_CANCEL_AFTER_MS").and_then(|v| v.trim().parse::<u64>().ok()) {
        std::thread::spawn(move || {
            std::thread::sleep(Duration::from_millis(ms));
            ffi::request_cancel();
        });
    }

    let kernel = match ffi::load() {
        Ok(k) => k,
        Err(ffi::LoadError::NotFound(tried)) => {
            term::write_err("[error] Vanadium kernel not found.\n");
            if env_flag("INV_SHELL_VERBOSE") {
                for path in tried {
                    term::write_err(&format!("  tried: {}\n", path.display()));
                }
            }
            return EXIT_KERNEL;
        }
        Err(ffi::LoadError::Invalid(symbol)) => {
            term::write_err(&format!(
                "[error] Vanadium kernel is invalid: missing export {}.\n",
                symbol
            ));
            return EXIT_KERNEL;
        }
    };

    if env_flag("INV_SHELL_VERBOSE") {
        let version = unsafe { ffi::cstr((kernel.version)()) };
        let abi = match kernel.abi {
            Some(f) => unsafe { ffi::cstr(f()) },
            None => "?".to_string(),
        };
        term::write_err(&format!("[debug] kernel version={} abi={}\n", version, abi));
    }

    render::init(args.json, cons);
    let stop = Arc::new(AtomicBool::new(false));
    let animator = {
        let stop = stop.clone();
        std::thread::spawn(move || {
            while !stop.load(Ordering::Relaxed) {
                std::thread::sleep(Duration::from_millis(25));
                render::with(|r| r.tick());
            }
        })
    };
    let shutdown = |stop: &Arc<AtomicBool>, animator: std::thread::JoinHandle<()>| {
        stop.store(true, Ordering::Relaxed);
        let _ = animator.join();
    };

    // Test hook: run the kernel self-test instead of a command.
    if let Some(filter) = env_value("INV_SHELL_SELFTEST") {
        let c_filter = CString::new(filter.clone()).unwrap_or_default();
        let ptr = if filter.is_empty() {
            std::ptr::null()
        } else {
            c_filter.as_ptr()
        };
        let failures = unsafe { (kernel.self_test)(ptr, event_cb, std::ptr::null_mut()) };
        shutdown(&stop, animator);
        render::with(|r| r.finish());
        return if failures > 0 { 1 } else { 0 };
    }

    // argv as the kernel expects it: argv[0] = "inv", then the command line.
    let mut owned: Vec<CString> = Vec::with_capacity(args.rest.len() + 1);
    owned.push(CString::new("inv").expect("static"));
    for arg in &args.rest {
        match CString::new(arg.as_str()) {
            Ok(c) => owned.push(c),
            Err(_) => {
                term::write_err("[error] argument contains a NUL byte.\n");
                shutdown(&stop, animator);
                return EXIT_USAGE;
            }
        }
    }
    let mut argv: Vec<*const c_char> = owned.iter().map(|c| c.as_ptr()).collect();
    argv.push(std::ptr::null());
    let argc = (argv.len() - 1) as c_int;

    let home = CString::new(home_dir()).unwrap_or_else(|_| CString::new(".").expect("static"));
    let lang = args.lang.as_ref().and_then(|l| CString::new(l.as_str()).ok());
    let lang_ptr = match &lang {
        Some(c) => c.as_ptr(),
        None => std::ptr::null(),
    };

    let ctx = unsafe { (kernel.create)(home.as_ptr(), lang_ptr) };
    if ctx.is_null() {
        shutdown(&stop, animator);
        render::with(|r| r.finish());
        term::write_err("[error] Vanadium kernel failed to initialise.\n");
        return EXIT_KERNEL;
    }

    let rc = unsafe { (kernel.run)(ctx, argc, argv.as_ptr(), event_cb, std::ptr::null_mut()) };
    unsafe { (kernel.destroy)(ctx) };

    shutdown(&stop, animator);
    let had_error = render::with(|r| {
        r.finish();
        r.had_error()
    });

    let mut code = rc;
    if code == 0 && ffi::cancel_requested() {
        code = EXIT_CANCELLED;
    }
    if code == 0 && had_error {
        code = 1;
    }
    code
}

/// Events that a pending Ctrl+C can abort. Plain output (LINE/WARN/ERROR/SUMMARY)
/// keeps being rendered after the flag is set, otherwise the kernel's own
/// "Operation cancelled, exit." line could never reach the screen.
fn cancellable(kind: c_int) -> bool {
    matches!(
        kind,
        ffi::VD_EV_STATUS | ffi::VD_EV_PROGRESS | ffi::VD_EV_PROMPT
    )
}

/// The kernel callback. Runs on the kernel thread and must stay cheap.
unsafe extern "system" fn event_cb(_user: *mut c_void, ev: *const ffi::VdEvent) -> c_int {
    if ev.is_null() {
        return 0;
    }
    let event = *ev;
    if ffi::CTRL_C.load(Ordering::SeqCst) > 0 && cancellable(event.kind) {
        return -1; // cancels the running command; the kernel prints its own notice
    }
    render::with(|r| r.event(&event))
}

/* ---- shell flags ------------------------------------------------------- */

fn parse_args() -> Result<Args, String> {
    let mut json = false;
    let mut lang: Option<String> = None;
    let mut rest: Vec<String> = Vec::new();
    let mut passthrough = false;

    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        if passthrough {
            rest.push(arg);
            continue;
        }
        match arg.as_str() {
            "--" => {
                passthrough = true;
                rest.push(arg);
            }
            "--json" => json = true,
            "--lang" => match it.next() {
                Some(value) => lang = Some(value),
                None => return Err("--lang requires a value".to_string()),
            },
            _ => {
                if let Some(value) = arg.strip_prefix("--lang=") {
                    if value.is_empty() {
                        return Err("--lang requires a value".to_string());
                    }
                    lang = Some(value.to_string());
                } else {
                    rest.push(arg);
                }
            }
        }
    }

    Ok(Args { json, lang, rest })
}

fn home_dir() -> String {
    /* App data (settings, credentials, caches) lives in %APPDATA%/inv —
       never in the user profile root or the working directory. */
    if let Some(value) = env_value("INV_HOME") {
        return value;
    }
    if let Some(appdata) = env_value("APPDATA") {
        let joined = format!("{}\\inv", appdata.trim_end_matches('\\'));
        return joined;
    }
    match (std::env::var("HOMEDRIVE"), std::env::var("HOMEPATH")) {
        (Ok(drive), Ok(path)) => format!("{}\\inv", format!("{}{}", drive, path)),
        _ => ".".to_string(),
    }
}

fn env_value(key: &str) -> Option<String> {
    std::env::var(key).ok().filter(|v| !v.is_empty())
}

fn env_flag(key: &str) -> bool {
    match std::env::var(key).ok() {
        Some(value) => !value.is_empty() && value != "0",
        None => false,
    }
}

/* ---- probe (test hook) ------------------------------------------------- */

fn print_probe(args: &Args) {
    let candidates: Vec<String> = ffi::candidates()
        .iter()
        .map(|p| p.display().to_string())
        .collect();
    let lang = match &args.lang {
        Some(value) => format!("\"{}\"", json_escape(value)),
        None => "null".to_string(),
    };
    let text = format!(
        "{{\"shell\":\"inv\",\"json\":{},\"lang\":{},\"home\":\"{}\",\"command\":{},\"candidates\":{}}}",
        args.json,
        lang,
        json_escape(&home_dir()),
        json_array(&args.rest),
        json_array(&candidates)
    );
    term::write_out(&format!("{}\n", text));
}

fn json_array(items: &[String]) -> String {
    let body: Vec<String> = items
        .iter()
        .map(|item| format!("\"{}\"", json_escape(item)))
        .collect();
    format!("[{}]", body.join(","))
}

fn json_escape(text: &str) -> String {
    let mut out = String::with_capacity(text.len() + 8);
    for ch in text.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}
