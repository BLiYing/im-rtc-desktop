#include "CObserver.h"

#include "CApiConvert.h"

namespace imrtc {
namespace capi_detail {

void CObserver::onConnected(const std::string& sessionId, bool resumed) {
  if (table_.on_connected) table_.on_connected(table_.user_data, sessionId.c_str(), fromBool(resumed));
}

void CObserver::onDisconnected(std::int32_t code, bool willReconnect) {
  /*
    **装了 _ex 就只调 _ex**，不叠加调旧的——同一次断开只报一次，
    不然宿主要么收两条重复事件，要么自己再判一次「这两条是不是同一次」。
  */
  if (table_.on_disconnected_ex) {
    table_.on_disconnected_ex(table_.user_data, code, fromBool(willReconnect));
    return;
  }
  if (table_.on_disconnected) table_.on_disconnected(table_.user_data);
}

void CObserver::onKickedOut(KickedReason reason) {
  if (table_.on_kicked_out) table_.on_kicked_out(table_.user_data, toKickedReason(reason));
}

void CObserver::onError(std::int32_t code, const std::string& name, const std::string& forType) {
  if (table_.on_error) table_.on_error(table_.user_data, code, name.c_str(), forType.c_str());
}

void CObserver::onCallReceived(const CallInvite& invite) {
  if (!table_.on_call_received) return;
  // 字符串数组要摊成 const char* 的连续数组：C 那边收的是指针 + 长度。
  std::vector<const char*> ids;
  ids.reserve(invite.calleeIds.size());
  for (const std::string& id : invite.calleeIds) ids.push_back(id.c_str());

  std::vector<const char*> joined;
  joined.reserve(invite.joinedIds.size());
  for (const std::string& id : invite.joinedIds) joined.push_back(id.c_str());

  imrtc_v1_call_invite out{};
  out.struct_size = sizeof(out);
  out.call_id = invite.callId.c_str();
  out.caller = invite.caller.c_str();
  out.callee_ids = ids.empty() ? nullptr : ids.data();
  out.callee_count = static_cast<std::uint32_t>(ids.size());
  out.media_type = invite.mediaType.c_str();
  out.is_group = fromBool(invite.isGroup);
  out.chat_group_id = invite.chatGroupId.c_str();
  out.user_data = invite.userData.c_str();
  out.inviter = invite.inviter.c_str();
  out.joined_ids = joined.empty() ? nullptr : joined.data();
  out.joined_count = static_cast<std::uint32_t>(joined.size());
  table_.on_call_received(table_.user_data, &out);
}

void CObserver::onCallBegin(const CallBegin& begin) {
  if (!table_.on_call_begin) return;
  imrtc_v1_call_begin out{};
  out.struct_size = sizeof(out);
  out.call_id = begin.callId.c_str();
  out.room_id = begin.roomId.c_str();
  out.media_type = begin.mediaType.c_str();
  out.is_group = fromBool(begin.isGroup);
  out.role = begin.role.c_str();
  out.caller = begin.caller.c_str();
  out.chat_group_id = begin.chatGroupId.c_str();
  out.user_data = begin.userData.c_str();
  table_.on_call_begin(table_.user_data, &out);
}

void CObserver::onCallEnd(const CallEnd& end) {
  if (!table_.on_call_end) return;
  imrtc_v1_call_end out{};
  out.struct_size = sizeof(out);
  out.call_id = end.callId.c_str();
  out.reason = end.reason.c_str();
  out.duration_sec = end.durationSec;
  out.ended_by = end.endedBy.c_str();
  out.reason_code = toEndReason(end.reasonCode);
  table_.on_call_end(table_.user_data, &out);
}

void CObserver::onCallSummary(const CallSummary& summary) {
  // 旧宿主的 struct_size 不含它：imrtc_observer_create 拷表时缺的尾部已补零，这里就是 NULL。
  if (!table_.on_call_summary) return;
  imrtc_v1_call_summary out{};
  out.struct_size = sizeof(out);
  out.call_id = summary.callId.c_str();
  out.reason = summary.reason.c_str();
  out.reason_code = toEndReason(summary.reasonCode);
  out.duration_sec = summary.durationSec;
  out.ended_by = summary.endedBy.c_str();
  out.media_type = summary.mediaType.c_str();
  out.is_group = fromBool(summary.isGroup);
  out.chat_group_id = summary.chatGroupId.c_str();
  out.caller = summary.caller.c_str();
  out.role = summary.role.c_str();
  out.peer = summary.peer.c_str();
  out.user_data = summary.userData.c_str();
  table_.on_call_summary(table_.user_data, &out);
}

void CObserver::onCallMissed(const CallMissed& missed) {
  if (!table_.on_call_missed) return;
  imrtc_v1_call_missed out{};
  out.struct_size = sizeof(out);
  out.call_id = missed.callId.c_str();
  out.caller = missed.caller.c_str();
  out.reason = missed.reason.c_str();
  table_.on_call_missed(table_.user_data, &out);
}

void CObserver::onCallCancelled(const std::string& uid) { one(table_.on_call_cancelled, uid); }
void CObserver::onCallRejected(const std::string& uid) { one(table_.on_call_rejected, uid); }
void CObserver::onCallBusy(const std::string& uid) { one(table_.on_call_busy, uid); }
void CObserver::onCallNoAnswer(const std::string& uid) { one(table_.on_call_no_answer, uid); }
void CObserver::onUserEnter(const std::string& uid) { one(table_.on_user_enter, uid); }
void CObserver::onUserLeave(const std::string& uid) { one(table_.on_user_leave, uid); }
void CObserver::onUserRinging(const std::string& uid) { one(table_.on_user_ringing, uid); }
void CObserver::onUserAccept(const std::string& uid) { one(table_.on_user_accept, uid); }
void CObserver::onUserReject(const std::string& uid) { one(table_.on_user_reject, uid); }
void CObserver::onUserNoResponse(const std::string& uid) { one(table_.on_user_no_response, uid); }
void CObserver::onRoomJoined(const std::string& roomId) { one(table_.on_room_joined, roomId); }
void CObserver::onRoomLeft(const std::string& roomId) { one(table_.on_room_left, roomId); }

void CObserver::onHandledOnOtherDevice(const std::string& callId, const std::string& action) {
  if (table_.on_handled_on_other_device) {
    table_.on_handled_on_other_device(table_.user_data, callId.c_str(), action.c_str());
  }
}

void CObserver::onRoomClosed(const std::string& roomId, const std::string& reason) {
  if (table_.on_room_closed) {
    table_.on_room_closed(table_.user_data, roomId.c_str(), reason.c_str());
  }
}

void CObserver::onUserAudioAvailable(const std::string& uid, bool available) {
  if (table_.on_user_audio_available) {
    table_.on_user_audio_available(table_.user_data, uid.c_str(), fromBool(available));
  }
}

void CObserver::onUserVideoAvailable(const std::string& uid, bool available) {
  if (table_.on_user_video_available) {
    table_.on_user_video_available(table_.user_data, uid.c_str(), fromBool(available));
  }
}

void CObserver::onActiveSpeakers(const std::vector<Speaker>& speakers) {
  if (!table_.on_active_speakers) return;
  std::vector<imrtc_v1_speaker> out;
  out.reserve(speakers.size());
  for (const Speaker& speaker : speakers) {
    imrtc_v1_speaker item{};
    item.struct_size = sizeof(item);
    item.uid = speaker.uid.c_str();
    item.participant_id = speaker.participantId.c_str();
    item.volume = speaker.volume;
    out.push_back(item);
  }
  table_.on_active_speakers(table_.user_data, out.empty() ? nullptr : out.data(),
                            static_cast<std::uint32_t>(out.size()));
}

void CObserver::onNetworkQuality(const std::vector<QualityEntry>& entries) {
  if (!table_.on_network_quality) return;
  std::vector<imrtc_v1_quality> out;
  out.reserve(entries.size());
  for (const QualityEntry& entry : entries) {
    imrtc_v1_quality item{};
    item.struct_size = sizeof(item);
    item.uid = entry.uid.c_str();
    item.participant_id = entry.participantId.c_str();
    item.level = entry.level;
    out.push_back(item);
  }
  table_.on_network_quality(table_.user_data, out.empty() ? nullptr : out.data(),
                            static_cast<std::uint32_t>(out.size()));
}

void CObserver::one(OneArg fn, const std::string& value) const {
  if (fn) fn(table_.user_data, value.c_str());
}

}  // namespace capi_detail
}  // namespace imrtc
