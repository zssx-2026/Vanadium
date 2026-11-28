#pragma once
// Git interoperability for inv.
//
// Design: coexistence + delegation. inv keeps its own archive format
// (meta.json + objects/ + branches/) and never rewrites .git. Anything that
// needs real git semantics (clone/push/pull/ahead-behind/dirty) is delegated
// to the git executable with an argv array (never a shell string), and every
// delegated call is optional: when git is absent the feature reports
// "git: not found" instead of failing the whole command.
//
// The core of this module is deliberately free of vd::App so it can be unit
// tested without the CLI layer. The App-facing entry points at the bottom are
// implemented in src/commands/git_cmds.cpp.

#include <cstddef>
#include <string>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/store.h"

namespace vd {

struct App;      // commands.h
class Emitter;   // ui.h

namespace gitcompat {

// ---------------------------------------------------------------------------
// URL classification
// ---------------------------------------------------------------------------

// inv clone accepts two very different things today: an ivpack release asset
// and (with this module) a real git remote. The rule is purely syntactic and
// documented in docs/GIT-INTEROP.md:
//   GitRemote -> ends with ".git", scp-like user@host:path, scheme git/ssh,
//                or an exact owner/repo path on a known forge host.
//   InvPack   -> a forge host URL that points at a release asset or a web
//                page (/releases/..., /archive/..., /blob/..., /raw/...).
//   Unknown   -> anything else; callers keep the legacy inv behaviour.
enum class UrlKind { Unknown, GitRemote, InvPack };

UrlKind classify_url(const std::string& url);
const char* url_kind_name(UrlKind kind);

// ---------------------------------------------------------------------------
// Detection (pure text: reads .git/HEAD, refs, packed-refs, config)
// ---------------------------------------------------------------------------

struct GitRemote {
  std::string name;       // "origin"
  std::string fetch_url;
  std::string push_url;
};

struct GitTrack {
  std::string branch;     // "main"
  std::string remote;     // "origin"
  std::string merge;      // "refs/heads/main"
  std::string upstream;   // "origin/main" (derived)
};

struct GitRepoInfo {
  bool found = false;
  bool bare = false;
  bool linked = false;          // .git was a file (linked worktree / submodule)
  std::string root;             // work tree root, absolute, '/'-separated
  std::string git_dir;          // per-worktree git dir
  std::string common_dir;       // shared git dir (== git_dir unless linked)
  std::string gitlink;          // raw "gitdir: ..." target when linked
  std::string head_raw;         // raw content of HEAD
  bool detached = false;
  std::string head_ref;         // "refs/heads/main" for a symbolic HEAD
  std::string branch;           // "main"; empty when detached or unborn
  std::string head_sha;         // resolved commit id, may be empty (unborn)
  std::vector<GitRemote> remotes;
  std::vector<GitTrack> tracks;
  std::vector<std::string> local_branches;
  std::string error;            // non-empty when .git exists but is unreadable

  const GitRemote* remote(const std::string& name) const;
  const GitTrack* track(const std::string& branch_name) const;
};

// Walks up from dir (max 64 levels) looking for .git (dir or file) or a bare
// repository layout. Never throws; inspect info.found / info.error.
GitRepoInfo detect(const std::string& dir);

// Human-readable multi-line rendering used by "inv git-info".
std::string format_info(const GitRepoInfo& info);

// ---------------------------------------------------------------------------
// Process execution (argv array, no shell, no string concatenation)
// ---------------------------------------------------------------------------

struct ProcResult {
  bool started = false;
  int exit_code = -1;
  std::string out;
  std::string err;
  std::string start_error;   // why the process could not be started

