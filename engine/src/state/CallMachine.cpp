#include "imrtc/CallMachine.h"

#include <cctype>
#include <utility>

#include "imrtc/Enums.h"
#include "imrtc/Errors.h"
#include "imrtc/Reasons.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/** hasWhitespace 认协议里「禁止空白与换行」的判据（`uid` / `chat_group_id` 共用的规矩）。 */
bool hasWhitespace(const std::string& text) {
  for (const char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch))) return true;
  }
  return false;
}

/** chatGroupIdValid 按协议 §2.6：≤64 字节、禁止空白与换行；空串合法（表示没有群号）。 */
bool chatGroupIdValid(const std::string& value) {
  return value.size() <= kChatGroupIdMaxBytes && !hasWhitespace(value);
}

/** userDataValid 按协议 §2.6：≤4096 字节。 */
bool userDataValid(const std::string& value) { return value.size() <= kUserDataMaxBytes; }

/**
 * localCallRejected 是 `call()` 参数本地不合规时的统一出口。
 *
 * **与「callee_ids 里有自己」同一个出口**（HOST_INTEGRATION_DESIGN §3.3，那条在门面
 * `CallEngine::call` 里拦）：本地拒绝 1004 回给调用方 + `onCallEnd(error)` 给界面一个收场信号，
 * 不转移状态、不发生任何帧——这通电话从未上过线路，state 原地不动。
 * 1004 **不再**经 onError 广播（ACTION_RESULT_DESIGN R3）。
 */
CallOutput localCallRejected(const CallContext& ctx) {
  const std::int32_t code = codeValue(ErrorCode::BadParams);
  CallOutput out = callOut(ctx, {},
                           {eventOf("onCallEnd", obj({{"call_id", Json::make(ctx.callId)},
                                                      {"reason", Json::make(reason::kError)},
                                                      {"duration_sec", Json::make(std::int64_t{0})},
                                                      {"ended_by", Json::make(std::string())}}))});
  out.reject = LocalReject{code, errorName(code)};
  return out;
}

CallOutput startCall(const CallContext& ctx, const Json& args) {
  if (ctx.state != CallState::Idle) return invalidCallState(ctx);

  const std::string mediaType = str(args, "media_type") == "video" ? "video" : "audio";
  const bool isGroup = boolean(args, "is_group");
  const std::string chatGroupId = str(args, "chat_group_id");
  const std::string userData = str(args, "user_data");
  const std::int64_t timeoutSec = num(args, "timeout_sec");

  /*
    本地先拦（HOST_INTEGRATION_DESIGN §3.3）：chat_group_id 超 64 字节或含空白/换行、
    user_data 超 4096 字节，不上线路。
  */
  if (!chatGroupIdValid(chatGroupId) || !userDataValid(userData)) {
    return localCallRejected(ctx);
  }

  Json calleeIds = Json::makeArray();
  for (const std::string& uid : strArray(args, "callee_ids")) {
    calleeIds.push(Json::make(uid));
  }

  CallContext next = ctx;
  next.state = CallState::Inviting;
  next.role = "caller";
  next.mediaType = mediaType;
  next.isGroup = isGroup;
  // 记下来供 handleConnected 回落——call.connected 缺席这两个字段时用得上（兼容旧服务端）。
  next.chatGroupId = chatGroupId;
  next.userData = userData;
  {
    const std::vector<std::string> callees = strArray(args, "callee_ids");
    next.peerUid = (isGroup || callees.empty()) ? std::string() : callees.front();
  }

  Json frameData = obj({{"callee_ids", std::move(calleeIds)},
                        {"media_type", Json::make(mediaType)},
                        {"is_group", Json::make(isGroup)}});
  // 三个都是可选项：**真的省略**才能让 Connection 那层按协议默认值填
  // （room_id="" / timeout_sec=30 / user_data=""，decodeFields 在发送前兜底）。
  if (!chatGroupId.empty()) frameData.set("chat_group_id", Json::make(chatGroupId));
  if (!userData.empty()) frameData.set("user_data", Json::make(userData));
  if (timeoutSec > 0) frameData.set("timeout_sec", Json::make(timeoutSec));

  return callOut(next, {frameOf(frame::kCallInvite, std::move(frameData))});
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
    /*
      call_failed 有两条来路：
      - `call.invite` 被服务端拒了——此刻 state 恒为 inviting，没有 call_id 可发任何
        结束帧，duration 恒为 0（connectedAtMs 还是 0）。
      - `room.publish` 通话中被拒（静默失败审计 §A，`CallEngine::failLocally` 复用这条
        内部事件强制收场整通电话）：此刻可能是 connecting / connected，call_id 已经拿到，
        这时必须真的把 `call.hangup` 发出去，否则对面还在等我们挂断，界面却已经撤了。
        与 Web 端 `state/forceEnd.ts` 的 `endFrames()` 同一形状（只是这里只会撞见
        connecting/connected 两种，会议房不走这条路，通话里 publish 才要）。
    */
    std::vector<OutgoingFrame> frames;
    if ((ctx.state == CallState::Connecting || ctx.state == CallState::Connected) &&
        !ctx.callId.empty()) {
      frames.push_back(frameOf(frame::kCallHangup, obj({{"call_id", Json::make(ctx.callId)}})));
    }
    const std::int64_t durationSec = callDurationSec(ctx.connectedAtMs, nowMs);
    return callOut(CallContext{}, std::move(frames),
                   {eventOf("onCallEnd", obj({{"call_id", Json::make(ctx.callId)},
                                              {"reason", Json::make(reason::kError)},
                                              {"duration_sec", Json::make(durationSec)},
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
  return CallOutput{std::move(state), std::move(send), std::move(emit), LocalReject{}};
}

CallOutput invalidCallState(const CallContext& ctx) {
  const std::int32_t code = codeValue(ErrorCode::InvalidState);
  CallOutput out = callOut(ctx);
  out.reject = LocalReject{code, errorName(code)};
  return out;
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
