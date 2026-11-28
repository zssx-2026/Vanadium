//! Raw Win32 declarations + the Vanadium C ABI surface.
//!
//! Everything here mirrors `include/vanadium/vd.h` (VD_ABI_VERSION 1). The shell
//! never reimplements kernel behaviour: it only loads, calls and renders.

use std::ffi::{c_char, c_int, c_void};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};

/* ---- vd.h: event kinds ------------------------------------------------- */

pub const VD_EV_LINE: i32 = 0;
pub const VD_EV_STATUS: i32 = 1;
pub const VD_EV_PROGRESS: i32 = 2;
pub const VD_EV_ERROR: i32 = 3;
pub const VD_EV_PROMPT: i32 = 4;
pub const VD_EV_SUMMARY: i32 = 5;
pub const VD_EV_STATUS_CLEAR: i32 = 6;
pub const VD_EV_TITLE: i32 = 7;
pub const VD_EV_WARN: i32 = 8;
pub const VD_EV_PROMPT_TEXT: i32 = 9;

/// `vd_event` - layout must stay identical to the C struct (i64 fields are 8-byte
/// aligned after two pointers on x64, hence the implicit padding is zero).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VdEvent {
    pub kind: c_int,
    pub a: *const c_char,
    pub b: *const c_char,
    pub i1: i64,
    pub i2: i64,
    pub i3: i64,
    pub i4: i64,
    pub resp: *mut c_char,
    pub resp_cap: i64,
}

/// Return 1 = yes, 0 = no, -1 = cancel.
pub type EventCb = unsafe extern "system" fn(*mut c_void, *const VdEvent) -> c_int;

pub type FnCreate = unsafe extern "system" fn(*const c_char, *const c_char) -> *mut c_void;
pub type FnDestroy = unsafe extern "system" fn(*mut c_void);
pub type FnRun =
    unsafe extern "system" fn(*mut c_void, c_int, *const *const c_char, EventCb, *mut c_void) -> c_int;
pub type FnVersion = unsafe extern "system" fn() -> *const c_char;
pub type FnAbi = unsafe extern "system" fn() -> *const c_char;
pub type FnSelfTest =
    unsafe extern "system" fn(*const c_char, EventCb, *mut c_void) -> c_int;

/* ---- kernel32 ---------------------------------------------------------- */

pub const STD_INPUT_HANDLE: u32 = -10i32 as u32;
pub const STD_OUTPUT_HANDLE: u32 = -11i32 as u32;

#[link(name = "kernel32")]
extern "system" {
    pub fn GetStdHandle(id: u32) -> *mut c_void;
    pub fn GetConsoleMode(h: *mut c_void, mode: *mut u32) -> i32;
    pub fn SetConsoleMode(h: *mut c_void, mode: u32) -> i32;
    pub fn SetConsoleOutputCP(cp: u32) -> i32;
    pub fn SetConsoleCP(cp: u32) -> i32;
    pub fn SetConsoleTitleW(title: *const u16) -> i32;
    pub fn LoadLibraryW(name: *const u16) -> *mut c_void;
    pub fn GetProcAddress(h: *mut c_void, name: *const c_char) -> *mut c_void;
    pub fn SetConsoleCtrlHandler(
        handler: Option<unsafe extern "system" fn(u32) -> i32>,
        add: i32,
    ) -> i32;
    pub fn ExitProcess(code: u32) -> !;
}

/* ---- helpers ----------------------------------------------------------- */

/// Copy a kernel-owned C string. Never keeps the pointer around.
pub fn cstr(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    unsafe { std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned() }
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

pub fn exe_dir() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_else(|| PathBuf::from("."))
}

/// Search order required by the design: exe directory, then `..\lib`, then PATH.
pub fn candidates() -> Vec<PathBuf> {
    let dir = exe_dir();
    vec![
        dir.join("vanadium.dll"),
        dir.join("..").join("lib").join("vanadium.dll"),
        PathBuf::from("vanadium.dll"),
    ]
}

pub enum LoadError {
    NotFound(Vec<PathBuf>),
    Invalid(&'static str),
}

pub struct Kernel {
    #[allow(dead_code)] // keep the module handle alive for the process lifetime
    pub handle: *mut c_void,
    pub create: FnCreate,
    pub destroy: FnDestroy,
    pub run: FnRun,
    pub version: FnVersion,
    pub abi: Option<FnAbi>,
    pub self_test: FnSelfTest,
}

unsafe fn sym(h: *mut c_void, name: &'static [u8]) -> *mut c_void {
    GetProcAddress(h, name.as_ptr() as *const c_char)
}

/// Load vanadium.dll and resolve the ABI. Callers decide how to report failure.
pub fn load() -> Result<Kernel, LoadError> {
    let tried = candidates();
    for path in &tried {
        let h = unsafe { LoadLibraryW(wide(&path.to_string_lossy()).as_ptr()) };
        if h.is_null() {
            continue;
        }
        unsafe {
            let create = sym(h, b"vd_create\0");
            let destroy = sym(h, b"vd_destroy\0");
            let run = sym(h, b"vd_run\0");
            let version = sym(h, b"vd_version\0");
            let self_test = sym(h, b"vd_self_test\0");
            let abi = sym(h, b"vd_abi\0");

            let missing = if create.is_null() {
                Some("vd_create")
            } else if destroy.is_null() {
                Some("vd_destroy")
            } else if run.is_null() {
                Some("vd_run")
            } else if version.is_null() {
                Some("vd_version")
            } else if self_test.is_null() {
                Some("vd_self_test")
            } else {
                None
            };
            if let Some(name) = missing {
                return Err(LoadError::Invalid(name));
            }

            return Ok(Kernel {
                handle: h,
                create: std::mem::transmute::<*mut c_void, FnCreate>(create),
                destroy: std::mem::transmute::<*mut c_void, FnDestroy>(destroy),
                run: std::mem::transmute::<*mut c_void, FnRun>(run),
                version: std::mem::transmute::<*mut c_void, FnVersion>(version),
                abi: if abi.is_null() {
                    None
                } else {
                    Some(std::mem::transmute::<*mut c_void, FnAbi>(abi))
                },
                self_test: std::mem::transmute::<*mut c_void, FnSelfTest>(self_test),
            });
        }
    }
    Err(LoadError::NotFound(tried))
}

/* ---- Ctrl+C ------------------------------------------------------------ */

pub static CTRL_C: AtomicU32 = AtomicU32::new(0);

unsafe extern "system" fn on_ctrl(ty: u32) -> i32 {
    // CTRL_C_EVENT only; every other console event keeps the default handling.
    if ty != 0 {
        return 0;
    }
    let seen = CTRL_C.fetch_add(1, Ordering::SeqCst);
    if seen >= 1 {
        // Second Ctrl+C: leave immediately, the kernel gets no chance to unwind.
        ExitProcess(130);
    }
    1 // swallow the first one; the event callback answers -1 from now on
}

pub fn install_ctrl_handler() -> bool {
    unsafe { SetConsoleCtrlHandler(Some(on_ctrl), 1) != 0 }
}

/// Raise the cancel flag as if Ctrl+C had been pressed (test hook).
pub fn request_cancel() {
    CTRL_C.fetch_add(1, Ordering::SeqCst);
}

pub fn cancel_requested() -> bool {
    CTRL_C.load(Ordering::SeqCst) > 0
}
