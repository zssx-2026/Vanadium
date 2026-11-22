#include "sched.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "vanadium/core.h"

namespace vd::remote::net {
namespace {

constexpr int kMaxThreads = 64;

u64 env_u64(const char* name) {
  const char* raw = std::getenv(name);
  if (!raw || !*raw) return 0;
  char* end = nullptr;
  unsigned long long value = std::strtoull(raw, &end, 10);
  if (end == raw) return 0;
  return static_cast<u64>(value);
}

}  // namespace

std::vector<Segment> plan_segments(u64 total, int threads, u64 min_segment) {
  std::vector<Segment> out;
  if (total == 0) return out;
  if (threads <= 0) threads = 1;
  if (threads > kMaxThreads) threads = kMaxThreads;
  if (min_segment == 0) min_segment = 1;

  u64 max_parts = std::max<u64>(1, total / min_segment);
  u64 parts = std::min<u64>(static_cast<u64>(threads), max_parts);
  u64 base = total / parts;
  u64 from = 0;
  for (u64 i = 0; i < parts; ++i) {
    u64 size = (i + 1 == parts) ? (total - from) : base;
    Segment seg;
    seg.index = static_cast<int>(i);
    seg.from = from;
    seg.to = from + size - 1;
    out.push_back(seg);
    from += size;
  }
  return out;
}

bool split_largest(std::vector<Segment>& segments, u64 min_segment) {
  if (segments.empty()) return false;
  if (min_segment == 0) min_segment = 1;
  std::size_t best = segments.size();
  u64 best_size = 0;
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (segments[i].size() > best_size) {
      best_size = segments[i].size();
      best = i;
    }
  }
  if (best == segments.size() || best_size < min_segment * 2) return false;

  Segment tail = segments[best];
  u64 half = best_size / 2;
  segments[best].to = segments[best].from + half - 1;
  tail.from = segments[best].to + 1;
  tail.index = static_cast<int>(segments.size());
  segments.push_back(tail);
  return true;
}

int choose_threads(u64 total, u64 bytes_per_second, int max_threads) {
  if (max_threads <= 0) max_threads = 1;
  if (max_threads > kMaxThreads) max_threads = kMaxThreads;
  constexpr u64 kMinSegment = 256 * 1024;
  int by_size = static_cast<int>(std::max<u64>(1, total / kMinSegment));
  by_size = std::min(by_size, max_threads);
  if (bytes_per_second == 0) return by_size;
  /* One connection per ~512 KiB/s measured, so a slow link does not pay for
     TLS handshakes it cannot use. */
  int by_rate = static_cast<int>(bytes_per_second / (512 * 1024)) + 1;
  return std::max(1, std::min(by_size, std::min(by_rate, max_threads)));
}

u64 resume_offset(const std::string& part_path) {
  if (!fs::exists(part_path)) return 0;
  if (fs::is_dir(part_path)) return 0;
  return fs::size(part_path);
}

bool status_retryable(int status) {
  return status == 408 || status == 429 || (status >= 500 && status <= 599);
}

bool error_retryable(unsigned long code) {
  return code == 12002 || code == 12029 || code == 12030;
}

int backoff_ms(int attempt) {
  static const int kLadder[] = {250, 500, 1000, 2000, 4000};
  if (attempt < 0) attempt = 0;
  if (attempt >= 5) return 4000;
  return kLadder[attempt];
}

void BandwidthMeter::record(std::size_t bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  i64 now = now_ms();
  if (window_start_ms_ == 0) window_start_ms_ = now;
  window_bytes_ += bytes;
  i64 elapsed = now - window_start_ms_;
  if (elapsed >= 1000) {
    double sample = static_cast<double>(window_bytes_) * 1000.0 / static_cast<double>(elapsed);
    rate_ = rate_ == 0 ? sample : (rate_ * 0.6 + sample * 0.4);
    window_start_ms_ = now;
    window_bytes_ = 0;
  }
}

u64 BandwidthMeter::bytes_per_second() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<u64>(rate_);
}

void BandwidthMeter::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  window_start_ms_ = 0;
  window_bytes_ = 0;
  rate_ = 0;
}

void RateLimiter::set_limit(u64 bytes_per_second) {
  std::lock_guard<std::mutex> lock(mutex_);
  limit_ = bytes_per_second;
  tokens_ = static_cast<double>(bytes_per_second);
  last_ms_ = now_ms();
}

u64 RateLimiter::limit() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return limit_;
}

void RateLimiter::acquire(std::size_t bytes) {
  for (;;) {
    i64 wait_ms = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (limit_ == 0) return;
      i64 now = now_ms();
      if (last_ms_ == 0) {
        last_ms_ = now;
        tokens_ = static_cast<double>(limit_);
      }
      double elapsed = static_cast<double>(now - last_ms_);
      tokens_ += elapsed * static_cast<double>(limit_) / 1000.0;
      double cap = static_cast<double>(limit_) * 2.0;
      if (tokens_ > cap) tokens_ = cap;
      last_ms_ = now;
      if (tokens_ >= static_cast<double>(bytes)) {
        tokens_ -= static_cast<double>(bytes);
        return;
      }
      wait_ms = static_cast<i64>((static_cast<double>(bytes) - tokens_) * 1000.0 /
                                 static_cast<double>(limit_)) + 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max<i64>(1, wait_ms)));
  }
}

namespace {
RateLimiter& global_limiter() {
  static RateLimiter limiter;
  static std::once_flag once;
  std::call_once(once, [] { limiter.set_limit(env_u64("INV_HTTP_LIMIT_BPS")); });
  return limiter;
}
}  // namespace

void throttle(std::size_t bytes) { global_limiter().acquire(bytes); }

void set_rate_limit(u64 bytes_per_second) { global_limiter().set_limit(bytes_per_second); }

u64 rate_limit() { return global_limiter().limit(); }

}  // namespace vd::remote::net
