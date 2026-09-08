#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace imrtc {

/**
 * 日志：**engine / capi / demo 的唯一日志入口**（CONVENTIONS §8）。
 *
 * 机制与取舍见 `im-rtc-server/docs/mechanism/LOGGING.md`——那份文档是五仓统一的。
 * 三条要点：
 * - **三条管道分开**：诊断日志（这里）/ 业务事件（`CallEngineObserver`）/ 质量指标
 *   （`onNetworkQuality`）。混成一坨是 RTC 项目最常见的翻车方式。
 * - **级别按「谁该被叫醒」分**，不按「有多详细」分：断线是 `warn` 不是 `error`
 *   （那是常态）；`info` 只记状态跃迁，**一次通话个位数条**。
 * - **不提供任何兼容桥接**——想打日志只有这一条路。姊妹项目 IMServer 上正是因为
 *   有桥接兜底，54 处违规才能长期无人察觉。`scripts/check-logging.sh` 守着这一条。
 */

/** LogLevel 是日志级别。数值与 web/server 同序，便于跨端比对。 */
enum class LogLevel : std::int32_t {
  Debug = 10,
  Info = 20,
  Warn = 30,
  Error = 40,
};

/** logLevelName 取小写名（"debug" / "info" / "warn" / "error"）。 */
const char* logLevelName(LogLevel level);

/**
 * 必带字段的名字，**与服务端 `internal/observability` 的常量一一对应**。
 *
 * 别写字符串字面量：`room_id` / `roomId` / `room` 三种写法同时出现在一份日志里，
 * 就没法按字段检索了。
 */
namespace logfield {
constexpr char kRequestId[] = "request_id";
constexpr char kSessionId[] = "session_id";
constexpr char kUid[] = "uid";
constexpr char kDeviceId[] = "device_id";
constexpr char kRoomId[] = "room_id";
constexpr char kCallId[] = "call_id";
constexpr char kTrackId[] = "track_id";
constexpr char kCode[] = "code";
constexpr char kType[] = "type";
}  // namespace logfield

/** LogFields 是结构化字段。值一律取字符串——跨 C ABI 只有这一种形状是稳的。 */
using LogFields = std::vector<std::pair<std::string, std::string>>;

/** LogSink 接收一条日志。宿主用它把 engine 日志接进自己的体系。 */
using LogSink = std::function<void(LogLevel level, const std::string& message,
                                   const LogFields& fields)>;

/**
 * setLogSink 装一个宿主 sink。传空则卸掉。
 *
 * **是 fan-out，不是替换**：内置的 stderr 那一路照旧。iOS 上踩过反例——
 * `IMRTCLog` 原先「装了 sink 就 return」，而 Demo 登录后装的正是回传服务端的 sink，
 * 于是 Xcode 控制台再也没有 Engine 日志；偏偏那天要查的故障**本身就是网络断了**，
 * 唯一的出口跟着一起没了。想只要一路的宿主自己在 sink 里过滤。
 */
void setLogSink(LogSink sink);

/**
 * setLogLevel 设最低输出级别。**默认 Info**——`debug` 是每帧每候选，生产默认关
 * （LOGGING.md §2）。低于它的调用**连字段都不会拼**。
 */
void setLogLevel(LogLevel level);
LogLevel logLevel();

/** log 打一条。级别不够时立刻返回。 */
void log(LogLevel level, const std::string& message, LogFields fields = {});

/*
  脱敏做成代码 + 闸门，不是规范里的一句话（LOGGING.md §4）。

  **为什么 SDP 也算敏感**：它带 ICE 候选（暴露内网拓扑与公网地址）、DTLS 指纹、
  以及 SRTP 的密钥协商材料。整条打进日志等于把一次通话的传输面摊开。
  要看完整 SDP 请用 Chrome 的 `webrtc-internals`，不要靠日志。
*/

/** redact 把凭据收成 `eyJhbG…(len=207)`。前 6 位够比对「是不是同一枚票」，不够伪造。 */
std::string redact(const std::string& secret);
/** redactSdp 把整条 SDP 收成 `sdp(lines=6, m=audio,m=video)`。 */
std::string redactSdp(const std::string& sdp);
/** redactCandidate 把候选收成 `candidate(udp/host)`。 */
std::string redactCandidate(const std::string& candidate);

}  // namespace imrtc
