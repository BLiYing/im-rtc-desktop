#include <string>
#include <utility>

#include "imrtc/Envelope.h"
#include "imrtc/Registry.h"
#include "imrtc/RoomMachine.h"

namespace imrtc {
namespace {

/**
 * 房间状态机的**下行帧**分支。
 *
 * 与 RoomMachine.cpp 拆开是体量红线（CONVENTIONS §3）；「上行动作」与「下行帧」
 * 本来也是两组独立的关注点。
 */

/** availabilityEvent 把「Track 有没有」翻译成 §7.5 的两个回调之一。 */
EmittedEvent availabilityEvent(const std::string& kind, const std::string& uid, bool available) {
  return eventOf(kind == "video" ? "onUserVideoAvailable" : "onUserAudioAvailable",
                 obj({{"uid", Json::make(uid)}, {"available", Json::make(available)}}));
}

/** promoteAll 把某个状态的所有条目推进到下一个状态。 */
void promoteAll(std::map<std::string, std::string>& map, const std::string& from,
                const std::string& to) {
  for (auto& entry : map) {
    if (entry.second == from) entry.second = to;
  }
}

/** dropByState 删掉处在某个状态的所有条目。 */
void dropByState(std::map<std::string, std::string>& map, const std::string& target) {
  for (auto it = map.begin(); it != map.end();) {
    it = it->second == target ? map.erase(it) : std::next(it);
  }
}

std::string kindOf(const Json& data) { return str(data, "kind") == "video" ? "video" : "audio"; }

/** handleJoinOk 用快照把房间一次性搭起来：先成员，再他们的 Track。 */
RoomOutput handleJoinOk(const RoomContext& ctx, const Json& data) {
  std::vector<EmittedEvent> emit = {
      eventOf("onRoomJoined", obj({{"room_id", Json::make(str(data, "room_id"))}}))};

  RoomContext next = ctx;
  next.state = RoomState::Joined;
  next.roomId = str(data, "room_id");
  next.participantId = str(data, "participant_id");

  const Json* participants = data.find("participants");
  if (participants != nullptr) {
    for (const Json& participant : participants->items()) {
      emit.push_back(eventOf("onUserEnter", obj({{"uid", Json::make(str(participant, "uid"))}})));
    }
  }
  const Json* tracks = data.find("tracks");
  if (tracks != nullptr) {
    for (const Json& track : tracks->items()) {
      const std::string trackId = str(track, "track_id");
      const std::string kind = kindOf(track);
      next.remoteTracks[trackId] =
          RemoteTrack{str(track, "uid"), kind, str(track, "participant_id")};
      emit.push_back(availabilityEvent(kind, str(track, "uid"), !boolean(track, "muted")));
      // 自动订阅是**服务端**做的，客户端这边只记账，等 sub offer 来把它们坐实。
      if (ctx.autoSubscribe) next.subscribe[trackId] = "subscribing";
    }
  }

  // 进房成功之后**立刻重放 joining 期间攒下的意图**（不变量 R2）：
  // 宿主在 onCallBegin 里就发起的 publish 走的正是这条路。
  RoomOutput replayed = replayBuffered(next);
  emit.insert(emit.end(), replayed.emit.begin(), replayed.emit.end());
  return roomOut(std::move(replayed.state), std::move(replayed.send), std::move(emit));
}

RoomOutput handlePublishOk(const RoomContext& ctx, const Json& data) {
  RoomContext next = ctx;
  next.publishTrackIds[str(data, "cid")] = str(data, "track_id");
  // 拿到 track_id 之后才发 pub offer：服务端要靠 msid 里的 cid 认领 m-line（§3.2）。
  return roomOut(next, {frameOf(frame::kRoomOffer,
                                obj({{"pc", Json::make("pub")}, {"sdp", Json::make("")}}))});
}

/**
 * handleSubOffer：**sub PC 的 offerer 恒为服务端**（§3.3），我们只负责应答。
 * 应答的同时把「订阅中」坐实为「已订阅」——那条流这时才真的挂上来。
 */
RoomOutput handleSubOffer(const RoomContext& ctx, const Json& data) {
  if (str(data, "pc") != "sub") return roomOut(ctx);
  RoomContext next = ctx;
  promoteAll(next.subscribe, "subscribing", "subscribed");
  return roomOut(next, {frameOf(frame::kRoomAnswer,
                                obj({{"pc", Json::make("sub")}, {"sdp", Json::make("")}}))});
}

RoomOutput handleParticipantLeft(const RoomContext& ctx, const Json& data) {
  const std::string participantId = str(data, "participant_id");
  RoomContext next = ctx;
  next.remoteTracks.clear();
  for (const auto& entry : ctx.remoteTracks) {
    if (entry.second.participantId == participantId) {
      // 人走了，他的 Track 与我们对它的订阅一起清掉——不清的话重连时会重放一个死订阅。
      next.subscribe.erase(entry.first);
      continue;
    }
    next.remoteTracks[entry.first] = entry.second;
  }
  return roomOut(next, {}, {eventOf("onUserLeave", obj({{"uid", Json::make(str(data, "uid"))}}))});
}

RoomOutput handleTrackPublished(const RoomContext& ctx, const Json& data) {
  const std::string trackId = str(data, "track_id");
  const std::string kind = kindOf(data);
  const std::string uid = str(data, "uid");

  RoomContext next = ctx;
  next.remoteTracks[trackId] = RemoteTrack{uid, kind, str(data, "participant_id")};
  if (ctx.autoSubscribe) next.subscribe[trackId] = "subscribing";

  return roomOut(next, {}, {availabilityEvent(kind, uid, !boolean(data, "muted"))});
}

/** handleTrackUnpublished：帧里**不带 kind**，只能靠本地记账知道该抛音频还是视频事件。 */
RoomOutput handleTrackUnpublished(const RoomContext& ctx, const Json& data) {
  const std::string trackId = str(data, "track_id");
  RoomContext next = ctx;

  std::vector<EmittedEvent> emit;
  const auto known = ctx.remoteTracks.find(trackId);
  if (known != ctx.remoteTracks.end()) {
    emit.push_back(availabilityEvent(known->second.kind, known->second.uid, false));
  }
  next.remoteTracks.erase(trackId);
  next.subscribe.erase(trackId);
  return roomOut(next, {}, std::move(emit));
}

}  // namespace

RoomOutput reduceRoomRecv(const RoomContext& ctx, const std::string& type, const Json& data) {
  if (type == okType(frame::kRoomJoin)) return handleJoinOk(ctx, data);
  if (type == okType(frame::kRoomLeave)) {
    return roomOut(clearedRoom(RoomState::Idle), {},
                   {eventOf("onRoomLeft", obj({{"room_id", Json::make(ctx.roomId)}}))});
  }
  if (type == okType(frame::kRoomPublish)) return handlePublishOk(ctx, data);
  if (type == frame::kRoomAnswer) {
    // 服务端对 pub offer 的应答：本端那条上行协商完成了。
    RoomContext next = ctx;
    promoteAll(next.publish, "publishing", "published");
    return roomOut(next);
  }
  if (type == frame::kRoomOffer) return handleSubOffer(ctx, data);
  if (type == okType(frame::kRoomUnpublish)) {
    RoomContext next = ctx;
    dropByState(next.publish, "unpublishing");
    return roomOut(next);
  }
  if (type == okType(frame::kRoomUnsubscribe)) {
    RoomContext next = ctx;
    dropByState(next.subscribe, "unsubscribing");
    return roomOut(next);
  }
  if (type == frame::kRoomParticipantJoined) {
    return roomOut(ctx, {}, {eventOf("onUserEnter", obj({{"uid", Json::make(str(data, "uid"))}}))});
  }
  if (type == frame::kRoomParticipantLeft) return handleParticipantLeft(ctx, data);
  if (type == frame::kRoomTrackPublished) return handleTrackPublished(ctx, data);
  if (type == frame::kRoomTrackUnpublished) return handleTrackUnpublished(ctx, data);
  if (type == frame::kRoomTrackMuted) {
    return roomOut(ctx, {},
                   {availabilityEvent(kindOf(data), str(data, "uid"), !boolean(data, "muted"))});
  }
  if (type == frame::kRoomActiveSpeakers) {
    const Json* speakers = data.find("speakers");
    return roomOut(ctx, {},
                   {eventOf("onActiveSpeakers",
                            obj({{"speakers", speakers == nullptr ? Json::makeArray() : *speakers}}))});
  }
  if (type == frame::kRoomQuality) {
    const Json* entries = data.find("entries");
    return roomOut(ctx, {},
                   {eventOf("onNetworkQuality",
                            obj({{"entries", entries == nullptr ? Json::makeArray() : *entries}}))});
  }
  if (type == frame::kRoomClosed) {
    return roomOut(clearedRoom(RoomState::Idle), {},
                   {eventOf("onRoomClosed", obj({{"room_id", Json::make(str(data, "room_id"))},
                                                 {"reason", Json::make(str(data, "reason"))}}))});
  }
  // 其余的 .ok（subscribe / update_layer / mute）不改状态也不抛回调。
  return roomOut(ctx);
}

}  // namespace imrtc
