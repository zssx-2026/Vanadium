// Process execution for git delegation.
//
// Rules that this file exists to enforce:
//   * argv array only - never a shell, never string concatenation, so a path
//     or URL containing spaces/quotes/&/| cannot turn into a command.
//   * stdout/stderr go to temporary files, not pipes: a child that writes more
//     than the pipe buffer while we read the other stream cannot deadlock us.
//   * the child never inherits our console, so no window flashes and no key
//     press can ever be requested from an unattended run.

#include "vanadium/gitcompat.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace vd::gitcompat {
namespace {

bool has_separator(const std::string& p) {
  return p.find('/') != std::string::npos || p.find('\\') != std::string::npos;
}

#ifdef _WIN32

std::wstring widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return std::wstring();
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                    nullptr, nullptr);
  if (n <= 0) return std::string();
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
  return s;
}

// CommandLineToArgvW-compatible quoting: backslashes are only special when
// they precede a quote or the terminating quote.
std::wstring quote_arg(const std::wstring& a) {
  if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
  std::wstring out;
  out.push_back(L'"');
  for (std::size_t i = 0; i < a.size();) {
    std::size_t slashes = 0;
    while (i < a.size() && a[i] == L'\\') {
      ++i;
      ++slashes;
    }
    if (i == a.size()) {
      out.append(slashes * 2, L'\\');
      break;
    }
    if (a[i] == L'"') {
      out.append(slashes * 2 + 1, L'\\');
      out.push_back(L'"');
      ++i;
    } else {
      out.append(slashes, L'\\');
      out.push_back(a[i]);
      ++i;
    }
  }
  out.push_back(L'"');
  return out;
}

std::vector<std::wstring> split_path_env(const std::wstring& path) {
  std::vector<std::wstring> dirs;
  std::wstring cur;
  for (wchar_t c : path) {
    if (c == L';') {
      if (!cur.empty()) dirs.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) dirs.push_back(cur);
  return dirs;
}

std::string find_on_path(const std::string& name) {
  const std::wstring wname = widen(name);
  const bool has_ext = wname.size() > 4 &&
                       (wname.compare(wname.size() - 4, 4, L".exe") == 0 ||
                        wname.compare(wname.size() - 4, 4, L".cmd") == 0 ||
                        wname.compare(wname.size() - 4, 4, L".bat") == 0);
  wchar_t buf[32768];
  const DWORD n = GetEnvironmentVariableW(L"PATH", buf, static_cast<DWORD>(std::size(buf)));
  std::vector<std::wstring> dirs;
  if (n > 0 && n < std::size(buf)) dirs = split_path_env(std::wstring(buf, n));
  for (const auto& dir : dirs) {
    std::vector<std::wstring> cands;
    cands.push_back(dir + L"\\" + wname + (has_ext ? L"" : L".exe"));
    if (!has_ext) cands.push_back(dir + L"\\" + wname);
    for (const auto& c : cands) {
      const DWORD attr = GetFileAttributesW(c.c_str());
      if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return narrow(c);
      }
    }
  }
  return std::string();
}

std::vector<std::wstring> current_env() {
  std::vector<std::wstring> out;
  LPWCH block = GetEnvironmentStringsW();
  if (!block) return out;
  for (LPWCH p = block; *p;) {
    std::wstring entry(p);
    out.push_back(entry);
    p += entry.size() + 1;
  }
  FreeEnvironmentStringsW(block);
  return out;
}

std::wstring build_env_block(const std::vector<std::string>& extra) {
  std::vector<std::wstring> entries = current_env();
  for (const auto& kv : extra) {
    const std::size_t eq = kv.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    const std::wstring key = widen(kv.substr(0, eq));
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const std::wstring& e) {
                                   if (e.empty() || e[0] == L'=') return false;
                                   const std::size_t p = e.find(L'=');
                                   if (p == std::wstring::npos) return false;
                                   return _wcsicmp(e.substr(0, p).c_str(), key.c_str()) == 0;
                                 }),
                  entries.end());
    entries.push_back(widen(kv));
  }
  std::wstring block;
  for (const auto& e : entries) {
    block += e;
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

#endif  // _WIN32

}  // namespace

std::string ProcResult::diagnose() const {
  if (!started) return start_error.empty() ? "could not start process" : start_error;
  std::string text = trim(err);
  if (text.empty()) text = trim(out);
  if (text.empty()) return "exit code " + std::to_string(exit_code);
  // Keep at most four lines; git is usually verbose right before the failure.
  std::vector<std::string> lines;
  std::size_t pos = 0;
  while (pos < text.size() && lines.size() < 4) {
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    std::string line = trim(text.substr(pos, nl - pos));
    pos = nl + 1;
    if (!line.empty()) lines.push_back(line);
  }
  std::string out2;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i) out2 += " | ";
    out2 += lines[i];
  }
  return out2 + " (exit " + std::to_string(exit_code) + ")";
}

