#include <cstdio>
#include <string>
#include <vector>
#include "vanadium/store.h"
#include "vanadium/crypto.h"
#include "vanadium/core.h"

namespace vd::gen {
const char* msg_lookup(const char*, int) { return nullptr; }
int msg_catalog_count() { return 0; }
}  // namespace vd::gen

using namespace vd;

static int fails = 0;
static void expect(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++fails;
}

static int run();
int main() {
  try { return run(); } catch (const vd::Failure& f) {
    std::printf("FAILURE code=%d msg=%s detail=%s\n", f.code, f.message.c_str(), f.detail.c_str());
    return 2;
  }
}
static int run() {
  const std::string dir = fs::join(fs::temp_dir(), "inv_store_test_" + random_hex(4));
  fs::remove_all(dir);
  fs::mkdirs(dir);
  Archive a = Archive::create(dir, "demo", ArchiveType::NodeOnly);
  expect(a.branch_exists("/"), "root branch created");
  BranchInfo b = a.create_branch("/feature/x", "x");
  expect(b.path == "/feature/x", "branch path canonical");
  expect(a.branch_exists("/feature/x"), "branch exists");
  auto parent = a.find("/");
  expect(parent && parent->children.size() == 1, "parent has child");

  std::string sha = a.objects().put("hello world");
  expect(sha.size() == 64, "object sha length");
  std::string back;
  expect(a.objects().get(sha, back) && back == "hello world", "object roundtrip");
  expect(a.objects().has(sha), "object has");

  std::vector<Entry> entries;
  Entry f;
  f.path = "a.txt";
  f.size = 11;
  f.sha = sha;
  f.crc = crc64("hello world", 11);
  entries.push_back(f);
  Entry d;
  d.path = "sub";
  d.dir = true;
  entries.push_back(d);

  std::string id1 = a.commit("/", entries, "first");
  expect(id1.size() == 64, "commit id");
  auto h = a.head("/");
  expect(h && h->seq == 1 && h->entries.size() == 2, "head seq/entries");
  std::string id2 = a.commit("/", entries, "second");
  expect(id2 != id1, "second commit differs");
  auto hist = a.history("/");
  expect(hist.size() == 2 && hist[0].seq == 1, "history ordered");
  expect(hist[1].parent_sha == id1, "parent chain");

  const std::string outdir = fs::join(dir, "out");
  a.materialize("/", 2, outdir, nullptr);
  expect(fs::is_file(fs::join(outdir, "a.txt")), "materialized file");
  expect(fs::read_file(fs::join(outdir, "a.txt")) == "hello world", "materialized content");

  Archive reopened = Archive::open(dir);
  expect(reopened.name() == "demo", "reopen name");
  expect(reopened.branch_exists("/feature/x"), "reopen branches");
  auto h2 = reopened.head("/");
  expect(h2 && h2->seq == 2, "reopen head");

  // scan_working_tree against head
  const std::string wt = fs::join(dir, "wt");
  fs::mkdirs(wt);
  fs::write_file(fs::join(wt, "a.txt"), "hello world");
  std::vector<std::string> deleted;
  auto changed = reopened.scan_working_tree(wt, &deleted);
  expect(changed.empty(), "scan: identical tree has no changes");
  fs::write_file(fs::join(wt, "b.txt"), "new");
  changed = reopened.scan_working_tree(wt, &deleted);
  bool saw_b = false;
  for (auto& e : changed) if (e.path == "b.txt") saw_b = true;
  expect(saw_b, "scan: detects new file");

  reopened.remove_branch("/feature/x", false);
  expect(!reopened.branch_exists("/feature/x"), "branch removed");
  fs::remove_all(dir);
  std::printf("FAILS=%d\n", fails);
  return fails ? 1 : 0;
}
