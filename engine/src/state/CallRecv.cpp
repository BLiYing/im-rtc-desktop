#include <string>
#include <utility>

#include "imrtc/CallMachine.h"
#include "imrtc/Envelope.h"
#include "imrtc/Reasons.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/**
 * 通话状态机的**下行帧**分支（RTC_PROTOCOL.md §5.1 转移表的右半边）。
 *
 * 与 CallMachine.cpp 拆开是体量红线（CONVENTIONS §3）——「上行动作」与「下行帧」
 * 本来也是两组独立的关注点。
 */

/**
 * 这一帧说的是不是**别的一通电话**。
 *
 * 通话中被第三个人呼叫时，服务端判他忙线并给我们发一条 `call.ended{busy}`——
 * 那条帧的 `call_id` 是**新来那通**的。不看 call_id 的话它会被当成
 * 「当前通话结束了」：媒体面直接关掉、通话页收起，而对面还好好地显示着通话中
 * （iOS 真机日志 08:30:39 抓到过）。
 */
bool isForAnotherCall(const CallContext& ctx, const Json& data) {
  const std::string frameCallId = str(data, "call_id");
  return !ctx.callId.empty() && !frameCallId.empty() && frameCallId != ctx.callId;
}

/**
 * 别的一通电话的帧：**一律不碰当前状态**。
 *
 * 只有终态帧要露个头——那说明「有人打进来，已经被自动回了忙线」，
 * 界面据此提示一句谁来过电话。
 */
CallOutput handleForeignCall(const CallContext& ctx, const std::string& type, const Json& data) {
  if (type != frame::kCallEnded) return callOut(ctx);
  return callOut(ctx, {},
                 {eventOf("onCallMissed", obj({{"call_id", Json::make(str(data, "call_id"))},
                                               {"caller", Json::make(str(data, "caller"))},
                                               {"reason", Json::make(str(data, "reason"))}}))});
}

/**
 * handleEnded：唯一的终态处理。
 *
 * **收到 call.ended 后禁止再发 room.leave**（不变量 I6）——服务端在结束通话时
 * 已经清掉了房间成员，再发只会换回 1201 / 1203。
 */
CallOutput handleEnded(const CallContext& ctx, const Json& data) {
  if (ctx.state == CallState::Idle) return callOut(ctx);
  const Json* rawReason = data.find("reason");
  const std::string endReason =
      rawReason == nullptr ? std::string(reason::kError) : normalizeReason(*rawReason);
  return callOut(CallContext{}, {},
                 {eventOf("onCallEnd", obj({{"call_id", Json::make(str(data, "call_id"))},
                                            {"reason", Json::make(endReason)},
                                            {"duration_sec", Json::make(num(data, "duration_sec"))},
                                            {"ended_by", Json::make(str(data, "ended_by"))}}))});
}

CallOutput handleIncoming(const CallContext& ctx, const Json& data) {
  if (ctx.state != CallState::Idle) return callOut(ctx);
  const std::string mediaType = str(data, "media_type") == "video" ? "video" : "audio";

  CallContext next = ctx;
  next.state = CallState::Ringing;
  next.role = "callee";
  next.callId = str(data, "call_id");
  next.roomId = str(data, "room_id");
  next.mediaType = mediaType;
  next.isGroup = boolean(data, "is_group");

  Json calleeIds = Json::makeArray();
  for (const std::string& uid : strArray(data, "callee_ids")) {
    calleeIds.push(Json::make(uid));
  }

  return callOut(next, {},
                 {eventOf("onCallReceived",
                          obj({{"call_id", Json::make(next.callId)},
                               {"caller", Json::make(str(data, "caller"))},
                               // **原样带上**：群通话里被叫要靠它摆占位格。
                               {"callee_ids", std::move(calleeIds)},
                               {"media_type", Json::make(mediaType)},
                               {"is_group", Json::make(next.isGroup)}}))});
}

/**
 * handleConnected：拿到 room_token，抛 onCallBegin，并**立刻发 room.join**。
 *
 * onCallBegin 抛在进入 connecting 时（不是 connected）——双方在 call.connected
 * 那一刻同时开始计时。
 */
