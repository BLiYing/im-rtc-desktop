#pragma once

/**
 * CallEngineTypes.hpp —— `CallEngine.hpp` 用到的值类型（`Error` / `Result` / `Speaker` / `Quality` / `CallOptions`）。
 *
 * 从 `CallEngine.hpp` 拆出来是体量红线（CONVENTIONS §3，600 行）：包装头贴线了，而这几个类型
 * 本来就是独立的一块——只描述数据，不碰句柄。**宿主照旧只 include `CallEngine.hpp`**，它会带上这个头。
 * 与它一样只依赖 `imrtc_c.h`，一行 engine 内部的东西都不碰。
 */

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/imrtc_c.h"

namespace imrtc {
namespace capi {

/** Error 是 C 错误码的一层薄封装，`ok()` 为真表示成功。 */
class Error {
public:
  Error() = default;
  explicit Error(std::int32_t code) : code_(code) {}

  bool ok() const { return code_ == IMRTC_V1_OK; }
  std::int32_t code() const { return code_; }
  /** name 返回机读名。指向库内的静态字符串，**不要释放**。 */
  const char* name() const { return imrtc_v1_error_name(code_); }

private:
  std::int32_t code_ = IMRTC_V1_OK;
};

/**
 * Result 是一次发起类调用的结果，对应 `imrtc_v1_result_cb`（2.0.0）。`ok()` 为真时 `value` 是成功值
 * （`call` / `callEx` 是 callId，`login` 是 sessionId）。失败的码**不会**再经 `Observer::onError` 抛一遍。
 */
template <typename T = void>
struct Result {
  std::int32_t code = IMRTC_V1_OK;
  std::string name;
  T value{};
  bool ok() const { return code == IMRTC_V1_OK; }
};

template <>
struct Result<void> {
  std::int32_t code = IMRTC_V1_OK;
  std::string name;
  bool ok() const { return code == IMRTC_V1_OK; }
};

/** 一个正在说话的人，对应 `imrtc_v1_speaker`。volume 0~100。 */
struct Speaker {
  std::string uid;
  std::string participantId;
  std::int64_t volume = 0;
};

/** 一个人的网络质量，对应 `imrtc_v1_quality`。level 0~6（0 = unknown）。 */
struct Quality {
  std::string uid;
  std::string participantId;
  std::int64_t level = 0;
};

/**
 * `call()` 的可选参数，对应 `imrtc_v1_call_options`（HOST_INTEGRATION_DESIGN §3.3）。
 *
 * 群号 / user_data 本地校验不过时（chatGroupId 超 64 字节/含空白换行、userData 超
 * 4096 字节），`callEx` 的返回值仍是成功——**错误从结果回调出**（先 `onCallEnd(error)`，
 * 再 `done` 收到 1004），不上线路。跟 `imrtc_v1_call` 的「登录状态错误也从回调出」
 * 是同一条约定：C ABI 的返回值只报「这次调用本身合不合法」（空指针、struct_size），
 * 不报「这条业务规则通不通过」。
 */
struct CallOptions {
  std::string chatGroupId;
  std::string userData;
  /** 0 = 使用协议默认值（30）。 */
  std::int64_t timeoutSec = 0;
};

/** 通话记录里的一位成员，对应 `imrtc_v1_call_member`。 */
struct CallHistoryMember {
  std::string uid;
  std::string state;
};

/**
 * 一条通话记录，对应 `imrtc_v1_call_record`。`reason` 是通话的最终结局，**不分角色**：
 * 要显示「已取消」还是「对方已取消」，用 `caller` 与自己的 uid 比出角色再定文案。
 */
struct CallHistoryRecord {
  std::string callId;
  std::string roomId;
  std::string caller;
  std::string mediaType;  ///< "audio" / "video"
  bool isGroup = false;
  std::string reason;
  std::string endedBy;
  std::int64_t durationSec = 0;
  std::int64_t startedAtMs = 0;
  std::int64_t connectedAtMs = 0;
  std::int64_t endedAtMs = 0;
  std::string userData;
  std::string chatGroupId;
  std::vector<CallHistoryMember> members;
};

/** 一页通话记录（按发起时间倒序）。`nextCursor == 0` 表示已经到底。 */
struct CallHistoryPage {
  std::vector<CallHistoryRecord> records;
  std::int64_t nextCursor = 0;

  bool hasNext() const { return nextCursor != 0; }
};

}  // namespace capi
}  // namespace imrtc
