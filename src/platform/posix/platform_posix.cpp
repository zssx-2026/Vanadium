/* POSIX backend for include/vanadium/platform.h (Linux, macOS).
   Compiled only when VD_PLATFORM_POSIX is defined; see docs/PORTABILITY.md. */
#include "vanadium/platform.h"

#if defined(VD_PLATFORM_POSIX)

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(VD_PLATFORM_LINUX)
#include <sys/ioctl.h>
#endif
#if defined(VD_PLATFORM_MACOS)
#include <mach-o/dyld.h>
#include <sys/ioctl.h>
#endif

namespace vd {
namespace platform {
namespace {

constexpr i64 kMtimeUnavailable = 0;

int sys_error() { return errno; }

/* stat with the platform's timestamp member name normalised. */
bool stat_mtime_ms(const struct stat& st, i64& out) {
#if defined(VD_PLATFORM_MACOS)
  const struct timespec& ts = st.st_mtimespec;
#else
  const struct timespec& ts = st.st_mtim;
#endif
  if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
    out = kMtimeUnavailable;
    return false;
  }
  out = static_cast<i64>(ts.tv_sec) * 1000 + static_cast<i64>(ts.tv_nsec) / 1000000;
  return true;
}

std::string strip_trailing_separator(std::string s) {
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  if (s == "/") s.clear();
  return s;
}

/* Write the whole buffer, retrying on EINTR and short writes. */
bool write_all(int fd, const char* data, std::size_t size, int& saved_errno) {
  std::size_t off = 0;
  while (off < size) {
    ssize_t n = ::write(fd, data + off, size - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      saved_errno = errno;
      return false;
    }
    if (n == 0) {
      saved_errno = EIO;
      return false;
    }
    off += static_cast<std::size_t>(n);
  }
  return true;
}

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
  switch (native) {
    case 0: s.code = ErrCode::Ok; return s;
    case ENOENT:
    case ENOTDIR: s.code = (native == ENOTDIR) ? ErrCode::NotADirectory : ErrCode::NotFound; return s;
    case EACCES:
    case EPERM: s.code = ErrCode::Denied; return s;
    case EEXIST: s.code = ErrCode::Exists; return s;
    case EISDIR: s.code = ErrCode::IsADirectory; return s;
    case EBUSY:
    case ETXTBSY: s.code = ErrCode::Busy; return s;
    case ENOSPC:
    case EDQUOT: s.code = ErrCode::NoSpace; return s;
    case EINVAL: s.code = ErrCode::InvalidName; return s;
    case ENAMETOOLONG: s.code = ErrCode::NameTooLong; return s;
    case ENOTSUP: s.code = ErrCode::NotSupported; return s;
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP: s.code = ErrCode::NotSupported; return s;
#endif
    case EIO: s.code = ErrCode::Io; return s;
    default: s.code = ErrCode::Unknown; return s;
  }
}

std::string status_text(const Status& status) {
  std::string out = error_name(status.code);
  out += "(";
  out += std::to_string(status.native);
  out += ")";
  if (status.native != 0) {
    const char* text = std::strerror(status.native);
    if (text != nullptr && text[0] != '\0') {
      out += ": ";
      out += text;
    }
  }
  return out;
}

const char* os_name() {
#if defined(VD_PLATFORM_MACOS)
  return "macos";
#else
  return "linux";
#endif
}

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

bool is_absolute(const std::string& p) { return !p.empty() && p[0] == '/'; }

std::string join(const std::string& base, const std::string& leaf) {
  if (base.empty()) return leaf;
  if (leaf.empty()) return base;
  if (is_absolute(leaf)) return leaf;
  if (base.back() == '/') return base + leaf;
  return base + "/" + leaf;
}

std::string normalize(const std::string& p) {
  if (p.empty()) return ".";
  const bool rooted = p[0] == '/';
  std::vector<std::string> parts;
  std::size_t i = 0;
  while (i <= p.size()) {
    std::size_t next = p.find('/', i);
    if (next == std::string::npos) next = p.size();
    std::string seg = p.substr(i, next - i);
    if (seg == "..") {
      if (!parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else if (!rooted) {
        parts.push_back("..");
      }
    } else if (!seg.empty() && seg != ".") {
      parts.push_back(seg);
    }
    if (next == p.size()) break;
    i = next + 1;
  }
  std::string out;
  if (rooted) out += "/";
  for (std::size_t k = 0; k < parts.size(); ++k) {
    if (k) out += "/";
    out += parts[k];
  }
  if (out.empty()) out = rooted ? "/" : ".";
  return out;
}

std::string parent(const std::string& p) {
  std::string s = normalize(p);
  std::size_t pos = s.find_last_of('/');
  if (pos == std::string::npos) return ".";
  if (pos == 0) return "/";
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
  struct stat st {};
  if (::lstat(p.c_str(), &st) != 0) {
    int e = sys_error();
    if (e == ENOENT || e == ENOTDIR) return ok_status(); /* exists == false */
    return status_from_native(e);
  }
  out.exists = true;
  out.is_symlink = S_ISLNK(st.st_mode) != 0;
  struct stat target {};
  const struct stat* use = &st;
  if (out.is_symlink && ::stat(p.c_str(), &target) == 0) use = &target;
  out.is_dir = S_ISDIR(use->st_mode) != 0;
  out.size = out.is_dir ? 0 : static_cast<i64>(use->st_size);
  out.read_only = (use->st_mode & S_IWUSR) == 0;
  i64 mt = 0;
  if (stat_mtime_ms(*use, mt)) out.mtime_ms = mt;
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
  std::size_t i = s[0] == '/' ? 1 : 0;
  while (true) {
    std::size_t next = s.find('/', i);
    std::string cur = (next == std::string::npos) ? s : s.substr(0, next);
    if (!cur.empty()) {
      if (::mkdir(cur.c_str(), 0777) != 0) {
        int e = sys_error();
        if (e != EEXIST) {
          struct stat st {};
          if (!(::stat(cur.c_str(), &st) == 0 && S_ISDIR(st.st_mode))) {
            return status_from_native(e);
          }
        }
      }
    }
    if (next == std::string::npos) break;
    i = next + 1;
  }
  return ok_status();
}

Status list_entries(const std::string& p, std::vector<DirEntry>& entries) {
  entries.clear();
  DIR* d = ::opendir(p.empty() ? "." : p.c_str());
  if (d == nullptr) return status_from_native(sys_error());
  while (struct dirent* ent = ::readdir(d)) {
    const char* name = ent->d_name;
    if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
    DirEntry e;
    e.name = name;
    /* d_type is unavailable on some filesystems (DT_UNKNOWN); fall back to lstat. */
    if (ent->d_type == DT_DIR) {
      e.is_dir = true;
    } else if (ent->d_type == DT_UNKNOWN) {
      struct stat st {};
      if (::lstat((path::join(p, e.name)).c_str(), &st) == 0) e.is_dir = S_ISDIR(st.st_mode) != 0;
    }
    entries.push_back(std::move(e));
  }
  ::closedir(d);
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
  if (::unlink(p.c_str()) == 0) return ok_status();
  int e = sys_error();
  if (e == ENOENT || e == ENOTDIR) return ok_status();
  if (e == EISDIR || e == EPERM) {
    /* Some platforms report a directory as EPERM from unlink. */
    if (is_dir(p)) {
      Status s;
      s.code = ErrCode::IsADirectory;
      s.native = e;
      return s;
    }
  }
  return status_from_native(e);
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
    if (::rmdir(p.c_str()) != 0) {
      int e = sys_error();
      if (e == ENOENT) return ok_status();
      return status_from_native(e);
    }
    return ok_status();
  }
  return remove_file(p);
}

Status rename(const std::string& from, const std::string& to, bool replace_existing) {
  if (from.empty() || to.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (!replace_existing && exists(to)) {
    Status s;
    s.code = ErrCode::Exists;
    return s;
  }
  if (::rename(from.c_str(), to.c_str()) == 0) return ok_status();
  return status_from_native(sys_error());
}

Status copy_file(const std::string& from, const std::string& to, bool replace_existing) {
  if (from.empty() || to.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  int in = ::open(from.c_str(), O_RDONLY);
  if (in < 0) return status_from_native(sys_error());
  int flags = O_WRONLY | O_CREAT | (replace_existing ? O_TRUNC : O_EXCL);
  int out = ::open(to.c_str(), flags, 0666);
  if (out < 0) {
    int e = sys_error();
    ::close(in);
    return status_from_native(e);
  }
  Status result = ok_status();
  std::vector<char> buf(1 << 16);
  while (true) {
    ssize_t got = ::read(in, buf.data(), buf.size());
    if (got < 0) {
      if (errno == EINTR) continue;
      result = status_from_native(errno);
      break;
    }
    if (got == 0) break;
    int saved = 0;
    if (!write_all(out, buf.data(), static_cast<std::size_t>(got), saved)) {
      result = status_from_native(saved);
      break;
    }
  }
  ::close(in);
  if (::close(out) != 0 && result.ok()) result = status_from_native(sys_error());
  if (!result.ok()) ::unlink(to.c_str());
  return result;
}

Status read_file_opt(const std::string& p, std::string& out) {
  out.clear();
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  int fd = ::open(p.c_str(), O_RDONLY);
  if (fd < 0) {
    int e = sys_error();
    if (e == ENOENT || e == ENOTDIR) return ok_status(); /* missing is not a failure */
    return status_from_native(e);
  }
  struct stat st {};
  if (::fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
    ::close(fd);
    Status s;
    s.code = ErrCode::IsADirectory;
    return s;
  }
  if (st.st_size > 0) out.reserve(static_cast<std::size_t>(st.st_size));
  char buf[1 << 16];
  while (true) {
    ssize_t got = ::read(fd, buf, sizeof(buf));
    if (got < 0) {
      if (errno == EINTR) continue;
      Status s = status_from_native(errno);
      ::close(fd);
      out.clear();
      return s;
    }
    if (got == 0) break;
    out.append(buf, static_cast<std::size_t>(got));
  }
  ::close(fd);
  return ok_status();
}

Status read_file(const std::string& p, std::string& out) {
  Status s = read_file_opt(p, out);
  if (!s.ok()) return s;
  if (out.empty() && !exists(p)) {
    Status missing;
    missing.code = ErrCode::NotFound;
    return missing;
  }
  return ok_status();
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
  int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) return status_from_native(sys_error());
  int saved = 0;
  bool ok = write_all(fd, data.data(), data.size(), saved);
  if (::close(fd) != 0 && ok) {
    saved = sys_error();
    ok = false;
  }
  if (!ok) return status_from_native(saved);
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
  s = rename(tmp, p, true);
  if (!s.ok()) remove_file(tmp);
  return s;
}

Status free_space(const std::string& p, u64& out) {
  out = 0;
  struct statvfs vfs {};
  std::string target = p.empty() ? std::string(".") : p;
  if (::statvfs(target.c_str(), &vfs) != 0) {
    /* A path inside a not-yet-created directory: ask about its parent. */
    std::string parent = path::parent(target);
    if (parent != target && ::statvfs(parent.c_str(), &vfs) == 0) {
      /* fall through */
    } else {
      return status_from_native(sys_error());
    }
  }
  out = static_cast<u64>(vfs.f_bavail) * static_cast<u64>(vfs.f_frsize);
  return ok_status();
}

std::string temp_dir() {
  std::string out;
  bool found = false;
  if (env::get("TMPDIR", out, &found).ok() && found && !out.empty()) return strip_trailing_separator(out);
  return "/tmp";
}

std::string appdata_dir() {
  std::string out;
  bool found = false;
#if defined(VD_PLATFORM_MACOS)
  if (env::get("HOME", out, &found).ok() && found && !out.empty()) {
    return path::join(out, "Library/Application Support");
  }
#else
  if (env::get("XDG_CONFIG_HOME", out, &found).ok() && found && !out.empty()) {
    return strip_trailing_separator(out);
  }
  if (env::get("HOME", out, &found).ok() && found && !out.empty()) {
    return path::join(out, ".config");
  }
#endif
  return temp_dir();
}

}  // namespace file

/* ------------------------------------------------------------- directory */

namespace dir {

std::string current() {
  std::vector<char> buf(1024);
  while (true) {
    if (::getcwd(buf.data(), buf.size()) != nullptr) return std::string(buf.data());
    if (errno != ERANGE) return std::string();
    buf.resize(buf.size() * 2);
    if (buf.size() > (1u << 20)) return std::string();
  }
}

Status set_current(const std::string& p) {
  if (p.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (::chdir(p.c_str()) != 0) return status_from_native(sys_error());
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
  const char* raw = std::getenv(name.c_str());
  if (raw == nullptr || raw[0] == '\0') return ok_status();
  out = raw;
  if (found != nullptr) *found = true;
  return ok_status();
}

Status set(const std::string& name, const std::string& value) {
  if (name.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (::setenv(name.c_str(), value.c_str(), 1) != 0) return status_from_native(sys_error());
  return ok_status();
}

Status unset(const std::string& name) {
  if (name.empty()) {
    Status s;
    s.code = ErrCode::InvalidName;
    return s;
  }
  if (::unsetenv(name.c_str()) != 0) return status_from_native(sys_error());
  return ok_status();
}

}  // namespace env

/* ----------------------------------------------------------------- time */

namespace time {

i64 now_unix_ms() {
  struct timespec ts {};
  if (::clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
  return static_cast<i64>(ts.tv_sec) * 1000 + static_cast<i64>(ts.tv_nsec) / 1000000;
}

i64 monotonic_ms() {
  struct timespec ts {};
  if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return static_cast<i64>(ts.tv_sec) * 1000 + static_cast<i64>(ts.tv_nsec) / 1000000;
}

i64 perf_counter() {
  struct timespec ts {};
  if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return static_cast<i64>(ts.tv_sec) * 1000000000LL + static_cast<i64>(ts.tv_nsec);
}

i64 perf_frequency() { return 1000000000LL; }

void sleep_ms(i64 ms) {
  if (ms <= 0) return;
  struct timespec req {};
  req.tv_sec = static_cast<time_t>(ms / 1000);
  req.tv_nsec = static_cast<long>((ms % 1000) * 1000000);
  struct timespec rem {};
  while (::nanosleep(&req, &rem) != 0) {
    if (errno != EINTR) return;
    req = rem;
  }
}

}  // namespace time

/* -------------------------------------------------------------- console */

namespace console {
namespace {

int fd_of(Stream s) { return s == Stream::Err ? STDERR_FILENO : STDOUT_FILENO; }

}  // namespace

bool is_tty(Stream s) { return ::isatty(fd_of(s)) == 1; }

int width() {
  struct winsize ws {};
  if (::ioctl(fd_of(Stream::Out), TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
  return 80;
}

i64 write_utf8(Stream s, const std::string& text) {
  const int fd = fd_of(s);
  std::size_t off = 0;
  while (off < text.size()) {
    ssize_t n = ::write(fd, text.data() + off, text.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) break;
    off += static_cast<std::size_t>(n);
  }
  return static_cast<i64>(off);
}

i64 write_line_utf8(Stream s, const std::string& text) {
  i64 n = write_utf8(s, text);
  if (n < 0) return n;
  i64 m = write_utf8(s, "\n");
  return m < 0 ? m : n + m;
}

bool enable_utf8() { return true; /* POSIX terminals are byte streams */ }

}  // namespace console

/* -------------------------------------------------------------- process */

namespace process {

int id() { return static_cast<int>(::getpid()); }

std::string executable_path() {
#if defined(VD_PLATFORM_MACOS)
  std::uint32_t size = 1024;
  std::vector<char> buf(size);
  while (true) {
    std::uint32_t need = size;
    if (_NSGetExecutablePath(buf.data(), &need) == 0) return std::string(buf.data());
    if (need <= size) return std::string();
    size = need;
    buf.resize(size);
    if (size > (1u << 20)) return std::string();
  }
#else
  std::vector<char> buf(1024);
  while (true) {
    ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
    if (n < 0) return std::string();
    if (static_cast<std::size_t>(n) < buf.size()) return std::string(buf.data(), static_cast<std::size_t>(n));
    buf.resize(buf.size() * 2);
    if (buf.size() > (1u << 20)) return std::string();
  }
#endif
}

}  // namespace process

}  // namespace platform
}  // namespace vd

#endif  /* VD_PLATFORM_POSIX */
