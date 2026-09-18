#include <algorithm>

#include "imrtc/Connection.h"
#include "imrtc/Log.h"
#include "imrtc/Registry.h"

/*
  回前台、系统网络变了：**不再按退避白等**（2026-09-18，与 iOS / Android / Web 同形，CLIENT_PARITY `[^netchange]`）。

  20:45 真机 OPPO：Wi-Fi 自己断开重连换了 IP，信令退避正在 30 秒那一档空等，服务端 30 秒恢复窗口
  先到期，通话被结束。桌面上的对应场景是**合盖睡眠 / 唤醒**与换网：唤醒时 socket 早死了，
  而心跳要 45 秒才判死、退避可能在 30 秒一档。

  三种处境三种做法：

  - **正等着重连**：退避归零，**下一个 tick** 就连（不在这里同步连——这个函数也会从
    `onTransportClosed` 经 `scheduleReconnect` 走到，而 `connect()` 会放掉当前 Transport，
    绝不能在它自己的关闭回调里析构它）。
  - **连着**：发 ping 探 `kProbeMs`，到期没收到任何下行就判死，走心跳判死同一条路（主动关）。
  - **正在连**：让这次跑完；失败了不走退避，立刻再连（`nudgePending_`）。

  两次「立刻重连」之间至少隔 `kNudgeMinGapMs`：网络来回跳时不刷出重连风暴。
  「前台」对桌面意义不大（窗口最小化不影响进程），宿主把**唤醒**也当回前台喂进来就好。

  拆成单独文件是体量：`Connection.cpp` 管握手 / 收发 / 断线，这里只管「什么时候提前重连」。
*/

namespace imrtc {

void Connection::appForeground(bool foreground, std::int64_t nowMs) {
  nowMs_ = nowMs;
  log(LogLevel::Info, foreground ? "App 切到前台" : "App 切到后台",
      {{"state", connectionStateName(state_)}});
  if (foreground) nudge("回前台", nowMs);
}

void Connection::networkChanged(std::int64_t nowMs) {
  nowMs_ = nowMs;
  log(LogLevel::Info, "系统网络变了",
      {{"state", connectionStateName(state_)}, {"waiting", reconnectAtMs_ != 0 ? "true" : "false"}});
  nudge("网络变化", nowMs);
}

void Connection::nudge(const char* rule, std::int64_t nowMs) {
  if (reconnectStopped_) return;
  switch (state_) {
    case ConnectionState::Idle:
    case ConnectionState::Closed:
      return;
    case ConnectionState::Connected:
      if (probeDeadlineMs_ != 0) return;  // 已经在探，一次只探一个
      probeAnswered_ = false;
      probeDeadlineMs_ = nowMs + kProbeMs;
      sendFrame(frame::kPing, nextReqId(), Json::makeObject(), nowMs);
      return;
    case ConnectionState::Connecting:
    case ConnectionState::Reconnecting:
      if (reconnectAtMs_ != 0) {
        reconnectRightAway(rule, nowMs);
      } else {
        nudgePending_ = true;
      }
      return;
  }
}

void Connection::reconnectRightAway(const char* rule, std::int64_t nowMs) {
  nudgePending_ = false;
  reconnectAttempt_ = 0;
  const std::int64_t earliest = hasNudged_ ? lastNudgeAtMs_ + kNudgeMinGapMs : nowMs;
  const std::int64_t at = std::max(nowMs, earliest);
  // reconnectAtMs_ 的 0 表示「没排」，所以至少排到 1。
  reconnectAtMs_ = std::max<std::int64_t>(at, 1);
  lastNudgeAtMs_ = reconnectAtMs_;
  hasNudged_ = true;
  log(LogLevel::Info, "计划重连",
      {{"attempt", "0"}, {"delay_ms", std::to_string(at - nowMs)}, {"rule", rule}});
}

void Connection::checkProbe(std::int64_t nowMs) {
  if (probeDeadlineMs_ == 0 || nowMs < probeDeadlineMs_) return;
  probeDeadlineMs_ = 0;
  if (probeAnswered_ || state_ != ConnectionState::Connected) return;
  log(LogLevel::Warn, "旧连接判死", {{"why", "探测 3000ms 没收到下行"}});
  nudgePending_ = true;
  // 与心跳判死同一条路：主动关，让关闭码的那套规则统一决定重不重连。
  if (transport_) transport_->close(closecode::kGoingAway, "probe timeout");
}

}  // namespace imrtc
