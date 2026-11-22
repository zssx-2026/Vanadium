#pragma once
/* Download scheduling primitives taken from the observed behaviour of Internet
   Download Manager (see docs/IDM-NOTES.md): an even initial split that grows by
   halving the largest chunk while it is still worth a new connection, a bounded
   thread count, the retry ladder 250/500/1000/2000/4000 ms with a status based
   retry policy, resume from the bytes already on disk, and an aggregate byte
   rate limiter. */
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "vanadium/core.h"

namespace vd::remote::net {

struct Segment {
  int index = 0;
  u64 from = 0;
  u64 to = 0; /* inclusive */
  u64 size() const { return to >= from ? to - from + 1 : 0; }
};

/* Even split of total into at most threads parts, never smaller than
   min_segment. Mirrors IDM's ChunksList::CreateNewItem initial layout. */
std::vector<Segment> plan_segments(u64 total, int threads, u64 min_segment);

/* Halves the largest segment while it can still produce two useful parts.
   Mirrors ChunksList::FindMaxFreeChunk + NeedNewConnection: a finished worker
   takes work from the biggest remaining chunk instead of opening a new
   connection per file. Returns false when nothing can be split. */
bool split_largest(std::vector<Segment>& segments, u64 min_segment);

/* Connection budget for one object: bounded by the configured ceiling and by
   the bandwidth measured so far (IDM keeps MaxConnectionsNumber as a ceiling,
   not a target). bytes_per_second == 0 means "not measured yet". */
int choose_threads(u64 total, u64 bytes_per_second, int max_threads);

/* Bytes already present in a .part file, used as the Range start on resume. */
u64 resume_offset(const std::string& part_path);

/* HTTP status codes worth retrying: 408, 429 and 5xx. 403/404/416 are final. */
bool status_retryable(int status);

/* Transport failures worth retrying: 12002 timeout, 12029 cannot connect,
   12030 connection aborted. */
bool error_retryable(unsigned long code);

/* Retry ladder in milliseconds: 250, 500, 1000, 2000, 4000, then capped. */
int backoff_ms(int attempt);

/* Sliding window throughput estimate used to size the connection budget. */
class BandwidthMeter {
 public:
  void record(std::size_t bytes);
  u64 bytes_per_second() const;
  void reset();

 private:
  mutable std::mutex mutex_;
  i64 window_start_ms_ = 0;
  u64 window_bytes_ = 0;
  double rate_ = 0;
};

/* Token bucket shared by every thread so the limit is aggregate, matching
   IDM's speed limiter (registry psnSpeedLimit / bUseSpeedLimiter). */
class RateLimiter {
 public:
  void set_limit(u64 bytes_per_second); /* 0 disables the limiter */
  u64 limit() const;
  void acquire(std::size_t bytes);

 private:
  mutable std::mutex mutex_;
  u64 limit_ = 0;
  double tokens_ = 0;
  i64 last_ms_ = 0;
};

/* Process wide limiter driven by INV_HTTP_LIMIT_BPS (bytes per second). */
void throttle(std::size_t bytes);
void set_rate_limit(u64 bytes_per_second);
u64 rate_limit();

}  // namespace vd::remote::net