ProcResult run_capture(const std::vector<std::string>& argv,
                       const std::string& cwd,
                       const std::vector<std::string>& env_extra,
                       int timeout_ms,
                       const std::string& stdin_path) {
  ProcResult r;
  if (argv.empty()) {
    r.start_error = "empty argv";
    return r;
  }

#ifdef _WIN32
  std::string exe = argv[0];
  if (!has_separator(exe)) {
    const std::string found = find_on_path(exe);
    if (!found.empty()) exe = found;
  }

  std::wstring cmd;
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (i) cmd.push_back(L' ');
    cmd += quote_arg(widen(argv[i]));
  }

  const std::string stem = fs::join(fs::temp_dir(), "inv-git-" + random_hex(8));
  const std::string out_path = stem + ".out";
  const std::string err_path = stem + ".err";

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE h_out = CreateFileW(widen(out_path).c_str(), GENERIC_WRITE | GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_TEMPORARY, nullptr);
  HANDLE h_err = CreateFileW(widen(err_path).c_str(), GENERIC_WRITE | GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_TEMPORARY, nullptr);
  HANDLE h_in = INVALID_HANDLE_VALUE;
  if (!stdin_path.empty()) {
    h_in = CreateFileW(widen(stdin_path).c_str(), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
  } else {
    h_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  }

  if (h_out == INVALID_HANDLE_VALUE || h_err == INVALID_HANDLE_VALUE ||
      h_in == INVALID_HANDLE_VALUE) {
    if (h_out != INVALID_HANDLE_VALUE) CloseHandle(h_out);
    if (h_err != INVALID_HANDLE_VALUE) CloseHandle(h_err);
    if (h_in != INVALID_HANDLE_VALUE) CloseHandle(h_in);
    r.start_error = "cannot create capture files under " + fs::temp_dir();
    return r;
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = h_in;
  si.hStdOutput = h_out;
  si.hStdError = h_err;

  PROCESS_INFORMATION pi{};
  const std::wstring wcmd = cmd;
  const std::wstring wexe = widen(exe);
  const std::wstring wcwd = cwd.empty() ? std::wstring() : widen(cwd);
  std::wstring env_block = build_env_block(env_extra);
  std::vector<wchar_t> cmd_buf(wcmd.begin(), wcmd.end());
  cmd_buf.push_back(L'\0');

  const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
  const BOOL started = CreateProcessW(wexe.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE, flags,
                                      env_block.empty() ? nullptr : env_block.data(),
                                      wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
  CloseHandle(h_out);
  CloseHandle(h_err);
  CloseHandle(h_in);

  if (!started) {
    const DWORD e = GetLastError();
    r.start_error = "cannot start \"" + exe + "\" (win32 error " + std::to_string(e) + ")";
    fs::remove_file(out_path);
    fs::remove_file(err_path);
    return r;
  }

  CloseHandle(pi.hThread);
  const DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms > 0 ? static_cast<DWORD>(timeout_ms)
                                                                    : INFINITE);
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    r.started = true;
    r.exit_code = -2;
    r.err = "timed out after " + std::to_string(timeout_ms) + " ms";
  } else {
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.started = true;
    r.exit_code = static_cast<int>(code);
  }
  CloseHandle(pi.hProcess);

  std::string body;
  if (fs::read_file_opt(out_path, body)) r.out = body;
  body.clear();
  if (fs::read_file_opt(err_path, body)) r.err = (r.err.empty() ? body : r.err + "\n" + body);
  fs::remove_file(out_path);
  fs::remove_file(err_path);
  return r;
#else
  (void)cwd;
  (void)env_extra;
  (void)timeout_ms;
  (void)stdin_path;
  r.start_error = "process delegation requires Windows in this build";
  return r;
#endif
}

std::string git_path() {
  static std::string cached;
  static bool resolved = false;
  if (resolved) return cached;
  resolved = true;
#ifdef _WIN32
  cached = find_on_path("git");
  if (cached.empty()) {
    wchar_t buf[32768];
    const wchar_t* kCandidates[] = {L"ProgramFiles", L"ProgramW6432", L"LOCALAPPDATA"};
    for (const wchar_t* var : kCandidates) {
      const DWORD n = GetEnvironmentVariableW(var, buf, static_cast<DWORD>(std::size(buf)));
      if (n == 0 || n >= std::size(buf)) continue;
      std::wstring base(buf, n);
      const wchar_t* tails[] = {L"\\Git\\cmd\\git.exe", L"\\Programs\\Git\\cmd\\git.exe"};
      for (const wchar_t* tail : tails) {
        const std::wstring cand = base + tail;
        const DWORD attr = GetFileAttributesW(cand.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
          cached = narrow(cand);
          return cached;
        }
      }
    }
  }
#endif
  return cached;
}

bool git_available() { return !git_path().empty(); }

std::string git_version() {
  static std::string cached;
  static bool resolved = false;
  if (resolved) return cached;
  resolved = true;
  if (!git_available()) return cached;
  ProcResult r = run_capture({git_path(), "--version"}, ".", {}, 10000);
  if (r.ok()) cached = trim(r.out);
  return cached;
}

ProcResult git_run(const std::vector<std::string>& args,
                   const std::string& cwd,
                   int timeout_ms,
                   const std::string& stdin_path) {
  ProcResult r;
  if (!git_available()) {
    r.start_error = "git: not found";
    return r;
  }
  std::vector<std::string> argv;
  argv.reserve(args.size() + 1);
  argv.push_back(git_path());
  argv.insert(argv.end(), args.begin(), args.end());
  // Never let git block on a credential prompt: an unattended run must fail
  // fast instead of waiting forever, and no token may be echoed.
  const std::vector<std::string> env = {
      "GIT_TERMINAL_PROMPT=0",
      "GCM_INTERACTIVE=never",
      "GIT_PAGER=cat",
      "GIT_EDITOR=true",
      "GIT_FLUSH=1",
  };
  return run_capture(argv, cwd.empty() ? "." : cwd, env, timeout_ms, stdin_path);
}

}  // namespace vd::gitcompat
