#include "imrtc/RoomMachine.h"

#include <algorithm>
#include <utility>

#include "imrtc/Errors.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/** localReject 是不变量 R1 的落点：错误状态下的调用**本地拒绝**，不发上去。 */
RoomOutput localReject(const RoomContext& ctx) {
  const std::int32_t code = codeValue(ErrorCode::InvalidState);
  return roomOut(ctx, {},
                 {eventOf("onError", obj({{"code", Json::make(static_cast<std::int64_t>(code))},
                                          {"name", Json::make(errorName(code))}}))});
}

RoomOutput joinRoom(const RoomContext& ctx, const Json& args) {
  if (ctx.state != RoomState::Idle) return localReject(ctx);
  // auto_subscribe 默认 true——直接读 args 会把「没写」当成 false，
  // 那正是协议 §2.4 点名的发送侧陷阱。
  const Json* autoSubscribeArg = args.find("auto_subscribe");
  const bool autoSubscribe =
      autoSubscribeArg == nullptr ? true : boolean(args, "auto_subscribe");
  const std::string roomId = str(args, "room_id");
  const std::string roomToken = str(args, "room_token");

  RoomContext next = ctx;
  next.state = RoomState::Joining;
  next.roomId = roomId;
  next.roomToken = roomToken;
  next.autoSubscribe = autoSubscribe;

  return roomOut(next, {frameOf(frame::kRoomJoin,
                                obj({{"room_id", Json::make(roomId)},
                                     {"room_token", Json::make(roomToken)},
                                     {"auto_subscribe", Json::make(autoSubscribe)}}))});
}

RoomOutput publishTrack(const RoomContext& ctx, const Json& args) {
  const std::string cid = str(args, "cid");
  RoomContext next = ctx;
  next.publish[cid] = "publishing";
  return roomOut(next, {frameOf(frame::kRoomPublish,
                                obj({{"cid", Json::make(cid)},
                                     {"kind", Json::make(str(args, "kind"))},
                                     {"source", Json::make(str(args, "source"))},
                                     {"simulcast", Json::make(boolean(args, "simulcast"))}}))});
}

/** cidOfTrack 反查某条 track_id 对应的本地 cid；没有则返回空串。 */
std::string cidOfTrack(const RoomContext& ctx, const std::string& trackId) {
  for (const auto& entry : ctx.publishTrackIds) {
    if (entry.second == trackId) return entry.first;
  }
  return {};
}

RoomOutput unpublishTrack(const RoomContext& ctx, const Json& args) {
  const std::string trackId = str(args, "track_id");
  RoomContext next = ctx;
  const std::string cid = cidOfTrack(ctx, trackId);
  if (!cid.empty()) next.publish[cid] = "unpublishing";
  return roomOut(next, {frameOf(frame::kRoomUnpublish,
                                obj({{"track_id", Json::make(trackId)}}))});
}

/** layerOf 取 max_layer，缺席时用协议默认的 "m"。 */
std::string layerOf(const Json& args) {
  const std::string layer = str(args, "max_layer");
  return layer.empty() ? "m" : layer;
}

/**
 * subscribeTrack：**重复订阅等价于换层**（不变量 R3）。
 *
 * 客户端的订阅与服务端的 `track_unpublished` 天然会赛跑，所以这条路径必须幂等。
 */
RoomOutput subscribeTrack(const RoomContext& ctx, const Json& args) {
  const std::string trackId = str(args, "track_id");
  const std::string maxLayer = layerOf(args);

  RoomContext next = ctx;
  next.layers[trackId] = maxLayer;
  // **只有还活着的订阅才算「已订阅」**。表里留着一条 "unsubscribing" 也当成已订阅的话，
  // 「退订之后再订阅」会发成 room.update_layer —— 服务端根本没在给这条流，
  // 换层是个空操作，格子就一直黑着，两边都不报错。
  const auto existing = ctx.subscribe.find(trackId);
  if (existing != ctx.subscribe.end() && existing->second != "unsubscribing") {
    return roomOut(next, {frameOf(frame::kRoomUpdateLayer,
                                  obj({{"track_id", Json::make(trackId)},
                                       {"max_layer", Json::make(maxLayer)}}))});
  }
  next.subscribe[trackId] = "subscribing";
  return roomOut(next, {frameOf(frame::kRoomSubscribe,
                                obj({{"track_id", Json::make(trackId)},
                                     {"max_layer", Json::make(maxLayer)}}))});
}