CallOutput handleConnected(const CallContext& ctx, const Json& data) {
  if (ctx.state != CallState::Inviting && ctx.state != CallState::Ringing &&
      ctx.state != CallState::Accepting) {
    return callOut(ctx);
  }
  const std::string roomId = str(data, "room_id");
  const std::string roomToken = str(data, "room_token");
  const std::string mediaType = str(data, "media_type") == "video" ? "video" : ctx.mediaType;
  const std::string callId = str(data, "call_id").empty() ? ctx.callId : str(data, "call_id");

  CallContext next = ctx;
  next.state = CallState::Connecting;
  next.callId = callId;
  next.roomId = roomId;
  next.roomToken = roomToken;
  next.mediaType = mediaType;
  next.isGroup = boolean(data, "is_group") || ctx.isGroup;
  next.connectedAtMs = num(data, "connected_at_ms");

  return callOut(
      next,
      {frameOf(frame::kRoomJoin,
               obj({{"room_id", Json::make(roomId)}, {"room_token", Json::make(roomToken)}}))},
      {eventOf("onCallBegin", obj({{"call_id", Json::make(callId)},
                                   {"room_id", Json::make(roomId)},
                                   {"media_type", Json::make(mediaType)},
                                   {"is_group", Json::make(next.isGroup)},
                                   {"role", Json::make(next.role)}}))});
}

/**
 * handleOutcome 处理某成员的裁决。
 *
 * **便利事件只在 1v1 抛**（不变量 I7）：群里一个人拒接，通话还在继续，
 * 后面并不会紧跟 onCallEnd，抛便利事件就自相矛盾了。
 */
CallOutput handleOutcome(const CallContext& ctx, const Json& data, const char* userCb,
                         const char* convenienceCb) {
  const std::string uid = str(data, "uid");
  std::vector<EmittedEvent> emit = {eventOf(userCb, obj({{"uid", Json::make(uid)}}))};
  if (!ctx.isGroup) {
    emit.push_back(eventOf(convenienceCb, obj({{"uid", Json::make(uid)}})));
  }
  return callOut(ctx, {}, std::move(emit));
}

}  // namespace

CallOutput reduceCallRecv(const CallContext& ctx, const std::string& type, const Json& data) {
  // **别的一通电话的帧一律不许碰当前这一通**（见 isForAnotherCall）。
  if (isForAnotherCall(ctx, data)) return handleForeignCall(ctx, type, data);

  // 终态帧优先：**任何非 idle 状态收到 call.ended 都直达 idle**（§5.1）。
  if (type == frame::kCallEnded) return handleEnded(ctx, data);

  // idle 下的迟到帧一律静默丢弃：不抛回调、不发帧、不报错。
  // 本地状态与服务端赛跑是正常的，客户端得容忍。
  if (ctx.state == CallState::Idle && type != frame::kCallIncoming) return callOut(ctx);

  if (type == frame::kCallIncoming) return handleIncoming(ctx, data);
  if (type == okType(frame::kCallInvite)) {
    CallContext next = ctx;
    next.callId = str(data, "call_id");
    next.roomId = str(data, "room_id");
    return callOut(next);
  }
  if (type == frame::kCallConnected) return handleConnected(ctx, data);
  if (type == frame::kCallAccepted) {
    return callOut(ctx, {}, {eventOf("onUserAccept", obj({{"uid", Json::make(str(data, "uid"))}}))});
  }
  if (type == frame::kCallRejected) {
    return handleOutcome(ctx, data, "onUserReject", "onCallRejected");
  }
  if (type == frame::kCallNoAnswer) {
    return handleOutcome(ctx, data, "onUserNoResponse", "onCallNoAnswer");
  }
  if (type == frame::kCallBusy) {
    // 忙线没有对应的 onUser* —— 被叫压根没振铃（§4.3）。
    if (ctx.isGroup) return callOut(ctx);
    return callOut(ctx, {}, {eventOf("onCallBusy", obj({{"uid", Json::make(str(data, "uid"))}}))});
  }
  if (type == frame::kCallCancelled) {
    return callOut(ctx, {},
                   {eventOf("onCallCancelled", obj({{"by", Json::make(str(data, "by"))}}))});
  }
  if (type == frame::kCallHandledElsewhere) {
    return callOut(ctx, {},
                   {eventOf("onHandledOnOtherDevice",
                            obj({{"call_id", Json::make(str(data, "call_id"))},
                                 {"action", Json::make(str(data, "action"))}}))});
  }
  // 其余（call.ringing、各种 .ok）不改状态也不抛回调。
  return callOut(ctx);
}

}  // namespace imrtc
