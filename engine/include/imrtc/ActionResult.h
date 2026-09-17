#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace imrtc {

/**
 * ActionResult 是一次**宿主调用**的结果（server `docs/design/ACTION_RESULT_DESIGN.md`，2.0.0）。
 *
 * - 成功：`code == 0`，`value` 放成功值（`call` 是 `call_id`，`login` 是 `session_id`，其余为空）；
 * - 失败：`code` 是错误码（本地拒绝 2005 / 1004、服务端拒绝、2004 超时、2003 等应答期间断线、
 *   2007 没登录），`forType` 是出错的那一帧（本地拒绝时可能为空）。
 *
 * 成功只管**这次调用直接发出的那一帧**收到 `.ok`，不等引擎随后自动发的连锁帧（R2 / D1）。
 */
struct ActionResult {
  std::int32_t code = 0;
  std::string name;
  std::string forType;
  std::string value;

  bool ok() const { return code == 0; }
};

/**
 * ActionCompletion 接一次调用的结果。**恰好调一次**，在调 `tick()` / 发起调用的那个线程上，
 * 并且排在这次调用引起的状态事件（`onCallEnd` / `onRoomLeft` …）**之后**。
 *
 * **可以不传**：不传时失败退回 `onError`（R7），免得「没接回调」变成静默丢失。
 */
using ActionCompletion = std::function<void(const ActionResult&)>;

}  // namespace imrtc
