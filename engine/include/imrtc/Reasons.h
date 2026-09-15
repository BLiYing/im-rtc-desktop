#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imrtc {

class Json;

/**
 * 通话结束原因：**一套 reason，三处共用**——`call.ended` 帧、`onCallEnd` 回调、
 * 服务端 webhook。单一真相源是 `im-rtc-server/docs/conformance/reasons.json`
 * （由 RTC_PROTOCOL.md §6 定稿）。
 */
namespace reason {
/** 已接通成员主动挂断。时长 > 0。 */
extern const char* const kHangup;
/** 主叫在接通前取消。 */
extern const char* const kCancel;
/** 被叫主动拒接。 */
extern const char* const kReject;
/** 服务端振铃超时。 */
extern const char* const kNoAnswer;
/** 被叫已在别的通话中。 */
extern const char* const kBusy;
/** 被叫无在线设备。 */
extern const char* const kOffline;
/** 本账号另一台设备接听了。 */
extern const char* const kAnsweredElsewhere;
/** 本账号另一台设备拒绝了。 */
extern const char* const kRejectedElsewhere;
/** 被主持人或管理 API 移出。 */
extern const char* const kKicked;
/** 房间被强制解散。 */
extern const char* const kRoomClosed;
/** 掉线超过 30 秒恢复窗口。 */
extern const char* const kNetwork;
/** 服务端内部错误兜底，**也是未知值的兜底**。 */
extern const char* const kError;
}  // namespace reason

/**
 * normalizeReason 把陌生的 reason 折成 "error"。
 *
 * 这条兜底是「新增枚举值不算破坏兼容」（§10）成立的前提：服务端发一个老客户端
 * 不认识的 reason 时，老客户端**必须**不崩、不把原始字符串显示给用户。
 */
std::string normalizeReason(const Json& value);
std::string normalizeReason(const std::string& value);

/**
 * groupDominantPriority 是群通话「全员都没接听时取哪个 reason」的固定优先级
 * （RTC_PROTOCOL.md §4.4 规则 3）。
 *
 * 定成有序表而不是散落的 if，是为了**五端算出同一个值**。
 */
const std::vector<std::string>& groupDominantPriority();

/**
 * dominantReason 从一组成员裁决里挑出群通话的主导 reason。
 * 没有一个落在优先级表里时返回 "no_answer"——保守地按「没人接」记。
 */
std::string dominantReason(const std::vector<std::string>& outcomes);

/**
 * EndReason 是上面那套 reason 字符串的类型化版本，供 C ABI 用（`imrtc_v1_end_reason`），
 * 与 iOS `IMCallEndReason` 同值，四端对齐。**只是同一份数据的另一种形状**——
 * 判等 / 拼日志仍然一律用 `reason::k*` 那批字符串常量，它们才是与协议、
 * 与另外三端比对的单一真相源。
 */
enum class EndReason : std::int32_t {
  Hangup = 0,
  Cancel = 1,
  Reject = 2,
  NoAnswer = 3,
  Busy = 4,
  Offline = 5,
  AnsweredElsewhere = 6,
  RejectedElsewhere = 7,
  Kicked = 8,
  RoomClosed = 9,
  Network = 10,
  Error = 11,
};

/**
 * endReasonOf 把 §6 的 reason 字符串折成类型化枚举。
 *
 * **陌生值折成 Error**——与 `normalizeReason` 同一条兜底逻辑。两者理应总是同步，
 * 这里再兜一层是防御性的：不能假设调用方一定已经过了 `normalizeReason` 那一步。
 */
EndReason endReasonOf(const std::string& reason);

/**
 * callDurationSec 按协议算通话时长：未接通恒为 0，接通则向下取整到秒。
 *
 * **正常路径下客户端不该自己算**——一律用 `call.ended` 帧里的 `duration_sec`
 * （时钟偏移）。这个函数只服务一个例外：重连恢复失败时服务端的 ended 帧送不到，
 * Engine 要本地合成 `onCallEnd(network)`（不变量 I8）。
 */
std::int64_t callDurationSec(std::int64_t connectedAtMs, std::int64_t endedAtMs);

}  // namespace imrtc