RoomOutput unsubscribeTrack(const RoomContext& ctx, const Json& args) {
  const std::string trackId = str(args, "track_id");
  RoomContext next = ctx;
  // **没订阅过就不要往表里塞条目**：那条凭空出现的记账会让后续的 subscribe
  // 误判成「已订阅」。帧照发（退订是幂等的，R3），只是不记账。
  if (next.subscribe.find(trackId) != next.subscribe.end()) {
    next.subscribe[trackId] = "unsubscribing";
  }
  return roomOut(next, {frameOf(frame::kRoomUnsubscribe,
                                obj({{"track_id", Json::make(trackId)}}))});
}

RoomOutput updateLayer(const RoomContext& ctx, const Json& args) {
  const std::string trackId = str(args, "track_id");
  const std::string maxLayer = layerOf(args);
  RoomContext next = ctx;
  next.layers[trackId] = maxLayer;
  return roomOut(next, {frameOf(frame::kRoomUpdateLayer,
                                obj({{"track_id", Json::make(trackId)},
                                     {"max_layer", Json::make(maxLayer)}}))});
}

/** bufferableOps 是值得攒下来重放的操作——正好是 R1 管的那一组。 */
bool isBufferable(const std::string& op) {
  static const std::vector<std::string> kOps = {"publish",     "unpublish",   "mute",
                                                "subscribe",   "unsubscribe", "update_layer"};
  return std::find(kOps.begin(), kOps.end(), op) != kOps.end();
}

/** bufferIntent 把中间态期间的用户意图缓存起来（不变量 R2）。 */
RoomOutput bufferIntent(const RoomContext& ctx, const std::string& op, const Json& args) {
  // 不认识的 op 照旧本地拒绝：缓存的是**合法但来早了**的调用，不是笔误。
  if (!isBufferable(op)) return localReject(ctx);
  RoomContext next = ctx;
  next.buffered.push_back(BufferedIntent{op, args});
  return roomOut(next);
}

RoomOutput reduceRoomInternal(const RoomContext& ctx, const std::string& name) {
  if (name == "disconnected") {
    // 断线**不等于**离房：协议给了 30 秒恢复窗口，房内其他人这时还看得见我们。
    if (ctx.state == RoomState::Idle) return roomOut(ctx);
    /*
      **joining 不能也塌进 reconnecting**。reconnecting 的含义是「服务端那边还认我们
      是房里的人，恢复了就接着用」；而 join 还在飞的时候掉线，服务端压根没把我们加进去。
      两者混成一个状态，resumeRoom 就再也分不开，只能一律宣布 joined（见那边的注释）。

      留在 joining 不影响缓存：不变量 R2 的缓存对 joining 与 reconnecting 一视同仁。
    */
    if (ctx.state == RoomState::Joining) return roomOut(ctx);
    RoomContext next = ctx;
    next.state = RoomState::Reconnecting;
    return roomOut(next);
  }
  if (name == "ws_closed_4403" || name == "reset") {
    return roomOut(clearedRoom(RoomState::Idle));
  }
  if (name == "join_failed") {
    /*
      进房被拒（房间没了、票过期、已在房里…）。**退回 idle**，否则状态机永远停在
      joining，之后每次 publish 都被 R1 本地拒成 2005。

      **还要抛 onRoomLeft**：只清状态的话宿主什么都不知道，会议界面会一直停在
      「正在进入会议…」。房间的收场信号就是这一条。
    */
    if (ctx.state != RoomState::Joining) return roomOut(ctx);
    return roomOut(clearedRoom(RoomState::Idle), {},
                   {eventOf("onRoomLeft", obj({{"room_id", Json::make(ctx.roomId)}}))});
  }
  if (name == "leave_failed") {
    /*
      离房被拒。**照样退回 idle**——这是 join_failed 的镜像，漏掉它的代价更大。

      `room.leave` 会被拒是真事：服务端在「会话已不在房间里」时回 1203
      （两人同时离房、或房间刚被「已空，已关闭」销毁掉，都撞得上）。
      而被拒的语义恰恰是**我们已经不在房里了**，本地却还停在 leaving：
      媒体停不掉（摄像头与前台资源一直开着），再 leave 被 R1 拒成 2005，
      再 join 因为「不在 idle」也被拒——除非 logout，这台 Engine 永远进不了房。

      所以「被拒」与「leave.ok」在本地是同一个收场：归零 + onRoomLeft。
    */
    if (ctx.state != RoomState::Leaving) return roomOut(ctx);
    return roomOut(clearedRoom(RoomState::Idle), {},
                   {eventOf("onRoomLeft", obj({{"room_id", Json::make(ctx.roomId)}}))});
  }
  return roomOut(ctx);
}

}  // namespace

