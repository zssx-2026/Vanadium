#pragma once
/* Vanadium portability layer: the narrow set of OS capabilities the kernel and
   shell actually use, expressed once and implemented per platform.

   Design rules (do not relax without a note in docs/PORTABILITY.md):
     - No C++ exceptions. Every fallible call returns Status; values come back
       through out-parameters. vd::fs in include/vanadium/core.h keeps its
       throwing surface and is expected to be reimplemented on top of this file.
     - Every path is UTF-8 and uses '/' as the internal separator. Backslash is
       accepted on Windows for compatibility with user-supplied paths; on POSIX
       it is an ordinary filename byte and is never rewritten.
     - No global constructors, no allocation at static-init time, no locale or
       std::filesystem dependency: the layer must link into the DLL and into the
       freestanding probe binary alike.
     - Functions are additive: a caller that needs a new capability adds it here
       and in both backends in the same change, never by calling Win32/POSIX
       directly from src/core.

   Backends:
     src/platform/win32/platform_win32.cpp   Win32 (Win32 file APIs, Schannel host)
     src/platform/posix/platform_posix.cpp   POSIX (Linux, macOS)

   Platform selection macros are defined below; a backend is compiled exactly
   when VD_PLATFORM_WINDOWS / VD_PLATFORM_POSIX is set. */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/* ------------------------------------------------------------------ platform */

#if defined(_WIN32)
#define VD_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#define VD_PLATFORM_POSIX 1
#define VD_PLATFORM_MACOS 1
#elif defined(__linux__)
#define VD_PLATFORM_POSIX 1
#define VD_PLATFORM_LINUX 1
#else
#error "vanadium platform layer: unsupported target (expected Windows, Linux or macOS)"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define VD_ARCH_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define VD_ARCH_ARM64 1
#else
#define VD_ARCH_OTHER 1
#endif

namespace vd {
namespace platform {

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;

/* Compile-time platform description, for logging and for self-test output. */
const char* os_name();    /* "windows" | "linux" | "macos" */
const char* arch_name();  /* "x64" | "arm64" | "unknown" */

/* -------------------------------------------------------------------- status */

/* Error classes are deliberately small and stable: they are what the kernel
   maps onto Failure codes and what the CLI prints. Unknown native failures fall
   into Io/Unknown and keep the raw code in Status::native. */
enum class ErrCode : int {
  Ok = 0,
  NotFound = 1,     /* path does not exist, or a parent does not */
  Denied = 2,       /* EACCES/EPERM, ERROR_ACCESS_DENIED */
  Exists = 3,       /* target already exists and replace was not requested */
  NotADirectory = 4,
  IsADirectory = 5,
  Busy = 6,         /* EBUSY/ETXTBSY, ERROR_SHARING_VIOLATION, locked */
  NoSpace = 7,      /* ENOSPC, ERROR_DISK_FULL */
  InvalidName = 8,  /* EINVAL, ERROR_INVALID_NAME, empty path */
  NameTooLong = 9,  /* ENAMETOOLONG, ERROR_FILENAME_EXCED_RANGE */
  NotSupported = 10,
  Io = 11,          /* short read/write, EIO */
  Unknown = 99,
};

const char* error_name(ErrCode code);

struct Status {
  ErrCode code = ErrCode::Ok;
  /* Raw errno (POSIX) or GetLastError() (Windows) that produced the class.
     Zero when the failure did not come from the OS (e.g. validation). */
  int native = 0;

