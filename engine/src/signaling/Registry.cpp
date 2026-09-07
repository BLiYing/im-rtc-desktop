#include "imrtc/Registry.h"

#include <algorithm>
#include <unordered_map>

#include "imrtc/Errors.h"
#include "imrtc/Frames.h"

namespace imrtc {

namespace frame {
const char* const kHello = "sys.hello";
const char* const kHelloOk = "sys.hello.ok";
const char* const kPing = "sys.ping";
const char* const kPong = "sys.pong";
const char* const kError = "sys.error";

const char* const kRoomJoin = "room.join";
const char* const kRoomLeave = "room.leave";
const char* const kRoomPublish = "room.publish";
const char* const kRoomUnpublish = "room.unpublish";
const char* const kRoomMute = "room.mute";
const char* const kRoomSubscribe = "room.subscribe";
const char* const kRoomUnsubscribe = "room.unsubscribe";
const char* const kRoomUpdateLayer = "room.update_layer";
const char* const kRoomOffer = "room.offer";
const char* const kRoomAnswer = "room.answer";
const char* const kRoomIceCandidate = "room.ice_candidate";

const char* const kRoomParticipantJoined = "room.participant_joined";
const char* const kRoomParticipantLeft = "room.participant_left";
const char* const kRoomTrackPublished = "room.track_published";
const char* const kRoomTrackUnpublished = "room.track_unpublished";
const char* const kRoomTrackMuted = "room.track_muted";
const char* const kRoomActiveSpeakers = "room.active_speakers";
const char* const kRoomQuality = "room.quality";
const char* const kRoomClosed = "room.closed";

const char* const kCallInvite = "call.invite";
const char* const kCallAccept = "call.accept";
const char* const kCallReject = "call.reject";
const char* const kCallCancel = "call.cancel";
const char* const kCallHangup = "call.hangup";
const char* const kCallInviteMore = "call.invite_more";
const char* const kCallJoin = "call.join";

const char* const kCallIncoming = "call.incoming";
const char* const kCallRinging = "call.ringing";
const char* const kCallAccepted = "call.accepted";
const char* const kCallRejected = "call.rejected";
const char* const kCallBusy = "call.busy";
const char* const kCallNoAnswer = "call.no_answer";
const char* const kCallCancelled = "call.cancelled";
const char* const kCallConnected = "call.connected";
const char* const kCallHandledElsewhere = "call.handled_elsewhere";
const char* const kCallEnded = "call.ended";
}  // namespace frame

namespace {

using Registry = std::unordered_map<std::string, const FrameFields*>;

const Registry& registry() {
  static const Registry kRegistry = {
      {frame::kHello, &helloFields()},
      {frame::kHelloOk, &helloOkFields()},
      {frame::kPing, &emptyFields()},
      {frame::kPong, &emptyFields()},
      {frame::kError, &errorFields()},

      {frame::kRoomJoin, &joinFields()},
      {frame::kRoomLeave, &leaveFields()},
      {frame::kRoomPublish, &publishFields()},
      {frame::kRoomUnpublish, &trackIdFields()},
      {frame::kRoomMute, &muteFields()},
      {frame::kRoomSubscribe, &layerFields()},
      {frame::kRoomUnsubscribe, &trackIdFields()},
      {frame::kRoomUpdateLayer, &layerFields()},
      {frame::kRoomOffer, &sdpFields()},
      {frame::kRoomAnswer, &sdpFields()},
      {frame::kRoomIceCandidate, &iceFields()},

      {frame::kRoomParticipantJoined, &participantJoinedFields()},
      {frame::kRoomParticipantLeft, &participantLeftFields()},
      {frame::kRoomTrackPublished, &trackPublishedFields()},
      {frame::kRoomTrackUnpublished, &trackUnpublishedFields()},
      {frame::kRoomTrackMuted, &trackMutedFields()},
      {frame::kRoomActiveSpeakers, &activeSpeakersFields()},
      {frame::kRoomQuality, &qualityFields()},
      {frame::kRoomClosed, &roomClosedFields()},

      {frame::kCallInvite, &inviteFields()},
      {frame::kCallAccept, &callIdFields()},
      {frame::kCallReject, &callIdFields()},
      {frame::kCallCancel, &callIdFields()},
      {frame::kCallHangup, &callIdFields()},
      {frame::kCallInviteMore, &inviteMoreFields()},
      {frame::kCallJoin, &callIdFields()},

      {frame::kCallIncoming, &incomingFields()},
      {frame::kCallRinging, &ringingFields()},
      {frame::kCallAccepted, &memberOutcomeFields()},
      {frame::kCallRejected, &memberOutcomeFields()},
      {frame::kCallBusy, &memberOutcomeFields()},
      {frame::kCallNoAnswer, &memberOutcomeFields()},
      {frame::kCallCancelled, &cancelledFields()},
      {frame::kCallConnected, &connectedFields()},
      {frame::kCallHandledElsewhere, &handledElsewhereFields()},
      {frame::kCallEnded, &endedFields()},
  };
  return kRegistry;
}

const Registry& okRegistry() {
  static const Registry kRegistry = {
      {okType(frame::kHello), &helloOkFields()},
      {okType(frame::kRoomJoin), &joinOkFields()},
      {okType(frame::kRoomPublish), &publishOkFields()},
      {okType(frame::kCallInvite), &inviteOkFields()},
      // room.offer 没有 .ok —— pub 侧的 offer 由 room.answer 直接作为应答回来（§3.3）。
      {okType(frame::kRoomAnswer), &emptyFields()},
      {okType(frame::kRoomIceCandidate), &emptyFields()},
      {okType(frame::kCallAccept), &emptyFields()},
      {okType(frame::kCallJoin), &emptyFields()},
      {okType(frame::kCallInviteMore), &emptyFields()},
  };
  return kRegistry;
}

bool endsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

const std::vector<std::string>& requestTypes() {
  static const std::vector<std::string> kTypes = {
      frame::kHello,         frame::kPing,           frame::kRoomJoin,
      frame::kRoomLeave,     frame::kRoomPublish,    frame::kRoomUnpublish,
      frame::kRoomMute,      frame::kRoomSubscribe,  frame::kRoomUnsubscribe,
      frame::kRoomUpdateLayer, frame::kCallInvite,   frame::kCallAccept,
      frame::kCallReject,    frame::kCallCancel,     frame::kCallHangup,
      frame::kCallInviteMore, frame::kCallJoin,
  };
  return kTypes;
}

const std::vector<std::string>& reservedTypes() {
  static const std::vector<std::string> kTypes = {
      "room.mute_participant", "room.kick",              "room.lock",
      "room.raise_hand",       "room.participant_muted", "room.participant_kicked",
      "room.hand_raised",      "room.locked",
  };
  return kTypes;
}

bool isRequestType(const std::string& type) {
  const std::vector<std::string>& types = requestTypes();
  return std::find(types.begin(), types.end(), type) != types.end();
}

bool isReservedType(const std::string& type) {
  const std::vector<std::string>& types = reservedTypes();
  return std::find(types.begin(), types.end(), type) != types.end();
}

std::string replyTypeOf(const std::string& requestType) {
  // room.offer 的应答是 room.answer，不是 room.offer.ok（§3.3）。
  if (requestType == frame::kRoomOffer) return frame::kRoomAnswer;
  return okType(requestType);
}

bool isOkType(const std::string& type) { return endsWith(type, kOkSuffix); }

const FrameFields* lookupFrame(const std::string& type) {
  const auto direct = registry().find(type);
  if (direct != registry().end()) return direct->second;
  const auto ok = okRegistry().find(type);
  if (ok != okRegistry().end()) return ok->second;

  const std::string suffix = kOkSuffix;
  if (endsWith(type, suffix)) {
    const std::string base = type.substr(0, type.size() - suffix.size());
    if (isRequestType(base)) return &emptyFields();
  }
  return nullptr;
}

std::vector<std::string> knownFrameTypes() {
  std::vector<std::string> types;
  types.reserve(registry().size() + okRegistry().size());
  for (const auto& entry : registry()) types.push_back(entry.first);
  for (const auto& entry : okRegistry()) types.push_back(entry.first);
  std::sort(types.begin(), types.end());
  return types;
}

Json decodeFrame(const Envelope& envelope) {
  const FrameFields* fields = lookupFrame(envelope.type);
  if (fields == nullptr) {
    throw RtcError(ErrorCode::UnknownType, "未注册的帧 \"" + envelope.type + "\"", envelope.type);
  }
  return decodeFields(*fields, envelope.data);
}

}  // namespace imrtc
