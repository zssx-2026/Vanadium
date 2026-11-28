/* Standalone self-test for the platform layer.
   Deliberately links only src/platform/*: it must build and run on a machine
   where src/core does not compile yet (that is the whole point of the port).
   Exit code 0 = every check passed, 1 = at least one failure. */
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "vanadium/platform.h"

namespace pf = vd::platform;
using pf::ErrCode;
using pf::Status;

namespace {

int g_failed = 0;
int g_passed = 0;

void check(bool ok, const char* name, const std::string& detail = std::string()) {
  if (ok) {
    ++g_passed;
    std::printf("[pass] %s\n", name);
  } else {
    ++g_failed;
    std::printf("[fail] %s%s%s\n", name, detail.empty() ? "" : ": ", detail.c_str());
  }
}

std::string describe(const Status& s) { return pf::status_text(s); }

void check_status(const Status& s, const char* name) {
  check(s.ok(), name, describe(s));
}

std::string uniq_suffix() {
  static int counter = 0;
  ++counter;
  return std::to_string(pf::process::id()) + "-" + std::to_string(counter) + "-" +
         std::to_string(pf::time::monotonic_ms() % 100000);
}

/* Every test lives under one scratch directory so a crash leaves one obvious
   tree to inspect. */
struct Scratch {
  std::string root;

