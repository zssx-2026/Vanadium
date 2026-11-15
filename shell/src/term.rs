//! Console plumbing: code page, VT mode, tty detection, raw output.
//!
//! The shell decides *how* to draw; it never decides *what* to say.

use std::io::Write;

use crate::ffi::{
    GetConsoleMode, GetStdHandle, SetConsoleCP, SetConsoleMode, SetConsoleOutputCP,
    SetConsoleTitleW, STD_INPUT_HANDLE, STD_OUTPUT_HANDLE,
};

const ENABLE_PROCESSED_OUTPUT: u32 = 0x0001;
const ENABLE_VIRTUAL_TERMINAL_PROCESSING: u32 = 0x0004;
const CP_UTF8: u32 = 65_001;

#[derive(Clone, Copy)]
pub struct Console {
    /// stdout is a real console (not a pipe or a file).
    pub is_console: bool,
    /// ANSI/VT escapes are honoured, therefore the live region may be redrawn.
    pub vt: bool,
    /// The console renders UTF-8, so block glyphs are safe to use.
    pub utf8: bool,
}

impl Console {
    pub fn detect() -> Console {
        let mut is_console = false;
        let mut vt = false;
        let mut utf8 = false;

        unsafe {
            let h = GetStdHandle(STD_OUTPUT_HANDLE);
            if !h.is_null() && h as isize != -1 {
                let mut mode: u32 = 0;
                if GetConsoleMode(h, &mut mode) != 0 {
                    is_console = true;
                    if SetConsoleOutputCP(CP_UTF8) != 0 {
                        utf8 = true;
                    }
                    let _ = SetConsoleCP(CP_UTF8);
                    let want =
                        mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                    if SetConsoleMode(h, want) != 0 {
                        vt = true;
                    }
                }
            }
        }

        // Test hooks so the rendering paths can be exercised without a console.
        match std::env::var("INV_SHELL_VT").ok().as_deref() {
            Some("1") => {
                vt = true;
                utf8 = std::env::var_os("INV_SHELL_ASCII").is_none();
            }
            Some("0") => vt = false,
            _ => {}
        }

        Console {
            is_console,
            vt,
            utf8,
        }
    }
}

/// stdin is an interactive console (not a pipe, file or NUL).
pub fn stdin_is_tty() -> bool {
    unsafe {
        let h = GetStdHandle(STD_INPUT_HANDLE);
        if h.is_null() || h as isize == -1 {
            return false;
        }
        let mut mode: u32 = 0;
        GetConsoleMode(h, &mut mode) != 0
    }
}

pub fn set_title(title: &str) {
    let wide: Vec<u16> = title.encode_utf16().chain(std::iter::once(0)).collect();
    unsafe {
        SetConsoleTitleW(wide.as_ptr());
    }
}

pub fn write_out(s: &str) {
    let out = std::io::stdout();
    let mut lock = out.lock();
    let _ = lock.write_all(s.as_bytes());
    let _ = lock.flush();
}

pub fn write_err(s: &str) {
    let err = std::io::stderr();
    let mut lock = err.lock();
    let _ = lock.write_all(s.as_bytes());
    let _ = lock.flush();
}
