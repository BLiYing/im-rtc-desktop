#include "imrtc/CallMachine.h"

#include <utility>

#include "imrtc/Errors.h"
#include "imrtc/Reasons.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

CallOutput startCall(const CallContext& ctx, const Json& args) {
  if (ctx.state != CallState::Idle) return invalidCallState(ctx);

  const std::string mediaType = str(args, "media_type") == "video" ? "video" : "audio";
  const bool isGroup = boolean(args, "is_group");

  Json calleeIds = Json::makeArray();
  for (const std::string& uid : strArray(args, "callee_ids")) {
    calleeIds.push(Json::make(uid));
  }

  CallContext next = ctx;
  next.state = CallState::Inviting;
  next.role = "caller";
  next.mediaType = mediaType;
  next.isGroup = isGroup;

  return callOut(next, {frameOf(frame::kCallInvite, obj({{"callee_ids", std::move(calleeIds)},
                                                         {"media_type", Json::make(mediaType)},
                                                         {"is_group", Json::make(isGroup)}}))});
}

CallOutput acceptCall(const CallContext& ctx) {
  // 第二次 accept 必须**本地**拦下，不能发上去让服务端回 1405（同 R1 的理由）。
  if (ctx.state != CallState::Ringing) return invalidCallState(ctx);
  CallContext next = ctx;
  next.state = CallState::Accepting;
  return callOut(next,
                 {frameOf(frame::kCallAccept, obj({{"call_id", Json::make(ctx.callId)}}))});
}

CallOutput inviteMore(const CallContext& ctx, const Json& args) {
  if (ctx.state != CallState::Connected && ctx.state != CallState::Connecting) {
    return invalidCallState(ctx);
  }
  Json calleeIds = Json::makeArray();
  for (const std::string& uid : strArray(args, "callee_ids")) {
    calleeIds.push(Json::make(uid));
  }
  return callOut(ctx, {frameOf(frame::kCallInviteMore,
                               obj({{"call_id", Json::make(ctx.callId)},
                                    {"callee_ids", std::move(calleeIds)}}))});
}

/**
 * joinOngoingCall 是「群成员看到『进行中』主动加入」（§4.1）。
 *
 * **「怎么知道有通话在进行中」不在本协议里**——那是宿主拿 webhook `call.started`
 * 自己发广播的事。Engine 只负责把 call_id 送上去。
 */
CallOutput joinOngoingCall(const CallContext& ctx, const Json& args) {
  if (ctx.state != CallState::Idle) return invalidCallState(ctx);
  const std::string callId = str(args, "call_id");

  CallContext next = ctx;
  next.state = CallState::Accepting;
  next.role = "callee";
  next.callId = callId;
  next.isGroup = true;

  return callOut(next, {frameOf(frame::kCallJoin, obj({{"call_id", Json::make(callId)}}))});
}

CallOutput reduceAct(const CallContext& ctx, const std::string& op, const Json& args) {
  if (op == "call") return startCall(ctx, args);
  if (op == "accept") return acceptCall(ctx);
  if (op == "reject") {
    // reject 只发帧，状态由随后的 call.ended 推进——服务端才是裁决方。
    if (ctx.state != CallState::Ringing) return invalidCallState(ctx);
    return callOut(ctx, {frameOf(frame::kCallReject, obj({{"call_id", Json::make(ctx.callId)}}))});
  }
  if (op == "cancel") {
    if (ctx.state != CallState::Inviting) return invalidCallState(ctx);
    return callOut(ctx, {frameOf(frame::kCallCancel, obj({{"call_id", Json::make(ctx.callId)}}))});
  }
  if (op == "hangup") {
    if (ctx.state != CallState::Connected && ctx.state != CallState::Connecting) {
      return invalidCallState(ctx);
    }
    return callOut(ctx, {frameOf(frame::kCallHangup, obj({{"call_id", Json::make(ctx.callId)}}))});
  }
  if (op == "invite_more") return inviteMore(ctx, args);
  if (op == "join_call") return joinOngoingCall(ctx, args);
  return invalidCallState(ctx);
}

