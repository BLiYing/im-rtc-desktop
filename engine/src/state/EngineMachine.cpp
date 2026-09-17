#include "imrtc/EngineMachine.h"

#include "imrtc/Log.h"

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
  // **加房间层的 op 时这里也要加**：漏了的话它连房间机都到不了，
  // 被当成未知动作本地拒掉，而症状是「什么都没发生」——没有帧、没有错、没有日志。
  static const std::vector<std::string> kOps = {
      "join",        "leave",        "publish",     "unpublish",      "mute",
      "subscribe",   "unsubscribe",  "update_layer", "restart_pub_ice"};
  return std::find(kOps.begin(), kOps.end(), op) != kOps.end();
}

/**
 * isRoomInternal 认出**只归房间机**的内部事件。
 *
 * 前四条是 `CallEngine::failLocally` 把「房间帧没送到」翻译过来的回滚
 * （静默失败审计 §A 加了 publish_failed / subscribe_failed 两条）；
 * 最后一条是会议房翻页退订的五秒迟滞到点（RoomPaging.h）。
 * **不显式路由的话它们会落到通话机去，被静默丢掉**——症状分别是
 * 「房间永远停在 joining」和「翻走的人五秒后没退订，订阅位一直占着」。
 */
bool isRoomInternal(const std::string& name) {
  static const std::vector<std::string> kNames = {"join_failed", "leave_failed", "publish_failed",
                                                  "subscribe_failed",
                                                  "unsubscribe_hysteresis_elapsed"};
  return std::find(kNames.begin(), kNames.end(), name) != kNames.end();
}

bool hasEvent(const std::vector<EmittedEvent>& emit, const char* cb) {
  return std::any_of(emit.begin(), emit.end(),
                     [cb](const EmittedEvent& event) { return event.cb == cb; });
}

EngineOutput liftRoom(const EngineContext& ctx, RoomOutput result) {
  EngineContext next = ctx;
  next.room = std::move(result.state);
  return EngineOutput{std::move(next), std::move(result.send), std::move(result.emit),
                      std::move(result.reject)};
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
  // 本地拒绝原样带到 engine 层——漏带的话调用方拿不到结果。
  return EngineOutput{std::move(next), std::move(send), std::move(emit), std::move(result.reject)};
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
  return EngineOutput{std::move(next), std::move(send), std::move(emit), LocalReject{}};
}

EngineOutput handleInternal(const EngineContext& ctx, const MachineInput& input,
                            std::int64_t nowMs) {
  const std::string& name = input.name;
  if (name == "reset") {
    // 宿主主动 logout：房间直接清空，通话本地合成一条 onCallEnd（见 CallMachine 的注释）。
    CallOutput call = reduceCall(ctx.call, input, nowMs);
    EngineContext next;
    next.room = clearedRoom(RoomState::Idle);
    next.call = std::move(call.state);
    return EngineOutput{std::move(next), {}, std::move(call.emit), LocalReject{}};
  }

  /*
    **服务端那一侧已经不可能再恢复这条会话了**（§1.4 的恢复窗口过了）。

    语义与「重连上了但 resumed=false」完全一样，所以走同一段代码：房间归零、
    通话本地合成一条 ended{network}。差别只在**不必等重连成功**——
    网络一直不回来的话那一刻永远不会到，界面就永远停在「正在重连」、
    连挂断都点不动（挂断只产出一帧发不出去的 call.hangup，本地状态按 §4.2 铁律 1
    一动不动）。真机 2026-09-08 的 iOS 端就是这一幕，四端同形。

    「什么时候算过了窗口」由连接层算（只有它知道心跳周期），见 Connection 的
    giveUpDelayMs：上界是 3×ping + 30s，**不是恢复窗口那 30 秒**。
  */
  if (name == "session_unrecoverable") {
    RoomOutput room = resumeRoom(ctx.room, false);
    std::vector<EmittedEvent> emit = std::move(room.emit);
    CallContext call = ctx.call;
    if (ctx.call.state != CallState::Idle) {
      CallOutput synthesized = synthesizeNetworkEnd(ctx.call, nowMs);
      call = std::move(synthesized.state);
      emit.insert(emit.end(), synthesized.emit.begin(), synthesized.emit.end());
    }
    EngineContext next;
    next.room = std::move(room.state);
    next.call = std::move(call);
    return EngineOutput{std::move(next), std::move(room.send), std::move(emit), LocalReject{}};
  }
  if (name == "ws_closed_4403") {
    // 被踢：什么都不留。重连没有意义——那等于跟另一台设备打架。
    EngineContext next;
    next.room = clearedRoom(RoomState::Idle);
    // 不带关闭码：这个内部事件也被「鉴权连续失败」复用，那时真实关闭码是 4401。
    return EngineOutput{std::move(next),
                        {},
                        {eventOf("onKickedOut", Json::makeObject()),
                         eventOf("onDisconnected", Json::makeObject())},
                        LocalReject{}};
  }
  if (name == "disconnected") {
    RoomOutput room = reduceRoom(ctx.room, MachineInput::internal(name));
    std::vector<EmittedEvent> emit = {eventOf("onDisconnected", Json::makeObject())};
    emit.insert(emit.end(), room.emit.begin(), room.emit.end());
    EngineContext next = ctx;
    next.room = std::move(room.state);
    return EngineOutput{std::move(next), {}, std::move(emit), LocalReject{}};
  }
  if (name == "call_failed") {
    // 交给通话机回 idle；它抛的 onCallEnd 会顺带把房间也清掉（见 liftCall）。
    // call_failed 现在也被 room.publish 通话中被拒这条路复用（CallEngine::failLocally），
    // 通话机会按此刻状态挑该发的结束帧（CallMachine.cpp 的注释）。
    return liftCall(ctx, reduceCall(ctx.call, input, nowMs));
  }
  if (isRoomInternal(name)) {
    return liftRoom(ctx, reduceRoom(ctx.room, input));
  }
  // 其余内部事件（media_ready）交给通话机。
  return liftCall(ctx, reduceCall(ctx.call, input, nowMs));
}

}  // namespace

EngineOutput reduceEngine(const EngineContext& ctx, const MachineInput& input,
                          std::int64_t nowMs) {
  if (input.kind == MachineInput::Kind::Recv && input.name == frame::kHelloOk) {
    return handleHelloOk(ctx, input.payload, nowMs);
  }
  if (input.kind == MachineInput::Kind::Internal) return handleInternal(ctx, input, nowMs);

  if (input.kind == MachineInput::Kind::Recv) {
    if (startsWith(input.name, "call.")) return liftCall(ctx, reduceCall(ctx.call, input, nowMs));
    if (startsWith(input.name, "room.")) return liftRoom(ctx, reduceRoom(ctx.room, input));
    return EngineOutput{ctx, {}, {}, LocalReject{}};
  }

  if (isCallAct(input.name)) return liftCall(ctx, reduceCall(ctx.call, input, nowMs));
  if (isRoomAct(input.name)) return liftRoom(ctx, reduceRoom(ctx.room, input));
  /*
    两张表都不认的动作**在这里被静默丢掉**：没有帧、没有回调、没有状态变化。

    加房间层 op 时忘了往 isRoomAct 里加一行就是这个下场，而症状是「什么都没发生」——
    最难查的那一类。（`restart_pub_ice` 落地时就正好踩了一次。）
    留一条 warn，让下一个人一眼看见，而不是去单步状态机。
  */
  log(LogLevel::Warn, "未知动作，已本地丢弃", {{"op", input.name}});
  return EngineOutput{ctx, {}, {}, LocalReject{}};
}

}  // namespace imrtc
