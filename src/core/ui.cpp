#include "vanadium/ui.h"

namespace vd {

void Emitter::line(const std::string& text) const {
  if (!cb_) return;
  vd_event ev{};
  ev.kind = VD_EV_LINE;
  ev.a = text.c_str();
  cb_(user_, &ev);
}

void Emitter::warn(const std::string& text) const {
  if (!cb_) return;
  vd_event ev{};
  ev.kind = VD_EV_WARN;
  ev.a = text.c_str();
  cb_(user_, &ev);
}

void Emitter::status(const std::string& key, const std::string& text) const {
  if (!cb_) return;
  vd_event ev{};
  ev.kind = VD_EV_STATUS;
  ev.a = key.c_str();
  ev.b = text.c_str();
  cb_(user_, &ev);
}

void Emitter::clear_status() const {
  if (!cb_) return;
  vd_event ev{};
  ev.kind = VD_EV_STATUS_CLEAR;
  cb_(user_, &ev);
}

void Emitter::progress(const std::string& phase, long long done, long long total,
                       long long bytes, long long flags) const {
  if (!cb_) return;
  vd_event ev{};
  ev.kind = VD_EV_PROGRESS;
  ev.a = phase.c_str();
  ev.i1 = done;
  ev.i2 = total;
  ev.i3 = bytes;
  ev.i4 = flags;
  cb_(user_, &ev);
}

bool Emitter::prompt(const std::string& question, const std::string& hint) const {
  if (!cb_) return false;
  vd_event ev{};
  ev.kind = VD_EV_PROMPT;
  ev.a = question.c_str();
  ev.b = hint.c_str();
  int r = cb_(user_, &ev);
  if (r == 1) return true;
  if (r < 0) cancelled_ = true;
  return false;
}

void Emitter::summary(const std::string& json) const {
  if (!cb_) return;
  vd_event ev{};
  ev.kind = VD_EV_SUMMARY;
  ev.a = json.c_str();
  cb_(user_, &ev);
}

void Emitter::title(const std::string& text) const {
  if (!cb_) return;
  vd_event ev{};
  ev.kind = VD_EV_TITLE;
  ev.a = text.c_str();
  cb_(user_, &ev);
}

void Emitter::error(int code, const std::string& text) const {
  if (!cb_) return;
  vd_event ev{};
  ev.kind = VD_EV_ERROR;
  ev.a = text.c_str();
  ev.i1 = code;
  cb_(user_, &ev);
}

}  // namespace vd
