/* Win32 backend for include/vanadium/platform.h.
   Compiled only when VD_PLATFORM_WINDOWS is defined; see docs/PORTABILITY.md.
   This is the behaviour the kernel had inline before the port, expressed once
   so Linux/macOS can provide the same contracts. */
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "vanadium/platform.h"

#if defined(VD_PLATFORM_WINDOWS)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vd {
namespace platform {
namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return std::wstring();
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                nullptr, nullptr);
  if (n <= 0) return std::string();
  std::string s(static_cast<std::size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                        nullptr);
  return s;
}

std::string strip_trailing_separator(std::string s) {
  while (s.size() > 1 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
  if (s.size() == 3 && s[1] == ':') return s; /* keep "C:\" style roots intact */
  return s;
}

/* 100ns FILETIME ticks -> Unix epoch milliseconds. */
i64 filetime_to_ms(const FILETIME& ft) {
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  if (u.QuadPart == 0) return 0;
  return static_cast<i64>(u.QuadPart / 10000ULL) - 11644473600000LL;
}

Status error_from_win32(DWORD code) { return status_from_native(static_cast<int>(code)); }

}  // namespace

/* -------------------------------------------------------------- status */

const char* error_name(ErrCode code) {
  switch (code) {
    case ErrCode::Ok: return "ok";
    case ErrCode::NotFound: return "not-found";
    case ErrCode::Denied: return "denied";
    case ErrCode::Exists: return "exists";
    case ErrCode::NotADirectory: return "not-a-directory";
    case ErrCode::IsADirectory: return "is-a-directory";
    case ErrCode::Busy: return "busy";
    case ErrCode::NoSpace: return "no-space";
    case ErrCode::InvalidName: return "invalid-name";
    case ErrCode::NameTooLong: return "name-too-long";
    case ErrCode::NotSupported: return "not-supported";
    case ErrCode::Io: return "io";
    case ErrCode::Unknown: return "unknown";
  }
  return "unknown";
}

Status ok_status() { return Status{}; }

Status status_from_native(int native) {
  Status s;
  s.native = native;
  switch (static_cast<DWORD>(native)) {
    case ERROR_SUCCESS: s.code = ErrCode::Ok; return s;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_NO_MORE_FILES:
    case ERROR_INVALID_DRIVE: s.code = ErrCode::NotFound; return s;
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD: s.code = ErrCode::Denied; return s;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: s.code = ErrCode::Exists; return s;
    case ERROR_DIRECTORY: s.code = ErrCode::NotADirectory; return s;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_BUSY: s.code = ErrCode::Busy; return s;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL: s.code = ErrCode::NoSpace; return s;
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
    case ERROR_BAD_PATHNAME: s.code = ErrCode::InvalidName; return s;
    case ERROR_FILENAME_EXCED_RANGE: s.code = ErrCode::NameTooLong; return s;
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED: s.code = ErrCode::NotSupported; return s;
    case ERROR_IO_DEVICE:
    case ERROR_CRC:
    case ERROR_HANDLE_EOF: s.code = ErrCode::Io; return s;
    default: s.code = ErrCode::Unknown; return s;
  }
}

std::string status_text(const Status& status) {
  std::string out = error_name(status.code);
  out += "(";
  out += std::to_string(status.native);
  out += ")";
  if (status.native != 0) {
    wchar_t* buffer = nullptr;
    DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, static_cast<DWORD>(status.native),
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (n != 0 && buffer != nullptr) {
      std::string text = narrow(std::wstring(buffer, n));
      while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
        text.pop_back();
      if (!text.empty()) {
        out += ": ";
        out += text;
      }
    }
    if (buffer != nullptr) ::LocalFree(buffer);
  }
  return out;
}

const char* os_name() { return "windows"; }

const char* arch_name() {
#if defined(VD_ARCH_X64)
  return "x64";
#elif defined(VD_ARCH_ARM64)
  return "arm64";
#else
  return "unknown";
#endif
}

/* ---------------------------------------------------------------- path */

