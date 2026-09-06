#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "imrtc/Envelope.h"
#include "imrtc/Json.h"

namespace imrtc {

/**
 * RequestResult 是一次请求的结局。
 *
 * 用「回调 + 结果结构体」而不是 future / 异常：C++ 侧的 Engine 是**回调驱动**的
 * （设计 §7.5），把一次请求的失败包装成异常，只会让调用方在回调线程里 try/catch，
 * 而那正是 CONVENTIONS §7 要避免的「异常跨模块边界」。
 */
struct RequestResult {
  /** true = 收到了 `<type>.ok`。 */
  bool ok = false;
  /** 失败时的错误码（服务端 sys.error 的 code，或本地 2004 / 2003）。 */
  std::int32_t errorCode = 0;
  /** 错误码的机读名，方便日志。 */
  std::string errorName;
  /** 出错的请求 type。 */
  std::string forType;
  /** 成功时的应答信封。 */
  Envelope envelope;
  /** 成功时**已按帧声明解码**的 data（线路形状 + 默认值）。 */
  Json data;
};

/** ResponseHandler 是一次请求的回调。**恰好被调用一次**（成功、失败或超时）。 */
using ResponseHandler = std::function<void(const RequestResult&)>;

/**
 * 在途请求表：**按 `req_id` 配对，不按帧类型**。
 *
 * pub 侧的 `room.offer` 是由 **`room.answer`** 应答的（协议 §3.3 固定 offerer），
 * 只看类型对不上号；按 req_id 配对还顺带解决了「多个同类请求在途」的问题。
 *
 * **它不持有定时器**——超时由 `expire(nowMs)` 驱动，时间从外面喂进来。
 * 状态机与时序逻辑都不自己读时钟，否则没法确定性地测。
 */
class PendingRequests {
public:
  /** DecodeFn 把应答信封解成线路形状的 data。由 Connection 提供。 */
  using DecodeFn = std::function<Json(const Envelope&)>;

  explicit PendingRequests(std::int64_t timeoutMs);

  /** track 登记一个在途请求。`nowMs` 用来算它的截止时刻。 */
  void track(const std::string& reqId, const std::string& type, std::int64_t nowMs,
             ResponseHandler handler);

  /** abandon 撤掉一个还没发出去就失败的请求，**不触发回调**。 */
  void abandon(const std::string& reqId);

  /**
   * settle 用一帧应答结算在途请求。
   * 返回 false 表示这个 req_id 没人在等——调用方应当把它当事件处理。
   */
  bool settle(const Envelope& envelope, const DecodeFn& decode);

  /** expire 结算所有已超时的请求，各回一个 2004 signaling_timeout。 */
  void expire(std::int64_t nowMs);

  /** failAll 在断线时把全部在途请求失败掉——它们的应答永远不会来了。 */
  void failAll(std::int32_t errorCode);

  /** size 供测试与诊断观察。 */
  std::size_t size() const { return waiters_.size(); }

private:
  struct Waiter {
    std::string type;
    std::int64_t deadlineMs = 0;
    ResponseHandler handler;
  };

  /** finish 从表里摘掉一个 waiter 并调用它的回调。**回调只会被调用一次**。 */
  void finish(const std::string& reqId, const RequestResult& result);

  std::int64_t timeoutMs_;
  /** req_id → 等待者。用有序 map：超时结算的顺序才是确定的，测试才好写。 */
  std::map<std::string, Waiter> waiters_;
};

}  // namespace imrtc
