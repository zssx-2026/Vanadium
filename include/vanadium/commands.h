#pragma once
#include <string>
#include <vector>
#include "core.h"
#include "i18n.h"
#include "settings.h"
#include "ui.h"

namespace vd {

struct App {
  std::string home;          /* %APPDATA%/inv */
  std::string cwd;
  Lang lang = LANG_EN;
  bool lang_locked = false;  /* -lang / vd_create wins over the stored setting */
  Emitter ui;
  Settings settings;
  bool json = false;
  bool yes = false;          /* -yes / --yes skip prompts */
  bool quiet = false;
  bool no_color = false;
};

/* argv[0] is "inv"; argv[1] is the command. */
int dispatch(App& app, const std::vector<std::string>& args);

/* helpers shared by commands */
std::string need_arg(const std::vector<std::string>& args, std::size_t i, const char* name);
/* Positional tokens of a command line with options (and their values) removed;
   the command name stays at index 0. */
std::vector<std::string> positionals(const std::vector<std::string>& args);
std::string opt_value(const std::vector<std::string>& args, const std::string& key,
                      const std::string& def = {});
bool has_flag(const std::vector<std::string>& args, const std::string& flag);
void print_help(App& app, const std::string& command);

/* Individual commands, one translation unit per family under src/commands. */
int cmd_help(App&, const std::vector<std::string>&);
int cmd_version(App&, const std::vector<std::string>&);
int cmd_set(App&, const std::vector<std::string>&);
int cmd_init(App&, const std::vector<std::string>&);
int cmd_status(App&, const std::vector<std::string>&);
int cmd_push(App&, const std::vector<std::string>&);
int cmd_pull(App&, const std::vector<std::string>&);
int cmd_log(App&, const std::vector<std::string>&);
int cmd_diff(App&, const std::vector<std::string>&);
int cmd_reg(App&, const std::vector<std::string>&);
int cmd_unreg(App&, const std::vector<std::string>&);
int cmd_list(App&, const std::vector<std::string>&);
int cmd_search(App&, const std::vector<std::string>&);
int cmd_rb(App&, const std::vector<std::string>&);
int cmd_rm(App&, const std::vector<std::string>&);
int cmd_clone(App&, const std::vector<std::string>&);
int cmd_fork(App&, const std::vector<std::string>&);
int cmd_login(App&, const std::vector<std::string>&);
int cmd_release(App&, const std::vector<std::string>&);
int cmd_edit(App&, const std::vector<std::string>&);
int cmd_download(App&, const std::vector<std::string>&);
int cmd_verify(App&, const std::vector<std::string>&);
int cmd_gc(App&, const std::vector<std::string>&);
int cmd_doctor(App&, const std::vector<std::string>&);
int cmd_report(App&, const std::vector<std::string>&);
int cmd_remote(App&, const std::vector<std::string>&);
int cmd_self_test(App&, const std::vector<std::string>&);
int cmd_bench(App&, const std::vector<std::string>&);
int cmd_completion(App&, const std::vector<std::string>&);

}  // namespace vd
