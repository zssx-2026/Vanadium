// Test double for vanadium.dll.
//
// It implements the whole public ABI of include/vanadium/vd.h and emits one
// scripted event stream per command, so the Rust shell's rendering, prompting,
// cancellation and exit-code paths can be verified before the real kernel is
// built. Never shipped.
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <vanadium/vd.h>

#if defined(_WIN32)
#define VD_API __declspec(dllexport)
#else
#define VD_API
#endif

namespace {

void nap(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int emit(vd_event_cb cb, void* user, int kind, const char* a = nullptr, const char* b = nullptr,
         long long i1 = 0, long long i2 = 0, long long i3 = 0, long long i4 = 0) {
  if (!cb) return 0;
  vd_event ev;
  ev.kind = kind;
  ev.a = a;
  ev.b = b;
  ev.i1 = i1;
  ev.i2 = i2;
  ev.i3 = i3;
  ev.i4 = i4;
  return cb(user, &ev);
}

int line(vd_event_cb cb, void* user, const char* text) {
  return emit(cb, user, VD_EV_LINE, text);
}

int status(vd_event_cb cb, void* user, const char* key, const char* text) {
  return emit(cb, user, VD_EV_STATUS, key, text);
}

int progress(vd_event_cb cb, void* user, const char* phase, long long done, long long total,
             long long bytes, long long rate) {
  return emit(cb, user, VD_EV_PROGRESS, phase, nullptr, done, total, bytes, rate);
}

}  // namespace

struct vd_context {
  int magic;
};

VD_API vd_context* vd_create(const char* home_dir, const char* lang) {
  (void)home_dir;
  (void)lang;
  vd_context* ctx = static_cast<vd_context*>(std::malloc(sizeof(vd_context)));
  if (ctx) ctx->magic = 0x5644;  // "VD"
  return ctx;
}

VD_API void vd_destroy(vd_context* ctx) { std::free(ctx); }

VD_API const char* vd_version(void) { return "0.1.0-stub"; }

VD_API const char* vd_abi(void) { return "1"; }

VD_API int vd_self_test(const char* filter, vd_event_cb cb, void* user) {
  std::string text = "selftest ok";
  if (filter && *filter) text += std::string(" filter=") + filter;
  line(cb, user, text.c_str());
  return 0;
}

VD_API int vd_run(vd_context* ctx, int argc, const char** argv, vd_event_cb cb, void* user) {
  if (!ctx || ctx->magic != 0x5644) return 2;
  const std::string cmd = (argc > 1 && argv[1]) ? argv[1] : "help";

  if (cmd == "help" || cmd == "--help" || cmd == "-h") {
    line(cb, user, "inv - Infinity Version Control System (vanadium stub)");
    line(cb, user, "usage: inv [--json] [--lang <code>] <command> [args...]");
    line(cb, user, "commands: help version demo prompt fail cancel summary selftest");
    return 0;
  }

  if (cmd == "version" || cmd == "--version" || cmd == "-v") {
    line(cb, user, "inv 0.1.0 (vanadium stub 0.1.0-stub, abi 1)");
    return 0;
  }

  if (cmd == "demo") {
    emit(cb, user, VD_EV_TITLE, "inv demo");
    line(cb, user, "demo: start");
    for (int i = 0; i < 4; ++i) {
      status(cb, user, "connect", "Connecting");
      nap(180);
    }
    emit(cb, user, VD_EV_STATUS_CLEAR);
    for (int step = 0; step <= 20; ++step) {
      progress(cb, user, "download", step, 20, step * 52428LL, 1258291LL);
      progress(cb, user, "verify", step / 2, 10, step * 1024LL, 524288LL);
      nap(60);
    }
    line(cb, user, "demo: done");
    emit(cb, user, VD_EV_WARN, "demo warning line");
    emit(cb, user, VD_EV_SUMMARY, "{\"command\":\"demo\",\"ok\":true,\"files\":3}");
    return 0;
  }

  if (cmd == "prompt") {
    const int answer = emit(cb, user, VD_EV_PROMPT, "Overwrite existing file?", "archive.json");
    line(cb, user, answer == 1 ? "answer=yes" : (answer == -1 ? "answer=cancel" : "answer=no"));
    return answer == -1 ? 130 : 0;
  }

  if (cmd == "fail") {
    emit(cb, user, VD_EV_ERROR, "simulated failure (code 42)", nullptr, 42);
    return 3;
  }

  if (cmd == "cancel") {
    for (int i = 0; i < 200; ++i) {
      if (status(cb, user, "work", "Working") == -1) {
        line(cb, user, "Operation cancelled, exit.");
        return 130;
      }
      nap(50);
    }
    line(cb, user, "cancel: not interrupted");
    return 0;
  }

  if (cmd == "summary") {
    emit(cb, user, VD_EV_SUMMARY, "{\"command\":\"summary\",\"items\":[1,2,3],\"ok\":true}");
    return 0;
  }

  if (cmd == "selftest") {
    return vd_self_test(argc > 2 ? argv[2] : nullptr, cb, user);
  }

  line(cb, user, "stub: unknown command");
  return 1;
}