  bool create() {
    root = pf::path::join(pf::file::temp_dir(), "vd_platform_selftest_" + uniq_suffix());
    Status s = pf::file::mkdirs(root);
    if (!s.ok()) {
      std::printf("[fail] scratch mkdirs %s: %s\n", root.c_str(), describe(s).c_str());
      ++g_failed;
      return false;
    }
    return true;
  }
  std::string at(const std::string& rel) const { return pf::path::join(root, rel); }
  ~Scratch() {
    if (!root.empty()) pf::file::remove_all(root);
  }
};

void test_identity() {
  std::printf("platform: %s / %s\n", pf::os_name(), pf::arch_name());
  check(pf::os_name() != nullptr && pf::os_name()[0] != '\0', "os_name");
  check(pf::arch_name() != nullptr && pf::arch_name()[0] != '\0', "arch_name");
  check(pf::path::separator() == '/', "separator is '/'");
}

void test_path() {
  check(pf::path::join("a", "b") == "a/b", "join a+b");
  check(pf::path::join("a/", "b") == "a/b", "join a/+b");
  check(pf::path::join("", "b") == "b", "join empty+b");
  check(pf::path::join("a", "") == "a", "join a+empty");
  check(pf::path::join("a", "/abs") == "/abs", "join absolute leaf wins");

  check(pf::path::normalize("a/./b//c") == "a/b/c", "normalize dots and dupes");
  check(pf::path::normalize("a/b/../c") == "a/c", "normalize ..");
  check(pf::path::normalize("") == ".", "normalize empty");
  check(pf::path::normalize("/") == "/", "normalize root");

  check(pf::path::parent("a/b/c") == "a/b", "parent a/b/c");
  check(pf::path::parent("a") == ".", "parent a");
  check(pf::path::parent("/a") == "/", "parent /a");
  check(pf::path::filename("a/b/c.txt") == "c.txt", "filename");
  check(pf::path::stem("a/b/c.tar.gz") == "c.tar", "stem");
  check(pf::path::ext("a/b/c.tar.gz") == ".gz", "ext");
  check(pf::path::ext("a/b/.hidden") == "", "ext hidden file");
  check(pf::path::stem(".hidden") == ".hidden", "stem hidden file");

  check(pf::path::is_absolute("/x"), "is_absolute /x");
  check(!pf::path::is_absolute("x/y"), "is_absolute relative");
#if defined(VD_PLATFORM_WINDOWS)
  check(pf::path::is_absolute("C:/x"), "is_absolute C:/x");
  check(pf::path::normalize("C:\\x\\y") == "C:/x/y", "normalize windows separators");
  check(pf::path::normalize("C:/x/../y") == "C:/y", "normalize keeps drive prefix");
#endif
}

void test_dirs_and_files(Scratch& s) {
  const std::string nested = s.at("a/b/c");
  check_status(pf::file::mkdirs(nested), "mkdirs recursive");
  check(pf::file::is_dir(nested), "is_dir after mkdirs");
  check_status(pf::file::mkdirs(nested), "mkdirs is idempotent");

  const std::string p = s.at("a/b/c/hello.txt");
  const std::string payload = "hello \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80\r\nline2\n";
  check_status(pf::file::write_file(p, payload), "write_file creates parents");
  check(pf::file::exists(p), "exists after write");
  check(pf::file::is_file(p), "is_file after write");
  check(pf::file::size(p) == static_cast<vd::platform::i64>(payload.size()), "size matches",
        std::to_string(pf::file::size(p)));

  std::string back;
  check_status(pf::file::read_file(p, back), "read_file");
  check(back == payload, "read_file roundtrip bytes");

  pf::Attrs attrs;
  check_status(pf::file::attrs(p, attrs), "attrs file");
  check(attrs.exists && !attrs.is_dir, "attrs flags");
  check(attrs.size == static_cast<vd::platform::i64>(payload.size()), "attrs size");
  check(attrs.mtime_ms > 0, "attrs mtime_ms", std::to_string(attrs.mtime_ms));

  const std::string missing = s.at("does/not/exist.bin");
  std::string out = "stale";
  Status opt = pf::file::read_file_opt(missing, out);
  check(opt.ok() && out.empty(), "read_file_opt missing -> ok + empty", describe(opt));
  Status hard = pf::file::read_file(missing, out);
  check(hard.code == ErrCode::NotFound, "read_file missing -> NotFound", describe(hard));

  pf::Attrs missing_attrs;
  Status ma = pf::file::attrs(missing, missing_attrs);
  check(ma.ok() && !missing_attrs.exists, "attrs missing -> ok + !exists", describe(ma));
  check(pf::file::size(missing) == -1, "size missing -> -1");
  check(pf::file::mtime_ms(missing) == 0, "mtime_ms missing -> 0");

  /* Unicode names: the whole point of the UTF-8 path contract. */
  const std::string uni = s.at("a/\xE4\xB8\xAD\xE6\x96\x87-\xD1\x82\xD0\xB5\xD1\x81\xD1\x82-\xF0\x9F\x98\x80.txt");
  check_status(pf::file::write_file(uni, "unicode"), "write unicode filename");
  check(pf::file::exists(uni), "exists unicode filename");
  std::string uni_back;
  check(pf::file::read_file(uni, uni_back).ok() && uni_back == "unicode", "read unicode filename");

  check_status(pf::file::copy_file(p, s.at("copy.txt")), "copy_file");
  std::string copied;
  check(pf::file::read_file(s.at("copy.txt"), copied).ok() && copied == payload, "copy_file bytes");
  Status exists_copy = pf::file::copy_file(p, s.at("copy.txt"), false);
  check(exists_copy.code == ErrCode::Exists, "copy_file without replace -> Exists",
        describe(exists_copy));

  check_status(pf::file::write_file_atomic(s.at("atomic.txt"), "atomic-1"), "write_file_atomic 1");
  check_status(pf::file::write_file_atomic(s.at("atomic.txt"), "atomic-2"), "write_file_atomic 2");
  std::string atomic;
  check(pf::file::read_file(s.at("atomic.txt"), atomic).ok() && atomic == "atomic-2",
        "write_file_atomic replaced");

  check_status(pf::file::rename(s.at("copy.txt"), s.at("renamed.txt")), "rename");
  check(!pf::file::exists(s.at("copy.txt")) && pf::file::exists(s.at("renamed.txt")),
        "rename moved the file");
  check_status(pf::file::write_file(s.at("target.txt"), "old"), "write rename target");
  check_status(pf::file::rename(s.at("renamed.txt"), s.at("target.txt"), true),
               "rename replace existing");
  std::string replaced;
  check(pf::file::read_file(s.at("target.txt"), replaced).ok() && replaced == payload,
        "rename replace bytes");

  check_status(pf::file::remove_file(s.at("target.txt")), "remove_file");
  check(!pf::file::exists(s.at("target.txt")), "remove_file removed");
  check_status(pf::file::remove_file(s.at("target.txt")), "remove_file idempotent");
}

void test_listing(Scratch& s) {
  const std::string dir = s.at("listing");
  check_status(pf::file::mkdirs(dir), "listing mkdirs");
  check_status(pf::file::write_file(pf::path::join(dir, "b.txt"), "b"), "listing b");
  check_status(pf::file::write_file(pf::path::join(dir, "a.txt"), "a"), "listing a");
  check_status(pf::file::mkdirs(pf::path::join(dir, "sub")), "listing sub dir");

  std::vector<std::string> names;
  check_status(pf::file::list_dir(dir, names), "list_dir");
  check(names.size() == 3, "list_dir count", std::to_string(names.size()));
  check(names.size() == 3 && names[0] == "a.txt" && names[1] == "b.txt" && names[2] == "sub",
        "list_dir sorted, no . or ..");

  std::vector<pf::DirEntry> entries;
  check_status(pf::file::list_entries(dir, entries), "list_entries");
  bool sub_is_dir = false;
  for (const auto& e : entries)
    if (e.name == "sub" && e.is_dir) sub_is_dir = true;
  check(sub_is_dir, "list_entries marks directory");

  std::vector<std::string> missing;
  Status ms = pf::file::list_dir(s.at("nope-dir"), missing);
  check(ms.ok() && missing.empty(), "list_dir missing -> empty (windows contract)", describe(ms));
}

void test_free_space_and_temp(Scratch& s) {
  const std::string tmp = pf::file::temp_dir();
  check(!tmp.empty(), "temp_dir non-empty", tmp);
  check(pf::file::is_dir(tmp), "temp_dir exists", tmp);
  check(tmp.back() != '/' && tmp.back() != '\\', "temp_dir has no trailing separator", tmp);

  const std::string appdata = pf::file::appdata_dir();
  check(!appdata.empty(), "appdata_dir non-empty", appdata);

  vd::platform::u64 free_bytes = 0;
  Status fs = pf::file::free_space(s.root, free_bytes);
  check(fs.ok() && free_bytes > 0, "free_space > 0", describe(fs) + " bytes=" + std::to_string(free_bytes));
}

void test_cwd(Scratch& s) {
  const std::string here = pf::dir::current();
  check(!here.empty(), "dir::current non-empty", here);
  check(pf::path::is_absolute(here), "dir::current absolute", here);

  const std::string target = s.at("cwd");
  check_status(pf::file::mkdirs(target), "cwd target mkdirs");
  check_status(pf::dir::set_current(target), "set_current");
  const std::string moved = pf::dir::current();
  check(!moved.empty(), "cwd after chdir", moved);
  check(pf::path::normalize(moved) == pf::path::normalize(target), "cwd equals target",
        moved + " vs " + target);
  check_status(pf::dir::set_current(here), "set_current back");
}

void test_env() {
  std::string path_value;
  bool found = false;
  check_status(pf::env::get("PATH", path_value, &found), "env get PATH");
  check(found && !path_value.empty(), "env PATH found");

  std::string absent;
  bool absent_found = true;
  check_status(pf::env::get("VD_PLATFORM_SELFTEST_UNSET_VAR", absent, &absent_found),
               "env get unset");
  check(!absent_found, "env unset reports found=false");

  check_status(pf::env::set("VD_PLATFORM_SELFTEST_VAR", "42"), "env set");
  std::string value;
  bool vf = false;
  check(pf::env::get("VD_PLATFORM_SELFTEST_VAR", value, &vf).ok() && vf && value == "42",
        "env set/get roundtrip", value);
  check_status(pf::env::unset("VD_PLATFORM_SELFTEST_VAR"), "env unset");
  std::string gone;
  bool gf = true;
  check(pf::env::get("VD_PLATFORM_SELFTEST_VAR", gone, &gf).ok() && !gf, "env unset removes");
}

void test_time() {
  const vd::platform::i64 wall = pf::time::now_unix_ms();
  /* 2020-01-01 .. 2100-01-01: catches an epoch or unit mix-up. */
  check(wall > 1577836800000LL && wall < 4102444800000LL, "now_unix_ms plausible",
        std::to_string(wall));

  const vd::platform::i64 m0 = pf::time::monotonic_ms();
  const vd::platform::i64 p0 = pf::time::perf_counter();
  pf::time::sleep_ms(25);
  const vd::platform::i64 m1 = pf::time::monotonic_ms();
  const vd::platform::i64 p1 = pf::time::perf_counter();
  check(m1 >= m0, "monotonic_ms non-decreasing");
  check(m1 - m0 >= 15, "sleep_ms actually slept", std::to_string(m1 - m0) + "ms");

  const vd::platform::i64 freq = pf::time::perf_frequency();
  check(freq > 0, "perf_frequency > 0", std::to_string(freq));
  check(p1 >= p0, "perf_counter non-decreasing");
  if (freq > 0) {
    const vd::platform::i64 elapsed_ms = (p1 - p0) * 1000 / freq;
    check(elapsed_ms >= 10, "perf_counter elapsed >= 10ms", std::to_string(elapsed_ms) + "ms");
  }
}

void test_console_and_process() {
  check(pf::console::width() > 0, "console width > 0", std::to_string(pf::console::width()));
  const vd::platform::i64 wrote = pf::console::write_line_utf8(pf::console::Stream::Out,
                                                              "  [selftest] console utf-8: \xE4\xB8\xAD\xE6\x96\x87");
  check(wrote > 0, "console write_utf8", std::to_string(wrote));
  pf::console::enable_utf8(); /* must never crash; return value is advisory */

  check(pf::process::id() > 0, "process id > 0", std::to_string(pf::process::id()));
  const std::string exe = pf::process::executable_path();
  check(!exe.empty(), "executable_path non-empty", exe);
}

}  // namespace

int main() {
  std::printf("vanadium platform self-test (%s/%s)\n", pf::os_name(), pf::arch_name());
  test_identity();
  test_path();
  test_env();
  test_time();
  test_console_and_process();

  Scratch scratch;
  if (scratch.create()) {
    test_dirs_and_files(scratch);
    test_listing(scratch);
    test_free_space_and_temp(scratch);
    test_cwd(scratch);
  }

  std::printf("platform selftest: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
