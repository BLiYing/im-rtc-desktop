#pragma once

#include <cstdint>

namespace imrtc {

/**
 * 心跳（RTC_PROTOCOL.md §1.3）。
 *
 * **判活条件是「收到对端任何一帧」，不是「pong 回来了」**——服务端发的任何帧
 * 都证明它还活着。所以 `noteFrameReceived()` 由读循环无差别调用。
 *
 * **它不持有定时器**：`tick(nowMs)` 由外面驱动，时间从外面喂。这样「连续 3 个周期
 * 没动静就判死」这条规则能在不等 45 秒的情况下被测到。
 */
class Heartbeat {
public:
  /** kMissLimit 是判死前允许连续静默的周期数（§1.3：3 个周期 = 45 秒）。 */
  static constexpr int kMissLimit = 3;

  /** Action 是 tick 的结论。 */
  enum class Action {
    /** 什么都不用做。 */
    None,
    /** 该发一个 sys.ping 了。 */
    SendPing,
    /** 连续静默超限，连接该判死了。 */
    Dead,
  };

  /** start 按服务端下发的间隔起心跳。重复调用会重置计数。 */
  void start(std::int64_t intervalSec, std::int64_t nowMs);

  /** stop 停掉心跳。幂等。 */
  void stop();

  /** noteFrameReceived 由读循环无差别调用：收到任何帧都算对端活着。 */
  void noteFrameReceived();

  /** tick 推进到 nowMs，返回该做什么。没到下一个周期就返回 None。 */
  Action tick(std::int64_t nowMs);

  /** running 报告心跳是否在跑。 */
  bool running() const { return running_; }
  /** missedBeats 供测试与诊断观察。 */
  int missedBeats() const { return missed_; }
  /** nextPingAtMs 供测试与诊断观察。 */
  std::int64_t nextPingAtMs() const { return nextPingAtMs_; }

private:
  bool running_ = false;
  std::int64_t intervalMs_ = 0;
  std::int64_t nextPingAtMs_ = 0;
  int missed_ = 0;
};

}  // namespace imrtc
