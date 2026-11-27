/* Vanadium kernel: public C ABI. Owns the App instance and translates every
   exception into a VD_EV_ERROR event so the Rust shell never sees C++ state. */
#include "vanadium/vd.h"

#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "vanadium/commands.h"
#include "vanadium/core.h"
#include "vanadium/i18n.h"
#include "vanadium/ui.h"

struct vd_context {
  vd::App app;
};

namespace {

constexpr const char* kVersion = "0.5.0-snapshot.3";
constexpr const char* kAbi = "vanadium-abi-1";

std::string current_dir() {
  std::string dir = vd::fs::absolute(".");
  return dir.empty() ? std::string(".") : dir;
}

void report_failure(vd::Emitter& ui, const vd::Failure& f) {
  const std::string text = f.detail.empty() ? f.message : f.message + ": " + f.detail;
  ui.error(f.code, text);
}

}  // namespace

extern "C" {

vd_context* vd_create(const char* home_dir, const char* lang) {
  vd_context* ctx = new (std::nothrow) vd_context();
  if (ctx == nullptr) return nullptr;
  if (home_dir != nullptr && home_dir[0] != '\0') {
    ctx->app.home = home_dir;
  } else {
    const char* appdata = std::getenv("APPDATA");
    ctx->app.home = vd::fs::join(appdata != nullptr ? appdata : ".", "inv");
  }
  if (lang != nullptr && lang[0] != '\0') {
    ctx->app.lang = vd::lang_from_code(lang);
    ctx->app.lang_locked = true;
  }
  vd::set_language(ctx->app.lang);
  ctx->app.cwd = current_dir();
  ctx->app.settings.load(vd::fs::join(ctx->app.home, "settings.json"));
  return ctx;
}

void vd_destroy(vd_context* ctx) { delete ctx; }

int vd_run(vd_context* ctx, int argc, const char** argv, vd_event_cb cb, void* user) {
  if (ctx == nullptr) return 9000;
  ctx->app.ui = vd::Emitter(cb, user);
  ctx->app.cwd = current_dir();
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc > 0 ? argc : 1));
  if (argc > 0 && argv != nullptr) {
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i] != nullptr ? argv[i] : "");
  } else {
    args.emplace_back("inv");
  }
  try {
    return vd::dispatch(ctx->app, args);
  } catch (const vd::Failure& f) {
    report_failure(ctx->app.ui, f);
    return f.code == 0 ? 1 : f.code;
  } catch (const std::exception& e) {
    ctx->app.ui.error(9001, e.what());
    return 9001;
  } catch (...) {
    ctx->app.ui.error(9002, "unknown kernel exception");
    return 9002;
  }
}

const char* vd_version(void) { return kVersion; }

const char* vd_abi(void) { return kAbi; }

int vd_self_test(const char* filter, vd_event_cb cb, void* user) {
  vd::App app;
  app.ui = vd::Emitter(cb, user);
  app.home = vd::fs::join(vd::fs::appdata_dir(), "inv");
  app.cwd = current_dir();
  std::vector<std::string> args = {"inv", "self-test"};
  if (filter != nullptr && filter[0] != '\0') args.emplace_back(filter);
  try {
    return vd::cmd_self_test(app, args);
  } catch (const vd::Failure& f) {
    report_failure(app.ui, f);
    return -1;
  } catch (const std::exception& e) {
    app.ui.error(9001, e.what());
    return -1;
  }
}

}  // extern "C"
