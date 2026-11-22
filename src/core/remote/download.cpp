#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/remote.h"
#include "transport.h"

namespace vd::remote {
namespace {

/* Segmentation policy (mirrors the IDM behaviour documented in docs/IDM-NOTES.md):
   one connection per thread, the largest outstanding range is split while a
   worker is still idle, and the tail of the object is split last. */
constexpr u64 kMinSegment = 256ull * 1024;
constexpr u64 kSplitThreshold = 4ull * 1024 * 1024;
constexpr int kMaxThreads = 64;
constexpr int kDefaultThreads = 8;
constexpr int kSegmentRetries = 3;
constexpr i64 kProgressIntervalMs = 200;

/* Probe policy. The probe is the first network touch of a download and used to
   abort the whole transfer on a single 12002 timeout; three attempts with
   0.5/1.5/4 s backoff ride out a transient edge. Only retryable transport
   errors (12002/12029/12030) and 5xx responses are retried. */
constexpr int kProbeAttempts = 3;
constexpr int kProbeBackoffMs[kProbeAttempts] = {500, 1500, 4000};
constexpr int kProbeTimeoutMs = 90000;

/* Phase timeouts. On a weak network the handshake alone can take >20 s, so the
   connect budget is raised well above the derived default. Both values are
   overridable through the environment for field diagnosis (the CLI help lives
   in src/commands, so the names are documented here and in
   docs/IDM-NOTES.md section 9):
     INV_HTTP_RESOLVE_MS  DNS/name resolution budget, default 15000 ms
     INV_HTTP_CONNECT_MS  TCP+TLS handshake budget, default 60000 ms
   Values below 1000 are ignored; the upper clamp is 600000 ms. */
int tune_ms(const char* env_name, int fallback) {
  const char* raw = std::getenv(env_name);
  if (!raw || !*raw) return fallback;
  const int value = std::atoi(raw);
  if (value < 1000) return fallback;
  return value > 600000 ? 600000 : value;
}

int resolve_ms() {
  static const int value = tune_ms("INV_HTTP_RESOLVE_MS", 15000);
  return value;
}

int connect_ms() {
  static const int value = tune_ms("INV_HTTP_CONNECT_MS", 60000);
  return value;
}

/* Appended to failure details so the user sees which stage ran out of luck and
   how far the transfer got. */
std::string where(const char* stage, int attempt, int attempts, int threads, u64 bytes) {
  return std::string("stage=") + stage + " attempt=" + std::to_string(attempt) + "/" +
         std::to_string(attempts) + " threads=" + std::to_string(threads) +
         " bytes=" + std::to_string(bytes);
}

struct Segment {
  u64 from = 0;
  u64 to = 0; /* inclusive */
  int index = 0;
  std::string path;
  u64 size() const { return to - from + 1; }
};

struct Job {
  std::string url;
  std::string out_path;
  u64 total = 0;
  /* Resume validators from the probe (IDM validates with If-Range, see
     docs/IDM-NOTES.md 2.3). Empty when the server sent neither header. */
  std::string etag;
  std::string last_modified;
  int threads = 1;
  Emitter* ui = nullptr;

  std::vector<Segment> segments;
  std::vector<int> pending;

  std::mutex mu;
  std::condition_variable cv;
  int active = 0;
  int split_budget = 0;
  i64 last_emit_ms = 0;

