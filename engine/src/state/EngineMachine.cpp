#include "imrtc/EngineMachine.h"

#include <algorithm>
#include <utility>

#include "imrtc/Registry.h"

namespace imrtc {
namespace {

bool startsWith(const std::string& text, const std::string& prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

bool isCallAct(const std::string& op) {
  static const std::vector<std::string> kOps = {"call",   "accept",      "reject",   "cancel",
                                                "hangup", "invite_more", "join_call"};
  return std::find(kOps.begin(), kOps.end(), op) != kOps.end();
}

bool isRoomAct(const std::string& op) {
  static const std::vector<std::string> kOps = {"join",        "leave",     "publish",
                                                "unpublish",   "mute",      "subscribe",
                                                "unsubscribe", "update_layer"};
  return std::find(kOps.begin(), kOps.end(), op) != kOps.end();
}

bool hasEvent(const std::vector<EmittedEvent>& emit, const char* cb) {
  return std::any_of(emit.begin(), emit.end(),
                     [cb](const EmittedEvent& event) { return event.cb == cb; });
}

EngineOutput liftRoom(const EngineContext& ctx, RoomOutput result) {
  EngineContext next = ctx;
  next.room = std::move(result.state);
  return EngineOutput{std::move(next), std::move(result.send), std::move(result.emit)};
}

/**
 * liftCall 把通话机的输出抬到 Engine 层，并**把 room.join 转交给房间机**。
 *
 * 不做这一步的话，房间机不知道自己正在进房，随后的 `room.join.ok` 就没人接。
 */
EngineOutput liftCall(const EngineContext& ctx, CallOutput result) {
  std::vector<OutgoingFrame> send;
  std::vector<EmittedEvent> emit = std::move(result.emit);
  RoomContext room = ctx.room;

  for (OutgoingFrame& outgoing : result.send) {
    if (outgoing.type != frame::kRoomJoin) {
      send.push_back(std::move(outgoing));
      continue;
    }
    RoomOutput joined = reduceRoomAct(
        room, "join",
        obj({{"room_id", Json::make(str(outgoing.data, "room_id"))},
             {"room_token", Json::make(str(outgoing.data, "room_token"))}}));
    room = std::move(joined.state);
    send.insert(send.end(), joined.send.begin(), joined.send.end());
    emit.insert(emit.end(), joined.emit.begin(), joined.emit.end());
  }

  /*
    通话结束 = 房间没了。服务端在发出 `call.ended` 的同时就销毁了房间（协议 §4.4），
    所以这里只是**本地归零**，不发 room.leave——那一帧只会换回一个 1201。

    也不补抛 onRoomLeft：`onCallEnd` 是所有结束分支的唯一出口（设计 §7.5），
    为同一件事抛两个回调会让宿主的记账重复。
  */
  if (hasEvent(emit, "onCallEnd")) {
    room = clearedRoom(RoomState::Idle);
  }

  EngineContext next;
  next.room = std::move(room);
  next.call = std::move(result.state);
  return EngineOutput{std::move(next), std::move(send), std::move(emit)};
}

/**
 * handleHelloOk：握手成功。`resumed=false` 时**房间与通话都要归零**——
 * 服务端那边的会话已经过期，装作还在只会让 UI 撒谎。
 */
EngineOutput handleHelloOk(const EngineContext& ctx, const Json& data, std::int64_t nowMs) {
  const bool resumed = boolean(data, "resumed");
  std::vector<EmittedEvent> emit = {
      eventOf("onConnected", obj({{"session_id", Json::make(str(data, "session_id"))},
                                  {"resumed", Json::make(resumed)}}))};

  RoomOutput room = resumeRoom(ctx.room, resumed);
  std::vector<OutgoingFrame> send = std::move(room.send);
  emit.insert(emit.end(), room.emit.begin(), room.emit.end());

  CallContext call = ctx.call;
  if (!resumed && ctx.call.state != CallState::Idle) {
    // 不变量 I8 的那个唯一例外：服务端的 call.ended 送不到，本地合成一条。
    CallOutput synthesized = synthesizeNetworkEnd(ctx.call, nowMs);
    call = std::move(synthesized.state);
    emit.insert(emit.end(), synthesized.emit.begin(), synthesized.emit.end());
  }

  EngineContext next;
  next.room = std::move(room.state);
  next.call = std::move(call);
  return EngineOutput{std::move(next), std::move(send), std::move(emit)};
}

EngineOutput handleInternal(const EngineContext& ctx, const std::string& name,
                            std::int64_t nowMs) {
  if (name == "reset") {
    // 宿主主动 logout：房间直接清空，通话本地合成一条 onCallEnd（见 CallMachine 的注释）。
    CallOutput call = reduceCall(ctx.call, MachineInput::internal(name), nowMs);
    EngineContext next;
    next.room = clearedRoom(RoomState::Idle);
    next.call = std::move(call.state);
    return EngineOutput{std::move(next), {}, std::move(call.emit)};
  }

  if (name == "ws_closed_4403") {
    // 被踢：什么都不留。重连没有意义——那等于跟另一台设备打架。
    EngineContext next;
    next.room = clearedRoom(RoomState::Idle);
    // 不带关闭码：这个内部事件也被「鉴权连续失败」复用，那时真实关闭码是 4401。
    return EngineOutput{std::move(next),
                        {},
                        {eventOf("onKickedOut", Json::makeObject()),
                         eventOf("onDisconnected", Json::makeObject())}};
  }
  if (name == "disconnected") {
    RoomOutput room = reduceRoom(ctx.room, MachineInput::internal(name));
    std::vector<EmittedEvent> emit = {eventOf("onDisconnected", Json::makeObject())};
    emit.insert(emit.end(), room.emit.begin(), room.emit.end());
    EngineContext next = ctx;
    next.room = std::move(room.state);
    return EngineOutput{std::move(next), {}, std::move(emit)};
  }
  if (name == "call_failed") {
    // 交给通话机回 idle；它抛的 onCallEnd 会顺带把房间也清掉（见 liftCall）。
    return liftCall(ctx, reduceCall(ctx.call, MachineInput::internal(name), nowMs));
  }
  if (name == "join_failed") {
    return liftRoom(ctx, reduceRoom(ctx.room, MachineInput::internal(name)));
  }
  // 其余内部事件（media_ready）交给通话机。
  return liftCall(ctx, reduceCall(ctx.call, MachineInput::internal(name), nowMs));
}

}  // namespace

EngineOutput reduceEngine(const EngineContext& ctx, const MachineInput& input,
                          std::int64_t nowMs) {
  if (input.kind == MachineInput::Kind::Recv && input.name == frame::kHelloOk) {
    return handleHelloOk(ctx, input.payload, nowMs);
  }
  if (input.kind == MachineInput::Kind::Internal) return handleInternal(ctx, input.name, nowMs);

  if (input.kind == MachineInput::Kind::Recv) {
    if (startsWith(input.name, "call.")) return liftCall(ctx, reduceCall(ctx.call, input, nowMs));
    if (startsWith(input.name, "room.")) return liftRoom(ctx, reduceRoom(ctx.room, input));
    return EngineOutput{ctx, {}, {}};
  }

  if (isCallAct(input.name)) return liftCall(ctx, reduceCall(ctx.call, input, nowMs));
  if (isRoomAct(input.name)) return liftRoom(ctx, reduceRoom(ctx.room, input));
  return EngineOutput{ctx, {}, {}};
}

}  // namespace imrtc