namespace path {

char separator() { return '/'; }

bool is_absolute(const std::string& p) {
  if (p.empty()) return false;
  if (p[0] == '/' || p[0] == '\\') return true;
  return p.size() >= 3 && p[1] == ':' && (p[2] == '/' || p[2] == '\\');
}

std::string join(const std::string& base, const std::string& leaf) {
  if (base.empty()) return leaf;
  if (leaf.empty()) return base;
  if (is_absolute(leaf)) return leaf;
  if (base.back() == '/' || base.back() == '\\') return base + leaf;
  return base + "/" + leaf;
}

std::string normalize(const std::string& p) {
  if (p.empty()) return ".";
  std::string s = p;
  for (char& c : s)
    if (c == '\\') c = '/';

  std::string prefix; /* "C:" or "" */
  std::size_t i = 0;
  bool rooted = false;
  if (s.size() >= 2 && s[1] == ':') {
    prefix = s.substr(0, 2);
    i = 2;
    if (i < s.size() && s[i] == '/') rooted = true;
  } else if (!s.empty() && s[0] == '/') {
    rooted = true;
  }

  std::vector<std::string> parts;
  while (i <= s.size()) {
    std::size_t next = s.find('/', i);
    if (next == std::string::npos) next = s.size();
    std::string seg = s.substr(i, next - i);
    if (seg == "..") {
      if (!parts.empty()) parts.pop_back();
    } else if (!seg.empty() && seg != ".") {
      parts.push_back(seg);
    }
    if (next == s.size()) break;
    i = next + 1;
  }

  std::string out = prefix;
  if (rooted) out += "/";
  for (std::size_t k = 0; k < parts.size(); ++k) {
    if (k) out += "/";
    out += parts[k];
  }
  if (out.empty()) out = ".";
  return out;
}

std::string parent(const std::string& p) {
  std::string s = normalize(p);
  std::size_t pos = s.find_last_of('/');
  if (pos == std::string::npos) return ".";
  if (pos == 0) return "/";
  if (pos == 2 && s[1] == ':') return s.substr(0, 2) + "/";
  return s.substr(0, pos);
}

std::string filename(const std::string& p) {
  std::string s = normalize(p);
  std::size_t pos = s.find_last_of('/');
  return pos == std::string::npos ? s : s.substr(pos + 1);
}

std::string stem(const std::string& p) {
  std::string f = filename(p);
  std::size_t pos = f.find_last_of('.');
  return pos == std::string::npos || pos == 0 ? f : f.substr(0, pos);
}

std::string ext(const std::string& p) {
  std::string f = filename(p);
  std::size_t pos = f.find_last_of('.');
  return pos == std::string::npos || pos == 0 ? std::string() : f.substr(pos);
}

}  // namespace path

/* ---------------------------------------------------------------- file */