const char* roomStateName(RoomState state) {
  switch (state) {
    case RoomState::Idle: return "idle";
    case RoomState::Joining: return "joining";
    case RoomState::Joined: return "joined";
    case RoomState::Leaving: return "leaving";
    case RoomState::Reconnecting: return "reconnecting";
  }
  return "idle";
}

bool parseRoomState(const std::string& text, RoomState& out) {
  const RoomState kStates[] = {RoomState::Idle, RoomState::Joining, RoomState::Joined,
                               RoomState::Leaving, RoomState::Reconnecting};
  for (const RoomState state : kStates) {
    if (text == roomStateName(state)) {
      out = state;
      return true;
    }
  }
  return false;
}

RoomOutput roomOut(RoomContext state, std::vector<OutgoingFrame> send,
                   std::vector<EmittedEvent> emit) {
  return RoomOutput{std::move(state), std::move(send), std::move(emit)};
}

RoomContext clearedRoom(RoomState state) {
  RoomContext ctx;
  ctx.state = state;
  return ctx;
}

RoomOutput reduceRoomAct(const RoomContext& ctx, const std::string& op, const Json& args) {
  if (op == "join") return joinRoom(ctx, args);
  if (op == "leave") {
    if (ctx.state != RoomState::Joined) return localReject(ctx);
    RoomContext next = ctx;
    next.state = RoomState::Leaving;
    return roomOut(next, {frameOf(frame::kRoomLeave,
                                  obj({{"room_id", Json::make(ctx.roomId)}}))});
  }

  // R1：只有 joined 才允许发布/订阅类操作。
  // R2：**joining 与 reconnecting** 期间把意图缓存下来，之后重放——不是丢掉，
  //     也不是发上去。这两个状态宿主都观察不到，在它们上面报「状态非法」
  //     等于让宿主为一个内部细节买单。
  if (ctx.state == RoomState::Joining || ctx.state == RoomState::Reconnecting) {
    return bufferIntent(ctx, op, args);
  }
  if (ctx.state != RoomState::Joined) return localReject(ctx);

  if (op == "publish") return publishTrack(ctx, args);
  if (op == "unpublish") return unpublishTrack(ctx, args);
  if (op == "mute") {
    // args 里的 track_id 是**服务端分配的那个**，不是本端 cid
    // （room_fsm.json 的 join_publish_mute_leave 第 10 步钉死了这条契约）。
    // 换算是调用方的事——媒体面手里只有 cid，见 MediaPlane::setMuted。
    return roomOut(ctx, {frameOf(frame::kRoomMute,
                                 obj({{"track_id", Json::make(str(args, "track_id"))},
                                      {"muted", Json::make(boolean(args, "muted"))}}))});
  }
  if (op == "subscribe") return subscribeTrack(ctx, args);
  if (op == "unsubscribe") return unsubscribeTrack(ctx, args);
  if (op == "update_layer") return updateLayer(ctx, args);
  /*
    上行那条 PC 断了，重新 offer 一次把 ICE 打回来（媒体层已经把重启位置好了）。

    **刻意不进 isBufferable**：这是「此刻网断了」的即时反应，等到重放的时候那条 PC
    早就换过一轮了，补发一个过期的重启只会白折腾一次协商。所以 joining / reconnecting
    期间它会被上面那道 R2 的分支挡下来（缓存不了 → localReject 2005）——
    这**不是漏洞而是设计**，会话恢复之后由门面重新发起一次（见 CallEngine 里
    hello.ok 的 resumed 分支），那一次房间已经回到 joined。
  */
  if (op == "restart_pub_ice") {
    return roomOut(ctx, {frameOf(frame::kRoomOffer, obj({{"pc", Json::make(std::string("pub"))},
                                                         {"sdp", Json::make(std::string())}}))});
  }
  return localReject(ctx);
}