  bool ok() const { return code == ErrCode::Ok; }
  explicit operator bool() const { return ok(); }
};

Status ok_status();
/* Classify a native errno / GetLastError value on the current platform. */
Status status_from_native(int native);
/* "not-found(2): No such file or directory" — for Failure::detail strings. */
std::string status_text(const Status& status);

/* ---------------------------------------------------------------------- path */

/* Pure string operations. No syscalls, no allocation of OS state: they are the
   only path helpers the kernel may use, so path syntax stays in one place. */
namespace path {

/* '/' on every platform; kept as a function so callers never hardcode it. */
char separator();

/* Join base and leaf. Contract:
     - an empty base returns leaf unchanged, an empty leaf returns base;
     - an absolute leaf replaces base entirely (including "C:/x" on Windows);
     - exactly one separator is inserted, no trailing separator is added. */
std::string join(const std::string& base, const std::string& leaf);

/* Collapse '.', '..' and duplicate separators, convert '\' to '/' on Windows
   only, keep a leading '/' (and a "X:" drive prefix on Windows). The result
   never ends with '/' except for the filesystem root. Empty input yields ".". */
std::string normalize(const std::string& p);

std::string parent(const std::string& p);    /* "." for a bare name, "/" at root */
std::string filename(const std::string& p);  /* last component, may be empty at root */
std::string stem(const std::string& p);      /* filename without its last extension */
std::string ext(const std::string& p);       /* ".txt" or "" ; leading dot not an extension */

bool is_absolute(const std::string& p);

}  // namespace path

/* ---------------------------------------------------------------- attributes */

struct Attrs {
  bool exists = false;
  bool is_dir = false;
  bool is_symlink = false;
  bool read_only = false;
  i64 size = 0;      /* bytes; 0 for directories */
  i64 mtime_ms = 0;  /* Unix epoch milliseconds, UTC; 0 when unavailable */
};

struct DirEntry {
  std::string name; /* file name only, no directory part */
  bool is_dir = false;
};

/* ---------------------------------------------------------------------- file */

namespace file {

/* Stat without following into an error: a missing path yields Ok with
   Attrs::exists == false. Only a real failure (permissions, I/O) is non-Ok. */
Status attrs(const std::string& p, Attrs& out);

bool exists(const std::string& p);
bool is_dir(const std::string& p);
bool is_file(const std::string& p);

/* -1 when the path cannot be stat'ed. */
i64 size(const std::string& p);
/* 0 when the path cannot be stat'ed (matches the current kernel behaviour). */
i64 mtime_ms(const std::string& p);

/* Recursive directory creation; an existing directory is success. The empty
   path is InvalidName. Parent components are created with default permissions
   (0755 & ~umask on POSIX, inheriting ACLs on Windows). */
Status mkdirs(const std::string& p);

/* Directory listing, names only, sorted by byte value. A missing directory is
   NotFound; entries "." and ".." are never returned. */
Status list_dir(const std::string& p, std::vector<std::string>& names);
Status list_entries(const std::string& p, std::vector<DirEntry>& entries);

Status remove_file(const std::string& p);   /* missing file is Ok (idempotent) */
Status remove_all(const std::string& p);    /* recursive; missing path is Ok */

/* Rename/replace. With replace_existing == false an existing target yields
   Exists. Cross-device moves are NOT required to work (callers copy+delete). */
Status rename(const std::string& from, const std::string& to, bool replace_existing = true);

Status copy_file(const std::string& from, const std::string& to, bool replace_existing = true);

/* Whole-file read. read_file_opt returns Ok with out cleared when the file is
   missing (mirrors fs::read_file_opt). */
Status read_file(const std::string& p, std::string& out);
Status read_file_opt(const std::string& p, std::string& out);

/* Whole-file write; create_parents creates the parent directory first.
   write_file_atomic writes "<p>.tmp<pid>" then renames over p. */
Status write_file(const std::string& p, const std::string& data, bool create_parents = true);
Status write_file_atomic(const std::string& p, const std::string& data);

/* Bytes available to the calling user on the volume holding p. */
Status free_space(const std::string& p, u64& out);

/* Scratch directory without a trailing separator ("/tmp", "%TEMP%"). */
std::string temp_dir();
/* Per-user application data root: %APPDATA% on Windows, $XDG_CONFIG_HOME or
   $HOME/.config on Linux, $HOME/Library/Application Support on macOS.
   Falls back to temp_dir() when the environment is unusable. */
std::string appdata_dir();

}  // namespace file

/* ---------------------------------------------------------------- directory */

namespace dir {

std::string current();                                  /* absolute, '/' separated */
Status set_current(const std::string& p);

}  // namespace dir

/* ----------------------------------------------------------------------- env */

namespace env {

/* Unset or empty both yield Ok with found == false. */
Status get(const std::string& name, std::string& out, bool* found = nullptr);
Status set(const std::string& name, const std::string& value);
Status unset(const std::string& name);

}  // namespace env

/* ---------------------------------------------------------------------- time */

namespace time {

/* Wall clock, Unix epoch milliseconds. */
i64 now_unix_ms();
/* Monotonic milliseconds since an unspecified epoch (never goes backwards). */
i64 monotonic_ms();
/* High-resolution counter and its frequency in Hz (QueryPerformanceCounter /
   clock_gettime(CLOCK_MONOTONIC)). Use (t1-t0)*1000/freq for milliseconds. */
i64 perf_counter();
i64 perf_frequency();
/* Sleep at least the requested time; negative values return immediately. */
void sleep_ms(i64 ms);

}  // namespace time

/* ------------------------------------------------------------------- console */

namespace console {

enum class Stream : int { Out = 1, Err = 2 };

/* Terminal width in columns; 80 when not a terminal or unknown. */
int width();
bool is_tty(Stream s);

/* Write UTF-8 bytes. On Windows this converts to UTF-16 and uses WriteConsoleW
   for terminals (so non-ASCII survives a non-UTF-8 code page) and WriteFile
   for redirected handles. Returns the number of bytes accepted or -1. */
i64 write_utf8(Stream s, const std::string& text);
i64 write_line_utf8(Stream s, const std::string& text);

/* Put the process console into UTF-8 mode (no-op where already UTF-8).
   Returns false when the platform refused; callers must keep working. */
bool enable_utf8();

}  // namespace console

/* ------------------------------------------------------------------- process */

namespace process {

int id();

/* Absolute path of the running executable; empty when it cannot be resolved. */
std::string executable_path();

}  // namespace process

}  // namespace platform
}  // namespace vd
