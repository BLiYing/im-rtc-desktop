#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace imrtc {

/**
 * 错误码：**五仓共用同一份定义**，单一真相源是
 * `im-rtc-server/docs/conformance/error_codes.json`（由 RTC_PROTOCOL.md §7 定稿）。
 *
 * 本文件的表由那份 JSON 手工同步，`tests/conformance_test.cpp` 逐条断言两者相等——
 * 加了码却忘了同步，测试立刻挂。
 *
 * 两条硬约束：
 * - `msg` 是**英文固定短语**，五端必须完全一致；它给开发者看，
 *   **禁止直接显示给用户**，UI 文案由各端按 code 查自己的本地化表。
 * - 2xxx 段是**客户端本地错误码**，永远不会出现在线路上。
 */
enum class ErrorCode : std::int32_t {
  BadEnvelope = 1001,
  UnknownType = 1002,
  NotImplemented = 1003,
  BadParams = 1004,
  FrameTooLarge = 1005,
  ProtocolVersionUnsupported = 1006,
  RateLimited = 1007,

  TokenInvalid = 1101,
  TokenExpired = 1102,
  NotAuthenticated = 1103,
  KickedOut = 1104,
  SessionNotResumable = 1105,

  RoomNotFound = 1201,
  RoomFull = 1202,
  NotInRoom = 1203,
  AlreadyInRoom = 1204,
  RoomClosed = 1205,
  PermissionDenied = 1206,
  ParticipantNotFound = 1207,

  TrackNotFound = 1301,
  PublishDenied = 1302,
  SubscribeDenied = 1303,
  SdpInvalid = 1304,
  PcNotFound = 1305,
  LayerUnavailable = 1306,
  CodecUnsupported = 1307,

  CallNotFound = 1401,
  CallEnded = 1402,
  CalleeOffline = 1403,
  CalleeBusy = 1404,
  InvalidCallState = 1405,
  TooManyCallees = 1406,
  NotCallOwner = 1407,
  AlreadyInCall = 1408,

  Internal = 1501,
  SfuUnavailable = 1502,
  ShuttingDown = 1503,
  StoreError = 1504,

  DevicePermissionDenied = 2001,
  DeviceNotFound = 2002,
  NetworkUnreachable = 2003,
  SignalingTimeout = 2004,
  InvalidState = 2005,
  MediaNegotiationFailed = 2006,
  NotLoggedIn = 2007,
};

/** codeValue 把枚举取成线路上的整数。 */
inline std::int32_t codeValue(ErrorCode code) { return static_cast<std::int32_t>(code); }

/** ErrorDefinition 是一个错误码的全部契约面。 */
struct ErrorDefinition {
  std::int32_t code;
  /** 稳定机读名，与 code 一一对应。 */
  std::string name;
  /** 英文固定短语，五端必须完全一致。 */
  std::string msg;
  /** true = 原样重试可能成功。 */
  bool retryable;
  /** true = 客户端本地错误码（2xxx），永不上线路。 */
  bool local;
};

/** errorDefinitions 的顺序与 error_codes.json 一致，便于对读。 */
const std::vector<ErrorDefinition>& errorDefinitions();

/** lookupError 按 code 查定义；未知码返回 nullptr。 */
const ErrorDefinition* lookupError(std::int32_t code);

/** errorName 返回错误码的机读名；未知码返回 "unknown"。 */
std::string errorName(std::int32_t code);
inline std::string errorName(ErrorCode code) { return errorName(codeValue(code)); }

/** isRetryable 报告这个码是否值得原样重试；未知码保守地按不可重试处理。 */
bool isRetryable(std::int32_t code);

/** isLocalError 报告这个码是不是客户端本地码（永不上线路）。 */
bool isLocalError(std::int32_t code);

/**
 * RtcError 是 engine 内部的统一异常。
 *
 * **它不越过模块边界**（CONVENTIONS §7）：将来的 capi/ 会在 C ABI 那一层
 * 把它转成错误码返回值。`detail` 只进日志，**不面向用户展示**。
 */
class RtcError : public std::runtime_error {
public:
  explicit RtcError(ErrorCode code, std::string detail = "", std::string forType = "");

  std::int32_t code() const { return code_; }
  const std::string& name() const { return name_; }
  bool retryable() const { return retryable_; }
  /** 出错的请求 type；无对应请求时为空串。 */
  const std::string& forType() const { return forType_; }
  const std::string& detail() const { return detail_; }

private:
  std::int32_t code_;
  std::string name_;
  bool retryable_;
  std::string forType_;
  std::string detail_;
};

}  // namespace imrtc
