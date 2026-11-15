/* Standalone WinHTTP probe for the remote layer.
   Not part of the main build: it links the real transport code plus a minimal
   core shim so the remote module can be exercised before the kernel lands.
   Build: see docs/IDM-NOTES.md. */
#include <cstdio>
#include <string>

#include "vanadium/core.h"
#include "vanadium/remote.h"
#include "winhttp_util.h"

namespace {

int g_cases = 0;
int g_bad = 0;
int g_progress = 0;

void check(bool ok, const char* name, const std::string& detail) {
  ++g_cases;
  if (!ok) ++g_bad;
  std::printf("PROBE case=%d %-22s %s  %s\n", g_cases, name, ok ? "ok  " : "FAIL",
              detail.c_str());
}

int on_event(void*, const vd_event* ev) {
  if (ev->kind == VD_EV_PROGRESS) ++g_progress;
  return 1;
}

}  // namespace

int main() {
  using namespace vd;
  using namespace vd::remote;

  const std::string api = "https://api.github.com/rate_limit";
  const std::string file = "https://proof.ovh.net/files/1Mb.dat";
  const u64 expected = 1048576;

  vd::Emitter ui(on_event, nullptr);
  std::printf("INV_PROBE transport=winhttp\n");

  /* 1. Plain API GET. */
  HttpResponse rate = get(api, "", 30000);
  check(rate.status == 200, "get_rate_limit",
        "status=" + std::to_string(rate.status) + " bytes=" + std::to_string(rate.body.size()));
  check(rate.header("content-type").find("json") != std::string::npos, "response_header",
        "content-type=" + rate.header("content-type"));

  /* 2. One-byte range probe: size plus range support. */
  detail::ProbeInfo info = detail::probe_remote(file, 30000);
  check(info.status == 200 || info.status == 206, "probe_status",
        "status=" + std::to_string(info.status));
  check(info.total == expected, "probe_total", "total=" + std::to_string(info.total));
  check(info.ranges, "probe_ranges", std::string("accept-ranges=") + (info.ranges ? "yes" : "no"));

  /* 3. Range request answered with 206. */
  std::string head;
  bool head_ok = fetch_range(file, 0, 1023, head, 30000);
  check(head_ok && head.size() == 1024, "fetch_range_206",
        "bytes=" + std::to_string(head.size()));

  /* 4. Server that ignores Range (200 + full body) is cropped locally. */
  std::string cropped;
  bool crop_ok = fetch_range(api, 10, 19, cropped, 30000);
  check(crop_ok && cropped.size() == 10 && rate.body.compare(10, 10, cropped) == 0,
        "fetch_range_crop", "bytes=" + std::to_string(cropped.size()));

  /* 5. Reference body for the content comparison. */
  HttpOptions opt;
  opt.timeout_ms = 120000;
  opt.retries = 2;
  HttpResponse whole = request(file, opt);
  check(whole.status == 200 && whole.body.size() == expected, "reference_body",
        "status=" + std::to_string(whole.status) + " bytes=" +
            std::to_string(whole.body.size()));

  /* 6. Segmented download: four connections, in-order merge, progress events. */
  std::string out = fs::join(fs::temp_dir(), "inv_probe_1mb.bin");
  if (fs::exists(out)) fs::remove_file(out);
  DownloadReport report;
  bool dl_ok = download_segmented(file, out, 4, &ui, &report);
  check(dl_ok, "download_segmented",
        "bytes=" + std::to_string(report.bytes) + " threads=" + std::to_string(report.threads) +
            " ms=" + std::to_string(report.elapsed_ms) +
            " retries=" + std::to_string(report.retries));
  check(report.bytes == expected && report.threads == 4, "download_report",
        "bytes=" + std::to_string(report.bytes) + " threads=" +
            std::to_string(report.threads));

  std::string got;
  bool read_ok = fs::read_file_opt(out, got);
  check(read_ok && got == whole.body, "download_content",
        "bytes=" + std::to_string(got.size()) +
            " match=" + ((got == whole.body) ? "yes" : "no"));
  check(g_progress > 0, "progress_events", "events=" + std::to_string(g_progress));

  /* 7. threads <= 0 asks for the default of eight, but the 256 KiB minimum
     segment keeps a 1 MiB object at four connections. */
  DownloadReport auto_report;
  bool auto_ok = download_segmented(file, out, 0, nullptr, &auto_report);
  check(auto_ok && auto_report.threads == 4, "download_default_threads",
        "threads=" + std::to_string(auto_report.threads) + " ms=" +
            std::to_string(auto_report.elapsed_ms));

  /* 8. A missing resource must be reported, not silently accepted. */
  std::string missing;
  bool missing_ok = fetch_range("https://api.github.com/inv-probe-missing-resource", 0, 9,
                                missing, 20000);
  check(!missing_ok, "fetch_range_404", std::string("returned=") + (missing_ok ? "true" : "false"));

  if (fs::exists(out)) fs::remove_file(out);

  std::printf("PROBE cases=%d bad=%d\n", g_cases, g_bad);
  std::printf("INV_PROBE_VERDICT=%s\n", g_bad == 0 ? "PASS" : "FAIL");
  return g_bad == 0 ? 0 : 1;
}