RoomOutput reduceRoom(const RoomContext& ctx, const MachineInput& input) {
  switch (input.kind) {
    case MachineInput::Kind::Act: return reduceRoomAct(ctx, input.name, input.payload);
    case MachineInput::Kind::Recv: return reduceRoomRecv(ctx, input.name, input.payload);
    case MachineInput::Kind::Internal: return reduceRoomInternal(ctx, input.name);
  }
  return roomOut(ctx);
}

RoomOutput replayBuffered(const RoomContext& ctx) {
  if (ctx.buffered.empty()) return roomOut(ctx);

  RoomContext state = ctx;
  state.buffered.clear();
  std::vector<OutgoingFrame> send;
  std::vector<EmittedEvent> emit;
  for (const BufferedIntent& intent : ctx.buffered) {
    RoomOutput result = reduceRoomAct(state, intent.op, intent.args);
    state = std::move(result.state);
    send.insert(send.end(), result.send.begin(), result.send.end());
    emit.insert(emit.end(), result.emit.begin(), result.emit.end());
  }
  return roomOut(std::move(state), std::move(send), std::move(emit));
}

RoomOutput resumeRoom(const RoomContext& ctx, bool resumed) {
  if (!resumed) return roomOut(clearedRoom(RoomState::Idle));

  /*
    **掉线时 join 还在飞的那一路要重新 join，不能直接宣布 joined。**

    `resumed=true` 说的是「会话还在」，不是「成员关系还在」——join 没走完，服务端
    从来没把我们加进这个房间。而在途的那条 `room.join` 已经被断线连带失败掉了
    （门面对 2003 是刻意放过的，见 CallEngine::onRequestFailed），没有任何人会再管它。

    照着 joined 走下去的后果：participant_id 是空的，之后每一帧都发向一个我们并不在
    其中的房间（服务端回 1201），而重新 join 又会被 joinRoom 的「必须在 idle」挡回来，
    这个房间**再也出不去**，只能 logout。
  */
  if (ctx.state == RoomState::Joining) {
    RoomContext retry = ctx;
    retry.state = RoomState::Idle;
    // 走正常的 join 路径：状态、帧、以及攒下的意图全都跟着走一遍。
    return reduceRoomAct(retry, "join",
                         obj({{"room_id", Json::make(ctx.roomId)},
                              {"room_token", Json::make(ctx.roomToken)},
                              {"auto_subscribe", Json::make(ctx.autoSubscribe)}}));
  }

  if (ctx.state != RoomState::Reconnecting) return roomOut(ctx);
  RoomContext next = ctx;
  next.state = RoomState::Joined;
  return replayBuffered(next);
}

}  // namespace imrtc