  std::atomic<u64> completed{0};
  std::atomic<int> retries{0};
  std::atomic<bool> abort{false};
  std::string error;
};

/* Adds attribution unless a deeper stage already supplied it. */
void add_where(Job& job, const char* stage, int attempt, int attempts, u64 bytes) {
  if (job.error.find("stage=") != std::string::npos) return;
  job.error += " | " + where(stage, attempt, attempts, job.threads, bytes);
}

std::string segment_path(const std::string& out_path, int index) {
  return out_path + ".tmp.d" + std::to_string(index);
}

void sleep_backoff(int attempt) {
  int ms = 250 << (attempt < 4 ? attempt : 4);
  if (ms > 4000) ms = 4000;
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void cleanup(Job& job) {
  for (const auto& seg : job.segments) {
    if (fs::exists(seg.path)) fs::remove_file(seg.path);
  }
  std::string tmp = job.out_path + ".tmp";
  if (fs::exists(tmp)) fs::remove_file(tmp);
}

void emit_progress(Job& job, u64 done) {
  if (!job.ui) return;
  i64 now = now_ms();
  if (now - job.last_emit_ms < kProgressIntervalMs) return;
  job.last_emit_ms = now;
  job.ui->progress("download", static_cast<long long>(done),
                   static_cast<long long>(job.total), static_cast<long long>(done), 0);
}

u64 partial_bytes(const std::string& path) {
  if (!fs::exists(path)) return 0;
  const i64 sz = fs::size(path);
  return sz > 0 ? static_cast<u64>(sz) : 0;
}

/* Value for If-Range: the exact ETag when present (quotes included, as the
   header requires an exact entity tag), else the Last-Modified date. */
std::string if_range_value(const Job& job) {
  if (!job.etag.empty()) return job.etag;
  return job.last_modified;
}

/* Compact validator for failure details: ETag without quotes, first 8 chars,
   else the first 10 chars of Last-Modified. Empty when the server sent none. */
std::string validator_tag(const Job& job) {
  std::string tag = job.etag;
  if (!tag.empty()) {
    if (tag.size() >= 2 && tag.front() == '"' && tag.back() == '"') {
      tag = tag.substr(1, tag.size() - 2);
    }
    if (tag.size() > 8) tag.resize(8);
    return tag;
  }
  tag = job.last_modified;
  if (tag.size() > 10) tag.resize(10);
  return tag;
}

bool download_segment(Job& job, const Segment& seg, const char* phase) {
  for (int attempt = 0; attempt <= kSegmentRetries; ++attempt) {
    if (job.abort.load()) return false;

    /* Resume from whatever this segment already holds: a dropped connection
       costs the bytes in flight, not the whole segment. */
    u64 have = partial_bytes(seg.path);
    if (have > seg.size()) {
      fs::remove_file(seg.path);
      have = 0;
    }
    if (have == seg.size()) {
      job.completed += have;
      return true;
    }

    HttpOptions opt;
    opt.method = "GET";
    opt.timeout_ms = 120000;
    opt.resolve_ms = resolve_ms();
    opt.connect_ms = connect_ms();
    opt.retries = 0; /* retried by this loop so we can count them */
    opt.headers.emplace_back("Range", "bytes=" + std::to_string(seg.from + have) + "-" +
                                          std::to_string(seg.to));
    opt.headers.emplace_back("Accept-Encoding", "identity");
    /* Only a resumed part needs a validator: a fresh Range starts at the
       segment head, so there is nothing to validate against. */
    if (have > 0 && !if_range_value(job).empty()) {
      opt.headers.emplace_back("If-Range", if_range_value(job));
    }

    try {
      detail::StreamMeta meta;
      u64 written = detail::stream_to_file(job.url, opt, seg.path, [&](u64 done, u64) {
        if (job.ui && job.ui->cancelled()) job.abort = true;
        u64 overall = job.completed.load() + have + done;
        if (job.ui) {
          job.ui->progress(phase, static_cast<long long>(have + done),
                           static_cast<long long>(seg.size()),
                           static_cast<long long>(overall), 0);
        }
        emit_progress(job, overall);
      }, have > 0, &meta);
      if (have > 0 && meta.status == 200) {
        /* Range ignored on a resume: the object was replaced (If-Range failed)
           or the server dropped range support. Restart this part from zero;
           appending the full object behind the old bytes would corrupt it. */
        if (job.total != 0 && meta.content_length != 0 && meta.content_length != job.total) {
          std::string detail = where("segment", attempt + 1, kSegmentRetries + 1, job.threads,
                                     job.completed.load() + have);
          detail += " validator-changed size=" + std::to_string(meta.content_length) +
                    " expected=" + std::to_string(job.total);
          const std::string tag = validator_tag(job);
          if (!tag.empty()) detail += " validator=" + tag;
          throw Failure(5, "validator changed: object size differs", detail);
        }
        fs::remove_file(seg.path);
        {
          std::lock_guard<std::mutex> lock(job.mu);
          if (!meta.etag.empty()) job.etag = meta.etag;
          if (!meta.last_modified.empty()) job.last_modified = meta.last_modified;
        }
        if (job.ui) {
          job.ui->warn("segment " + std::to_string(seg.index + 1) +
                       " restarted: validator changed");
        }
        if (attempt > 0) --attempt; /* a validator restart is not a retry */
        continue;
      }
      if (have + written != seg.size()) {
        throw Failure(1, "short segment",
                      "wanted " + std::to_string(seg.size()) + " got " +
                          std::to_string(have + written));
      }
      job.completed += have + written;
      return true;
    } catch (const Failure& f) {
      if (f.code == 5) {
        /* The object changed size under a resume: no retry can fix that. */
        std::lock_guard<std::mutex> lock(job.mu);
        job.error = f.message;
        if (!f.detail.empty()) job.error += " | " + f.detail;
        return false;
      }
      ++job.retries;
      {
        std::lock_guard<std::mutex> lock(job.mu);
        job.error = f.message;
        if (!f.detail.empty()) job.error += " | " + f.detail;
        if (attempt == kSegmentRetries) {
          job.error += " | " +
                       where("segment", attempt + 1, kSegmentRetries + 1, job.threads,
                             job.completed.load() + have);
          const std::string tag = validator_tag(job);
          if (!tag.empty()) job.error += " validator=" + tag;
        }
      }
      if (job.abort.load()) return false;
      if (attempt == kSegmentRetries) return false;
      sleep_backoff(attempt);
    }
  }
  return false;
}

void worker(Job& job, int thread_no) {
  char phase[32];
  std::snprintf(phase, sizeof(phase), "thread%d", thread_no + 1);

  for (;;) {
    Segment seg;
    {
      std::unique_lock<std::mutex> lock(job.mu);
      for (;;) {
        if (job.abort.load()) return;
        if (job.pending.empty()) {
          if (job.active == 0) {
            lock.unlock();
            job.cv.notify_all();
            return;
          }
          job.cv.wait(lock);
          continue;
        }
        /* Largest outstanding range first. */
        size_t best = 0;
        for (size_t i = 1; i < job.pending.size(); ++i) {
          if (job.segments[job.pending[i]].size() > job.segments[job.pending[best]].size()) {
            best = i;
          }
        }
        int index = job.pending[best];
        job.pending.erase(job.pending.begin() + static_cast<long>(best));

        if (job.segments[index].size() > kSplitThreshold && job.split_budget > 0 &&
            job.active + 1 < job.threads) {
          /* Split in half: the head is downloaded now, the tail goes back to the
             pool so the largest remaining block is always worked on first. */
          u64 from = job.segments[index].from;
          u64 to = job.segments[index].to;
          u64 mid = from + (to - from) / 2;
          Segment right;
          right.from = mid + 1;
          right.to = to;
          right.index = static_cast<int>(job.segments.size());
          right.path = segment_path(job.out_path, right.index);
          job.segments.push_back(right);
          job.segments[index].to = mid;
          job.pending.push_back(right.index);
          job.pending.push_back(index);
          --job.split_budget;
          continue;
        }

        seg = job.segments[index];
        ++job.active;
        break;
      }
    }

    bool ok = download_segment(job, seg, phase);
    {
      std::lock_guard<std::mutex> lock(job.mu);
      --job.active;
      if (!ok) {
        /* download_segment normally fills job.error with the transport text; the
           generic line is only a fallback. Either way the job must abort, or the
           merge step runs over missing segment files and reports the wrong stage. */
        if (job.error.empty()) {
          job.error = "segment " + std::to_string(seg.from) + "-" + std::to_string(seg.to) +
                      " failed | " +
                      where("segment", kSegmentRetries + 1, kSegmentRetries + 1, job.threads,
                            job.completed.load());
        }
        job.abort = true;
      }
    }
    job.cv.notify_all();
  }
}

/* Concatenates the segment files in offset order into <out>.tmp, then renames. */
bool merge_segments(Job& job) {
  std::vector<Segment> ordered = job.segments;
  std::sort(ordered.begin(), ordered.end(),
            [](const Segment& a, const Segment& b) { return a.from < b.from; });
  u64 expect = 0;
  for (const auto& seg : ordered) {
    if (seg.from != expect) {
      job.error = "segment layout has a gap at offset " + std::to_string(expect);
      return false;
    }
    expect = seg.to + 1;
  }
  if (job.total != 0 && expect != job.total) {
    job.error = "segment layout ends at " + std::to_string(expect) + " of " +
                std::to_string(job.total);
    return false;
  }

  std::string tmp = job.out_path + ".tmp";
  FILE* out = _wfopen(detail::widen(tmp).c_str(), L"wb");
  if (!out) {
    job.error = "cannot create " + tmp;
    return false;
  }
  std::vector<char> buffer(1u << 20);
  u64 written = 0;
  for (const auto& seg : ordered) {
    FILE* in = _wfopen(detail::widen(seg.path).c_str(), L"rb");
    if (!in) {
      std::fclose(out);
      job.error = "cannot read " + seg.path;
      return false;
    }
    size_t n = 0;
    while ((n = std::fread(buffer.data(), 1, buffer.size(), in)) > 0) {
      if (std::fwrite(buffer.data(), 1, n, out) != n) {
        std::fclose(in);
        std::fclose(out);
        job.error = "write failed for " + tmp;
        return false;
      }
      written += n;
    }
    std::fclose(in);
    fs::remove_file(seg.path);
  }
  std::fclose(out);

  if (job.total != 0 && written != job.total) {
    job.error = "merged size " + std::to_string(written) + " != " +
                std::to_string(job.total);
    return false;
  }
  if (fs::exists(job.out_path)) fs::remove_file(job.out_path);
  fs::rename(tmp, job.out_path);
  return true;
}

bool single_stream(Job& job, DownloadReport* report, i64 started) {
  std::string tmp = job.out_path + ".tmp";
  u64 written = 0;
  for (int attempt = 0; attempt <= kSegmentRetries; ++attempt) {
    u64 have = partial_bytes(tmp);
    if (job.total != 0 && have > job.total) {
      fs::remove_file(tmp);
      have = 0;
    }
    HttpOptions opt;
    opt.method = "GET";
    opt.timeout_ms = 600000;
    opt.resolve_ms = resolve_ms();
    opt.connect_ms = connect_ms();
    opt.retries = 0;
    if (have > 0) {
      opt.headers.emplace_back("Range", "bytes=" + std::to_string(have) + "-");
      opt.headers.emplace_back("Accept-Encoding", "identity");
      if (!if_range_value(job).empty()) {
        opt.headers.emplace_back("If-Range", if_range_value(job));
      }
    }
    try {
      detail::StreamMeta meta;
      u64 got = detail::stream_to_file(job.url, opt, tmp, [&](u64 done, u64) {
        if (job.ui && job.ui->cancelled()) job.abort = true;
        if (job.ui) {
          job.ui->progress("download", static_cast<long long>(have + done),
                           static_cast<long long>(job.total),
                           static_cast<long long>(have + done), 0);
        }
      }, have > 0, &meta);
      if (have > 0 && meta.status == 200) {
        /* Range ignored on a resume: the object changed. Truncate and restart
           instead of splicing the full body behind the old bytes. */
        if (job.total != 0 && meta.content_length != 0 && meta.content_length != job.total) {
          job.error = "validator changed: object size " + std::to_string(meta.content_length) +
                      " != " + std::to_string(job.total) + " | " +
                      where("segment", attempt + 1, kSegmentRetries + 1, 1, 0);
          return false;
        }
        fs::remove_file(tmp);
        if (!meta.etag.empty()) job.etag = meta.etag;
        if (!meta.last_modified.empty()) job.last_modified = meta.last_modified;
        if (job.ui) job.ui->warn("resumed transfer restarted: validator changed");
        if (attempt > 0) --attempt; /* a validator restart is not a retry */
        continue;
      }
      written = have + got;
      if (job.total != 0 && written != job.total) {
        throw Failure(1, "short transfer", std::to_string(written) + " of " +
                                                std::to_string(job.total));
      }
      break;
    } catch (const Failure& f) {
      ++job.retries;
      job.error = f.message;
      if (!f.detail.empty()) job.error += " | " + f.detail;
      if (job.abort.load() || attempt == kSegmentRetries) {
        job.error += " | " +
                     where("segment", attempt + 1, kSegmentRetries + 1, 1, have);
        const std::string tag = validator_tag(job);
        if (!tag.empty()) job.error += " validator=" + tag;
        if (fs::exists(tmp)) fs::remove_file(tmp);
        return false;
      }
      sleep_backoff(attempt);
    }
  }
  if (fs::exists(job.out_path)) fs::remove_file(job.out_path);
  fs::rename(tmp, job.out_path);
  if (report) {
    report->bytes = written;
    report->elapsed_ms = now_ms() - started;
    report->threads = 1;
    report->retries = job.retries.load();
  }
  return true;
}

}  // namespace

bool download_segmented(const std::string& url, const std::string& out_path, int threads,
                        Emitter* ui, DownloadReport* report) {
  i64 started = now_ms();
  if (report) *report = DownloadReport{};

  Job job;
  job.url = url;
  job.out_path = out_path;
  job.ui = ui;
  job.last_emit_ms = started;

  try {
    fs::mkdirs(fs::parent(out_path));
    int wanted = threads <= 0 ? kDefaultThreads : threads;
    if (wanted > kMaxThreads) wanted = kMaxThreads;

    if (ui) ui->status("download", "probing " + url);

    /* The probe is retried on its own: a single 12002 timeout here used to kill
       the whole download before any segment was ever requested. */
    detail::ProbeInfo info;
    bool probed = false;
    for (int attempt = 1;; ++attempt) {
      unsigned long err = 0;
      try {
        info = detail::probe_remote(url, kProbeTimeoutMs, resolve_ms(), connect_ms());
        if (info.status >= 500 && info.status <= 599) {
          err = static_cast<unsigned long>(info.status);
        } else {
          probed = true;
          break;
        }
      } catch (const Failure& f) {
        err = detail::transport_error_code(f);
        if (!detail::transport_retryable(f) || attempt >= kProbeAttempts) {
          /* The probe only buys size/range knowledge. A dead edge must not kill
             a download that a plain GET could still complete, so fall through
             to a single stream and let its own retry budget run. */
          if (ui) {
            ui->warn("probe failed (" + f.message + "), falling back to a single stream");
          }
          probed = false;
          break;
        }
      }
      if (attempt >= kProbeAttempts) {
        if (ui) {
          ui->warn("probe failed: HTTP " + std::to_string(info.status) +
                   ", falling back to a single stream");
        }
        probed = false;
        break;
      }
      const int delay = kProbeBackoffMs[attempt - 1];
      if (ui) {
        ui->warn("probe attempt " + std::to_string(attempt + 1) + "/" +
                 std::to_string(kProbeAttempts) + " after " + std::to_string(delay) +
                 "ms (error " + std::to_string(err) + ")");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    job.total = info.total;
    job.etag = info.etag;
    job.last_modified = info.last_modified;

    if (!probed) {
      /* The probe never answered. A plain GET may still succeed on this edge,
         and single_stream carries its own retry budget, so try that instead of
         failing the command on a probe-only error. */
      if (ui) ui->clear_status();
      bool ok = single_stream(job, report, started);
      if (!ok && ui) ui->error(1, "download failed: " + job.error);
      return ok;
    }

    if (!info.ranges || job.total == 0) {
      /* Server does not support byte ranges: one connection, one stream. */
      if (ui) ui->clear_status();
      bool ok = single_stream(job, report, started);
      if (!ok && ui) ui->error(1, "download failed: " + job.error);
      return ok;
    }

    u64 max_segments = std::max<u64>(1, job.total / kMinSegment);
    int count = static_cast<int>(std::min<u64>(static_cast<u64>(wanted), max_segments));
    if (count < 1) count = 1;
    job.threads = count;
    job.split_budget = count * 4;

    job.segments.reserve(static_cast<size_t>(count) * 5 + 8);
    u64 chunk = job.total / static_cast<u64>(count);
    for (int i = 0; i < count; ++i) {
      Segment seg;
      seg.from = chunk * static_cast<u64>(i);
      seg.to = (i == count - 1) ? job.total - 1 : chunk * static_cast<u64>(i + 1) - 1;
      seg.index = i;
      seg.path = segment_path(out_path, i);
      job.segments.push_back(seg);
      job.pending.push_back(i);
    }

    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) pool.emplace_back(worker, std::ref(job), i);
    for (auto& t : pool) t.join();

    if (job.abort.load()) {
      cleanup(job);
      add_where(job, "segment", kSegmentRetries + 1, kSegmentRetries + 1, job.completed.load());
      if (ui) ui->error(1, "download failed: " + job.error);
      return false;
    }
    if (!merge_segments(job)) {
      cleanup(job);
      add_where(job, "merge", 1, 1, job.completed.load());
      if (ui) ui->error(1, "download failed: " + job.error);
      return false;
    }
    if (ui) ui->clear_status();

    if (report) {
      report->bytes = job.total;
      report->elapsed_ms = now_ms() - started;
      report->threads = job.threads;
      report->retries = job.retries.load();
    }
    return true;
  } catch (const Failure& f) {
    cleanup(job);
    if (ui) {
      std::string text = f.message;
      if (!f.detail.empty()) text += " | " + f.detail;
      if (text.empty()) text = "unknown transport failure for " + url;
      ui->error(f.code, "download failed: " + text);
    }
    return false;
  }
}

}  // namespace vd::remote
