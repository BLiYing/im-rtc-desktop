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
  // `>=` 不是 `>`：协议 §1.3 说的是「连续 **3 个周期（45s）** 未收到对端任何帧即判死」。
  // 用 `>` 要攒到第 4 个周期（60s）才判，比服务端那边晚整整一个周期——
  // 那时对端早已经把连接关掉，本端还在对着空气发 ping。
  if (missed_ >= kMissLimit) {
    running_ = false;
    return Action::Dead;
  }
  return Action::SendPing;
}

}  // namespace imrtc
