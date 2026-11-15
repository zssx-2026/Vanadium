#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "vanadium/core.h"
#include "vanadium/remote.h"
#include "winhttp_util.h"

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

bool download_segment(Job& job, const Segment& seg, const char* phase) {
  HttpOptions opt;
  opt.method = "GET";
  opt.timeout_ms = 120000;
  opt.retries = 0; /* retried by this loop so we can count them */
  opt.headers.emplace_back("Range", "bytes=" + std::to_string(seg.from) + "-" +
                                        std::to_string(seg.to));
  opt.headers.emplace_back("Accept-Encoding", "identity");

  for (int attempt = 0; attempt <= kSegmentRetries; ++attempt) {
    if (job.abort.load()) return false;
    try {
      u64 written = detail::stream_to_file(job.url, opt, seg.path, [&](u64 done, u64) {
        if (job.ui && job.ui->cancelled()) job.abort = true;
        u64 overall = job.completed.load() + done;
        if (job.ui) {
          job.ui->progress(phase, static_cast<long long>(done),
                           static_cast<long long>(seg.size()),
                           static_cast<long long>(overall), 0);
        }
        emit_progress(job, overall);
      });
      if (written != seg.size()) {
        throw Failure(1, "short segment",
                      "wanted " + std::to_string(seg.size()) + " got " +
                          std::to_string(written));
      }
      job.completed += written;
      return true;
    } catch (const Failure&) {
      ++job.retries;
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
      if (!ok && job.error.empty()) {
        job.error = "segment " + std::to_string(seg.from) + "-" + std::to_string(seg.to) +
                    " failed";
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
  HttpOptions opt;
  opt.method = "GET";
  opt.timeout_ms = 600000;
  opt.retries = 0;
  std::string tmp = job.out_path + ".tmp";
  u64 written = 0;
  for (int attempt = 0; attempt <= kSegmentRetries; ++attempt) {
    try {
      written = detail::stream_to_file(job.url, opt, tmp, [&](u64 done, u64) {
        if (job.ui && job.ui->cancelled()) job.abort = true;
        if (job.ui) {
          job.ui->progress("download", static_cast<long long>(done),
                           static_cast<long long>(job.total), static_cast<long long>(done), 0);
        }
      });
      if (job.total != 0 && written != job.total) {
        throw Failure(1, "short transfer", std::to_string(written) + " of " +
                                                std::to_string(job.total));
      }
      break;
    } catch (const Failure&) {
      ++job.retries;
      if (job.abort.load() || attempt == kSegmentRetries) {
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
    if (ui) ui->status("download", "probing " + url);

    detail::ProbeInfo info = detail::probe_remote(url, 60000);
    job.total = info.total;

    int wanted = threads <= 0 ? kDefaultThreads : threads;
    if (wanted > kMaxThreads) wanted = kMaxThreads;

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
      if (ui) ui->error(1, "download failed: " + job.error);
      return false;
    }
    if (!merge_segments(job)) {
      cleanup(job);
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
    if (ui) ui->error(f.code, f.message);
    return false;
  }
}

}  // namespace vd::remote
