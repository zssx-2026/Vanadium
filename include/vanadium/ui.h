#pragma once
#include <string>
#include "vd.h"

namespace vd {

/* Emitter funnels every user-visible string through the shell. */
class Emitter {
 public:
  Emitter() = default;
  Emitter(vd_event_cb cb, void* user) : cb_(cb), user_(user) {}

  void line(const std::string& text) const;
  void warn(const std::string& text) const;
  void status(const std::string& key, const std::string& text) const;
  void clear_status() const;
  void progress(const std::string& phase, long long done, long long total,
                long long bytes, long long flags) const;
  bool prompt(const std::string& question, const std::string& hint = {}) const;
  void summary(const std::string& json) const;
  void title(const std::string& text) const;
  void error(int code, const std::string& text) const;

  bool cancelled() const { return cancelled_; }
  void set_cancelled(bool v) { cancelled_ = v; }

 private:
  vd_event_cb cb_ = nullptr;
  void* user_ = nullptr;
  mutable bool cancelled_ = false;
};

}  // namespace vd