CallOutput reduceInternal(const CallContext& ctx, const std::string& name,
                          std::int64_t nowMs) {
  // 媒体就绪：room.join.ok 到手 + sub PC 的 ICE 连通（§5.1）。
  if (name == "media_ready" && ctx.state == CallState::Connecting) {
    CallContext next = ctx;
    next.state = CallState::Connected;
    return callOut(next);
  }
  /*
    **`call.invite` 被服务端拒了要回 idle**。不退的话通话机永远停在 inviting：
    界面上是「正在呼叫…」转个不停，而那通电话服务端根本没建；随后每一次挂断都发向
    一个不存在的 call，换回 1401 call_not_found，**永远退不出去**。
    （Web 端实测：群呼把主叫自己也放进了 callee_ids，服务端回 1004，
    然后连点五次挂断全是 1401。）

    抛 onCallEnd 而不是只清状态：它是所有结束分支的唯一出口（设计 §7.5），
    界面只认这一个信号来收场子。reason 用 error——这通电话从未建立。
  */
  /*
    `reset` 是**本地拆除**：宿主主动 logout。服务端随后也会结束这通电话，
    但那条 `call.ended` 到不了我们手里（连接已经关了）——与不变量 I8 里
    「重连恢复失败」是同一种处境，所以用同一种处置：本地合成 onCallEnd，
    时长用本地计时。

    不合成不行：`onCallEnd` 是所有结束分支的唯一出口（不变量 I1），而宿主的
    通话记录**只由它拼出来**。中途登出那通电话就会从记录里凭空消失。

    **待与协议确认**：§5.1 的 I8 目前只写了「重连恢复失败」一个例外，
    logout 是第二个。要么把它写进 I8，要么给 reason 加一个值。
  */
  if (name == "reset") return synthesizeNetworkEnd(ctx, nowMs);

  if (name == "call_failed" && ctx.state != CallState::Idle) {
    return callOut(CallContext{}, {},
                   {eventOf("onCallEnd", obj({{"call_id", Json::make(ctx.callId)},
                                              {"reason", Json::make(reason::kError)},
                                              {"duration_sec", Json::make(std::int64_t{0})},
                                              {"ended_by", Json::make("")}}))});
  }
  return callOut(ctx);
}

}  // namespace

const char* callStateName(CallState state) {
  switch (state) {
    case CallState::Idle: return "idle";
    case CallState::Inviting: return "inviting";
    case CallState::Ringing: return "ringing";
    case CallState::Accepting: return "accepting";
    case CallState::Connecting: return "connecting";
    case CallState::Connected: return "connected";
  }
  return "idle";
}

bool parseCallState(const std::string& text, CallState& out) {
  const CallState kStates[] = {CallState::Idle,      CallState::Inviting,   CallState::Ringing,
                               CallState::Accepting, CallState::Connecting, CallState::Connected};
  for (const CallState state : kStates) {
    if (text == callStateName(state)) {
      out = state;
      return true;
    }
  }
  return false;
}

CallOutput callOut(CallContext state, std::vector<OutgoingFrame> send,
                   std::vector<EmittedEvent> emit) {
  return CallOutput{std::move(state), std::move(send), std::move(emit)};
}

CallOutput invalidCallState(const CallContext& ctx) {
  const std::int32_t code = codeValue(ErrorCode::InvalidState);
  return callOut(ctx, {},
                 {eventOf("onError", obj({{"code", Json::make(static_cast<std::int64_t>(code))},
                                          {"name", Json::make(errorName(code))}}))});
}

CallOutput reduceCall(const CallContext& ctx, const MachineInput& input,
                      std::int64_t nowMs) {
  switch (input.kind) {
    case MachineInput::Kind::Act: return reduceAct(ctx, input.name, input.payload);
    case MachineInput::Kind::Recv: return reduceCallRecv(ctx, input.name, input.payload);
    case MachineInput::Kind::Internal: return reduceInternal(ctx, input.name, nowMs);
  }
  return callOut(ctx);
}

CallOutput synthesizeNetworkEnd(const CallContext& ctx, std::int64_t nowMs) {
  if (ctx.state == CallState::Idle) return callOut(ctx);
  const std::int64_t durationSec = callDurationSec(ctx.connectedAtMs, nowMs);
  return callOut(CallContext{}, {},
                 {eventOf("onCallEnd", obj({{"call_id", Json::make(ctx.callId)},
                                            {"reason", Json::make(reason::kNetwork)},
                                            {"duration_sec", Json::make(durationSec)},
                                            {"ended_by", Json::make("")}}))});
}

}  // namespace imrtc
