#include "imrtc/Heartbeat.h"

#include <algorithm>

namespace imrtc {

void Heartbeat::start(std::int64_t intervalSec, std::int64_t nowMs) {
  running_ = true;
  // 服务端下发 0 或负数时兜底成 1 秒——不兜底的话下面会变成「每次 tick 都发 ping」。
  intervalMs_ = std::max<std::int64_t>(1, intervalSec) * 1000;
  nextPingAtMs_ = nowMs + intervalMs_;
  missed_ = 0;
}

void Heartbeat::stop() {
  running_ = false;
  missed_ = 0;
  nextPingAtMs_ = 0;
}

void Heartbeat::noteFrameReceived() { missed_ = 0; }

Heartbeat::Action Heartbeat::tick(std::int64_t nowMs) {
  if (!running_ || nowMs < nextPingAtMs_) return Action::None;

  nextPingAtMs_ = nowMs + intervalMs_;
  ++missed_;
  if (missed_ > kMissLimit) {
    running_ = false;
    return Action::Dead;
  }
  return Action::SendPing;
}

}  // namespace imrtc
