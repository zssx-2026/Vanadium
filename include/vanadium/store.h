#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "core.h"

namespace vd {

enum class ArchiveType { Full, NodeOnly, Least };
const char* archive_type_name(ArchiveType t);
ArchiveType archive_type_parse(const std::string& s);

struct Entry {
  std::string path;      /* archive-relative, '/'-separated, no leading slash */
  bool dir = false;
  u64 size = 0;
  std::string sha;       /* sha256 hex of content (dirs: empty) */
  u64 crc = 0;
  i64 mtime = 0;
  u32 mode = 0644;
};

struct Snapshot {
  i64 seq = 0;
  std::string branch;
  i64 time_ms = 0;
  std::string message;
  std::string parent_sha;   /* snapshot id of parent commit */
  std::string id;           /* sha256 of canonical snapshot body */
  std::vector<Entry> entries;
};

struct BranchInfo {
  std::string id;
  std::string path;      /* canonical: /branches/a/b */
  std::string name;
  std::string parent;
  i64 created_ms = 0;
  i64 head_seq = 0;
  bool is_node = false;
  std::vector<std::string> children;
  bool system = false;
};

class ObjectStore {
 public:
  ObjectStore() = default;
  explicit ObjectStore(std::string root);
  std::string put(const std::string& data);
  std::string put_file(const std::string& path, u64* size_out = nullptr);
  bool get(const std::string& sha, std::string& out) const;
  bool has(const std::string& sha) const;
  u64 stored_size(const std::string& sha) const;
  std::string object_path(const std::string& sha) const;
  std::vector<std::string> all_objects() const;
  void erase(const std::string& sha);
  void gc(const std::set<std::string>& live);
  u64 total_bytes() const;

 private:
  std::string root_;
};

class Archive {
 public:
  static bool looks_like_archive(const std::string& root);
  static Archive create(const std::string& root, const std::string& name, ArchiveType type);
  static Archive open(const std::string& root);

  const std::string& root() const { return root_; }
  const std::string& name() const { return name_; }
  const std::string& id() const { return archive_id_; }
  i64 created_ms() const { return created_ms_; }
  ArchiveType type() const { return type_; }
  ObjectStore& objects() { return objects_; }
  const ObjectStore& objects() const { return objects_; }

  std::vector<BranchInfo> branches() const;
  std::optional<BranchInfo> find(const std::string& path) const;
  bool branch_exists(const std::string& path) const;
  BranchInfo create_branch(const std::string& path, const std::string& name);
  void remove_branch(const std::string& path, bool recursive);

  /* Snapshots */
  std::optional<Snapshot> head(const std::string& path) const;
  std::optional<Snapshot> snapshot_at(const std::string& path, i64 seq) const;
  std::vector<Snapshot> history(const std::string& path, std::size_t limit = 0) const;
  std::string commit(const std::string& path, const std::vector<Entry>& entries,
                     const std::string& message);

  /* Materialize a snapshot into a directory (rollback). */
  void materialize(const std::string& path, i64 seq, const std::string& target,
                   class Emitter* ui);

  /* Diff working directory against head; returns entries to add/update/delete.
     When non-null, *added receives the paths that are new since head (the rest
     of the returned entries are modifications of known files). */
  std::vector<Entry> scan_working_tree(const std::string& root,
                                       std::vector<std::string>* deleted,
                                       std::set<std::string>* added = nullptr) const;

  void save_meta();
  void reload();

 private:
  std::string meta_path() const;
  std::string branch_dir(const std::string& id) const;

  std::string root_;
  std::string name_;
  ArchiveType type_ = ArchiveType::NodeOnly;
  std::string archive_id_;
  i64 created_ms_ = 0;
  std::vector<BranchInfo> branches_;
  ObjectStore objects_;
  Json extra_;
};

}  // namespace vd