  bool ok() const { return started && exit_code == 0; }
  // Combined, trimmed one-line diagnosis suitable for an [error] line.
  std::string diagnose() const;
};

bool git_available();
std::string git_path();      // absolute path of git, empty when not found
std::string git_version();   // "git version 2.55.0.windows.4" or ""

// Runs argv[0] (resolved through PATH when it has no separator) with the given
// working directory. stdout/stderr are captured through temporary files, so a
// chatty child can never deadlock us on a full pipe. timeout_ms == 0 waits
// forever. stdin_path, when set, is fed to the child as stdin.
ProcResult run_capture(const std::vector<std::string>& argv,
                       const std::string& cwd,
                       const std::vector<std::string>& env_extra = {},
                       int timeout_ms = 0,
                       const std::string& stdin_path = {});

// Same, with "git" prepended and GIT_TERMINAL_PROMPT=0 / GCM_INTERACTIVE=never
// so a credential prompt can never block an unattended run.
ProcResult git_run(const std::vector<std::string>& args,
                   const std::string& cwd,
                   int timeout_ms = 0,
                   const std::string& stdin_path = {});

// ---------------------------------------------------------------------------
// Status (delegated to git)
// ---------------------------------------------------------------------------

struct StatusSummary {
  bool ok = false;
  bool has_upstream = false;
  std::string branch;
  std::string upstream;
  long long ahead = 0;
  long long behind = 0;
  std::size_t dirty = 0;
  std::vector<std::string> dirty_lines;   // truncated to dirty_limit
  std::string error;                      // "git: not found" / "not a git repository"
};

StatusSummary status_summary(const GitRepoInfo& info, std::size_t dirty_limit = 20);

// ---------------------------------------------------------------------------
// .gitignore management (idempotent append inside a marked block, undoable)
// ---------------------------------------------------------------------------

struct IgnoreResult {
  bool ok = true;
  bool changed = false;
  bool created = false;
  std::string path;
  std::string error;
};

// Archive metadata that must never be committed to the user's git repo.
const std::vector<std::string>& default_ignore_entries();

IgnoreResult ensure_ignored(const std::string& work_root,
                            const std::vector<std::string>& entries);
IgnoreResult remove_ignored(const std::string& work_root,
                            const std::vector<std::string>& entries);

// ---------------------------------------------------------------------------
// Delegated commands
// ---------------------------------------------------------------------------

struct DelegateResult {
  bool started = false;
  int exit_code = -1;
  std::string out;
  std::string err;
  std::string start_error;
  bool ok() const { return started && exit_code == 0; }
};

// depth <= 0 means a full clone.
DelegateResult clone_git(const std::string& url,
                         const std::string& target,
                         int depth,
                         const std::vector<std::string>& passthrough);

DelegateResult push_git(const std::string& cwd,
                        const std::vector<std::string>& passthrough);

DelegateResult pull_git(const std::string& cwd,
                        const std::vector<std::string>& passthrough);

// ---------------------------------------------------------------------------
// inv -> git export / git -> inv import
// ---------------------------------------------------------------------------

struct ExportReport {
  bool ok = false;
  std::size_t commits = 0;
  std::string target;
  std::string error;
  std::vector<std::string> warnings;
};

// Replays every snapshot of branch into target (a new or existing git repo)
// as one commit per snapshot. Author identity is passed with -c so the user
// git config is never modified.
ExportReport export_branch(Archive& archive,
                           const std::string& branch,
                           const std::string& target,
                           const std::string& author_name,
                           const std::string& author_email,
                           Emitter* ui);

struct ImportReport {
  bool ok = false;
  std::size_t commits = 0;
  std::size_t files = 0;
  std::string branch;
  std::string error;
  std::vector<std::string> warnings;
};

// Replays a (linear) git history into an inv branch, one snapshot per commit.
// Merge commits are flattened in rev-list order; original git author dates are
// preserved in the snapshot message because Archive::commit stamps now_ms().
ImportReport import_git(Archive& archive,
                        const std::string& git_repo,
                        const std::string& branch_path,
                        const std::string& branch_name,
                        std::size_t max_commits,
                        Emitter* ui);

// ---------------------------------------------------------------------------
// App-facing entry points (implemented in src/commands/git_cmds.cpp)
// ---------------------------------------------------------------------------

// Returns true when args carry a git remote URL, so cmd_clone can hand over.
bool wants_git_clone(const std::vector<std::string>& args);
bool wants_git_push(const std::vector<std::string>& args);
bool wants_git_pull(const std::vector<std::string>& args);

int cmd_clone_git(App& app, const std::vector<std::string>& args);
int cmd_push_git(App& app, const std::vector<std::string>& args);
int cmd_pull_git(App& app, const std::vector<std::string>& args);
int cmd_export_git(App& app, const std::vector<std::string>& args);
int cmd_import_git(App& app, const std::vector<std::string>& args);
int cmd_git_info(App& app, const std::vector<std::string>& args);

// One-line "git: <branch> ..." section for "inv status". Never throws and
// never fails the caller: without git it prints "git: not found".
int print_status_section(App& app, const std::string& dir);

// Called by "inv init" after Archive::create so the new metadata does not show
// up as untracked noise in an enclosing git repository.
int init_gitignore(App& app, const std::string& archive_root);

}  // namespace gitcompat
}  // namespace vd
