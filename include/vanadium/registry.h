#pragma once
/* Workspace registry: <workspace>/.inv/<id>.inv records.

   The registry is the hidden bookkeeping directory the CLI keeps next to a
   working tree. `inv init` creates it; nothing else does. `inv reg` writes one
   `<id>.inv` record per registered branch, so a workspace can point at archives
   that live outside it without copying their metadata around. */
#include <optional>
#include <string>
#include <vector>

#include "core.h"

namespace vd {

constexpr const char* kRegistryDir = ".inv";
constexpr const char* kRegistrySuffix = ".inv";

struct RegistryEntry {
  std::string id;          /* branch id; also the record's file name stem */
  std::string archive;     /* owning archive id */
  std::string name;        /* archive name */
  std::string type;        /* full | nodeonly | least */
  std::string path;        /* canonical branch path, e.g. "/docs" */
  std::string dir;         /* absolute directory the branch was registered from */
  i64 created_ms = 0;      /* archive creation time */
  i64 registered_ms = 0;   /* when this record was written */
  i64 head_seq = 0;        /* branch head sequence at registration time */
};

/* True when <root>/.inv exists as a directory. Never creates anything. */
bool workspace_initialized(const std::string& root);

/* Creates <root>/.inv and marks it hidden on Windows. Returns true when the
   directory was created by this call. */
bool ensure_registry_dir(const std::string& root);

std::string registry_dir(const std::string& root);
std::string registry_path(const std::string& root, const std::string& id);

/* Writes (or replaces) <root>/.inv/<id>.inv atomically. Fails with err set when
   the workspace was never initialised; it never creates .inv implicitly. */
bool write_registry(const std::string& root, const RegistryEntry& entry, std::string* err);

std::optional<RegistryEntry> read_registry(const std::string& root, const std::string& id);
std::vector<RegistryEntry> list_registry(const std::string& root);   /* sorted by id */
bool remove_registry(const std::string& root, const std::string& id);

}  // namespace vd