namespace file {

Status attrs(const std::string& p, Attrs& out) {
  out = Attrs{};
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  std::wstring w = widen(p);
  if (w.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  WIN32_FILE_ATTRIBUTE_DATA d{};
  if (!::GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &d)) {
    DWORD e = ::GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_INVALID_NAME)
      return ok_status(); /* exists == false */
    return error_from_win32(e);
  }
  out.exists = true;
  out.is_dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  out.is_symlink = (d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
  out.read_only = (d.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  out.size = out.is_dir ? 0
                        : static_cast<i64>((static_cast<u64>(d.nFileSizeHigh) << 32) |
                                           static_cast<u64>(d.nFileSizeLow));
  out.mtime_ms = filetime_to_ms(d.ftLastWriteTime);
  return ok_status();
}

bool exists(const std::string& p) {
  Attrs a;
  return attrs(p, a).ok() && a.exists;
}

bool is_dir(const std::string& p) {
  Attrs a;
  return attrs(p, a).ok() && a.exists && a.is_dir;
}

bool is_file(const std::string& p) {
  Attrs a;
  return attrs(p, a).ok() && a.exists && !a.is_dir;
}

i64 size(const std::string& p) {
  Attrs a;
  if (!attrs(p, a).ok() || !a.exists) return -1;
  return a.size;
}

i64 mtime_ms(const std::string& p) {
  Attrs a;
  if (!attrs(p, a).ok() || !a.exists) return 0;
  return a.mtime_ms;
}

Status mkdirs(const std::string& p) {
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  std::string s = path::normalize(p);
  if (s == "." || s == "/") return ok_status();
  std::size_t i = 0;
  std::string cur;
  if (s.size() >= 2 && s[1] == ':') {
    cur = s.substr(0, 2);
    i = 2;
  }
  while (i <= s.size()) {
    std::size_t next = s.find('/', i);
    if (next == std::string::npos) next = s.size();
    if (next > i) {
      cur += "/";
      cur += s.substr(i, next - i);
      std::wstring w = widen(cur);
      if (!::CreateDirectoryW(w.c_str(), nullptr)) {
        DWORD e = ::GetLastError();
        if (e != ERROR_ALREADY_EXISTS) {
          DWORD a = ::GetFileAttributesW(w.c_str());
          if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return error_from_win32(e);
        }
      }
    }
    if (next == s.size()) break;
    i = next + 1;
  }
  return ok_status();
}

Status list_entries(const std::string& p, std::vector<DirEntry>& entries) {
  entries.clear();
  const std::string pattern = path::join(p.empty() ? std::string(".") : p, "*");
  WIN32_FIND_DATAW fd{};
  HANDLE h = ::FindFirstFileW(widen(pattern).c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD e = ::GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_NO_MORE_FILES)
      return ok_status(); /* empty listing for a missing directory, as the kernel expects */
    return error_from_win32(e);
  }
  do {
    std::string name = narrow(std::wstring(fd.cFileName));
    if (name == "." || name == "..") continue;
    DirEntry e;
    e.name = std::move(name);
    e.is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    entries.push_back(std::move(e));
  } while (::FindNextFileW(h, &fd));
  ::FindClose(h);
  std::sort(entries.begin(), entries.end(),
            [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
  return ok_status();
}

Status list_dir(const std::string& p, std::vector<std::string>& names) {
  names.clear();
  std::vector<DirEntry> entries;
  Status s = list_entries(p, entries);
  if (!s.ok()) return s;
  names.reserve(entries.size());
  for (const auto& e : entries) names.push_back(e.name);
  return ok_status();
}

Status remove_file(const std::string& p) {
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  std::wstring w = widen(p);
  if (::DeleteFileW(w.c_str())) return ok_status();
  DWORD e = ::GetLastError();
  if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return ok_status();
  /* A read-only file must be cleared before it can be unlinked (kernel does the
     same for remove_all; doing it here keeps single-file deletes working too). */
  if (e == ERROR_ACCESS_DENIED) {
    DWORD a = ::GetFileAttributesW(w.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      ::SetFileAttributesW(w.c_str(), FILE_ATTRIBUTE_NORMAL);
      if (::DeleteFileW(w.c_str())) return ok_status();
      e = ::GetLastError();
    } else if (a != INVALID_FILE_ATTRIBUTES) {
      Status s;
      s.code = ErrCode::IsADirectory;
      s.native = static_cast<int>(e);
      return s;
    }
  }
  return error_from_win32(e);
}

Status remove_all(const std::string& p) {
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  Attrs a;
  Status s = attrs(p, a);
  if (!s.ok()) return s;
  if (!a.exists) return ok_status();
  if (a.is_dir && !a.is_symlink) {
    std::vector<std::string> names;
    s = list_dir(p, names);
    if (!s.ok()) return s;
    for (const auto& name : names) {
      Status child = remove_all(path::join(p, name));
      if (!child.ok()) return child;
    }
    std::wstring w = widen(p);
    if (!::RemoveDirectoryW(w.c_str())) {
      DWORD e = ::GetLastError();
      if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return ok_status();
      return error_from_win32(e);
    }
    return ok_status();
  }
  std::wstring w = widen(p);
  ::SetFileAttributesW(w.c_str(), FILE_ATTRIBUTE_NORMAL);
  return remove_file(p);
}

Status rename(const std::string& from, const std::string& to, bool replace_existing) {
  if (from.empty() || to.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  DWORD flags = MOVEFILE_COPY_ALLOWED;
  if (replace_existing) flags |= MOVEFILE_REPLACE_EXISTING;
  if (::MoveFileExW(widen(from).c_str(), widen(to).c_str(), flags)) return ok_status();
  return error_from_win32(::GetLastError());
}

Status copy_file(const std::string& from, const std::string& to, bool replace_existing) {
  if (from.empty() || to.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (::CopyFileW(widen(from).c_str(), widen(to).c_str(), replace_existing ? FALSE : TRUE))
    return ok_status();
  return error_from_win32(::GetLastError());
}

Status read_file_opt(const std::string& p, std::string& out) {
  out.clear();
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  HANDLE h = ::CreateFileW(widen(p).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD e = ::GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return ok_status();
    return error_from_win32(e);
  }
  LARGE_INTEGER sz{};
  if (!::GetFileSizeEx(h, &sz)) {
    Status s = error_from_win32(::GetLastError());
    ::CloseHandle(h);
    return s;
  }
  out.resize(static_cast<std::size_t>(sz.QuadPart));
  std::size_t off = 0;
  while (off < out.size()) {
    DWORD got = 0;
    DWORD want = static_cast<DWORD>(std::min<std::size_t>(out.size() - off, 1u << 24));
    if (!::ReadFile(h, out.data() + off, want, &got, nullptr)) {
      DWORD e = ::GetLastError();
      if (e != ERROR_HANDLE_EOF) {
        Status s = error_from_win32(e);
        ::CloseHandle(h);
        out.clear();
        return s;
      }
      break;
    }
    if (got == 0) break;
    off += got;
  }
  ::CloseHandle(h);
  out.resize(off);
  return ok_status();
}

Status read_file(const std::string& p, std::string& out) {
  out.clear();
  Attrs a;
  Status s = attrs(p, a);
  if (!s.ok()) return s;
  if (!a.exists) {
    Status missing;
    missing.code = ErrCode::NotFound;
    return missing;
  }
  return read_file_opt(p, out);
}

Status write_file(const std::string& p, const std::string& data, bool create_parents) {
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (create_parents) {
    Status s = mkdirs(path::parent(p));
    if (!s.ok()) return s;
  }
  HANDLE h = ::CreateFileW(widen(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return error_from_win32(::GetLastError());
  std::size_t off = 0;
  while (off < data.size()) {
    DWORD wrote = 0;
    DWORD want = static_cast<DWORD>(std::min<std::size_t>(data.size() - off, 1u << 24));
    if (!::WriteFile(h, data.data() + off, want, &wrote, nullptr)) {
      Status s = error_from_win32(::GetLastError());
      ::CloseHandle(h);
      return s;
    }
    if (wrote == 0) {
      ::CloseHandle(h);
      Status s;
      s.code = ErrCode::Io;
      return s;
    }
    off += wrote;
  }
  if (!::FlushFileBuffers(h)) {
    Status s = error_from_win32(::GetLastError());
    ::CloseHandle(h);
    return s;
  }
  ::CloseHandle(h);
  return ok_status();
}

Status write_file_atomic(const std::string& p, const std::string& data) {
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  const std::string tmp = p + ".tmp" + std::to_string(process::id());
  Status s = write_file(tmp, data, true);
  if (!s.ok()) return s;
  if (!::MoveFileExW(widen(tmp).c_str(), widen(p).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    Status moved = error_from_win32(::GetLastError());
    remove_file(tmp);
    return moved;
  }
  return ok_status();
}

Status free_space(const std::string& p, u64& out) {
  out = 0;
  ULARGE_INTEGER avail{};
  std::string target = p.empty() ? std::string(".") : p;
  if (!::GetDiskFreeSpaceExW(widen(target).c_str(), &avail, nullptr, nullptr)) {
    std::string parent = path::parent(target);
    if (parent == target || !::GetDiskFreeSpaceExW(widen(parent).c_str(), &avail, nullptr, nullptr))
      return error_from_win32(::GetLastError());
  }
  out = avail.QuadPart;
  return ok_status();
}

std::string temp_dir() {
  wchar_t buf[MAX_PATH + 1];
  DWORD n = ::GetTempPathW(MAX_PATH, buf);
  if (n == 0) return "C:/Windows/Temp";
  return strip_trailing_separator(narrow(std::wstring(buf, n)));
}

std::string appdata_dir() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = ::GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH * 2);
  if (n == 0 || n >= MAX_PATH * 2) return temp_dir();
  return narrow(std::wstring(buf, n));
}

}  // namespace file

/* ------------------------------------------------------------- directory */

namespace dir {

std::string current() {
  DWORD size = ::GetCurrentDirectoryW(0, nullptr);
  if (size == 0) return std::string();
  std::vector<wchar_t> buf(size + 1);
  DWORD n = ::GetCurrentDirectoryW(size + 1, buf.data());
  if (n == 0) return std::string();
  return path::normalize(narrow(std::wstring(buf.data(), n)));
}

Status set_current(const std::string& p) {
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (!::SetCurrentDirectoryW(widen(p).c_str())) return error_from_win32(::GetLastError());
  return ok_status();
}

}  // namespace dir

/* ------------------------------------------------------------------ env */

namespace env {

Status get(const std::string& name, std::string& out, bool* found) {
  out.clear();
  if (found != nullptr) *found = false;
  if (name.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  std::wstring wname = widen(name);
  DWORD n = ::GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
  if (n == 0) {
    DWORD e = ::GetLastError();
    if (e == ERROR_ENVVAR_NOT_FOUND) return ok_status();
    if (e != ERROR_SUCCESS) return error_from_win32(e);
    return ok_status(); /* empty value */
  }
  std::vector<wchar_t> buf(n);
  DWORD got = ::GetEnvironmentVariableW(wname.c_str(), buf.data(), n);
  if (got == 0 || got >= n) return ok_status();
  out = narrow(std::wstring(buf.data(), got));
  if (found != nullptr) *found = !out.empty();
  return ok_status();
}

Status set(const std::string& name, const std::string& value) {
  if (name.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (!::SetEnvironmentVariableW(widen(name).c_str(), widen(value).c_str()))
    return error_from_win32(::GetLastError());
  return ok_status();
}

Status unset(const std::string& name) {
  if (name.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (!::SetEnvironmentVariableW(widen(name).c_str(), nullptr))
    return error_from_win32(::GetLastError());
  return ok_status();
}

}  // namespace env

/* ----------------------------------------------------------------- time */

namespace time {

i64 now_unix_ms() {
  FILETIME ft{};
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0602
  ::GetSystemTimePreciseAsFileTime(&ft);
#else
  ::GetSystemTimeAsFileTime(&ft);
#endif
  return filetime_to_ms(ft);
}

i64 monotonic_ms() { return static_cast<i64>(::GetTickCount64()); }

i64 perf_counter() {
  LARGE_INTEGER v{};
  if (!::QueryPerformanceCounter(&v)) return 0;
  return static_cast<i64>(v.QuadPart);
}

i64 perf_frequency() {
  LARGE_INTEGER v{};
  if (!::QueryPerformanceFrequency(&v)) return 0;
  return static_cast<i64>(v.QuadPart);
}

void sleep_ms(i64 ms) {
  if (ms <= 0) return;
  ::Sleep(static_cast<DWORD>(ms > 0x7FFFFFFF ? 0x7FFFFFFF : ms));
}

}  // namespace time

/* -------------------------------------------------------------- console */

namespace console {
namespace {

HANDLE handle_of(Stream s) {
  return ::GetStdHandle(s == Stream::Err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
}

bool handle_is_tty(HANDLE h) {
  if (h == nullptr || h == INVALID_HANDLE_VALUE) return false;
  DWORD mode = 0;
  return ::GetConsoleMode(h, &mode) != 0;
}

}  // namespace

bool is_tty(Stream s) { return handle_is_tty(handle_of(s)); }

int width() {
  HANDLE h = handle_of(Stream::Out);
  if (!handle_is_tty(h)) return 80;
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!::GetConsoleScreenBufferInfo(h, &info)) return 80;
  int w = info.srWindow.Right - info.srWindow.Left + 1;
  return w > 0 ? w : 80;
}

i64 write_utf8(Stream s, const std::string& text) {
  if (text.empty()) return 0;
  HANDLE h = handle_of(s);
  if (h == nullptr || h == INVALID_HANDLE_VALUE) return -1;
  if (handle_is_tty(h)) {
    std::wstring w = widen(text);
    std::size_t off = 0;
    while (off < w.size()) {
      DWORD wrote = 0;
      DWORD want = static_cast<DWORD>(std::min<std::size_t>(w.size() - off, 1u << 15));
      if (!::WriteConsoleW(h, w.data() + off, want, &wrote, nullptr)) return -1;
      if (wrote == 0) break;
      off += wrote;
    }
    return static_cast<i64>(text.size());
  }
  std::size_t off = 0;
  while (off < text.size()) {
    DWORD wrote = 0;
    DWORD want = static_cast<DWORD>(std::min<std::size_t>(text.size() - off, 1u << 20));
    if (!::WriteFile(h, text.data() + off, want, &wrote, nullptr)) return -1;
    if (wrote == 0) break;
    off += wrote;
  }
  return static_cast<i64>(off);
}

i64 write_line_utf8(Stream s, const std::string& text) {
  i64 n = write_utf8(s, text);
  if (n < 0) return n;
  i64 m = write_utf8(s, "\n");
  return m < 0 ? m : n + m;
}

bool enable_utf8() {
  bool ok = ::SetConsoleOutputCP(CP_UTF8) != 0;
  if (!::SetConsoleCP(CP_UTF8)) ok = false;
  return ok;
}

}  // namespace console

/* -------------------------------------------------------------- process */

namespace process {

int id() { return static_cast<int>(::GetCurrentProcessId()); }

std::string executable_path() {
  std::vector<wchar_t> buf(MAX_PATH * 2);
  while (true) {
    DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return std::string();
    if (n < buf.size()) return narrow(std::wstring(buf.data(), n));
    if (buf.size() > (1u << 20)) return std::string();
    buf.resize(buf.size() * 2);
  }
}

}  // namespace process

}  // namespace platform
}  // namespace vd

#endif  /* VD_PLATFORM_WINDOWS */
