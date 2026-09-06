#include "imrtc/Enums.h"
#include "imrtc/Frames.h"

namespace imrtc {
namespace {

/** withRoomId 在一张表前面补上 room_id —— 房间事件都带它。 */
FrameFields withRoomId(const FrameFields& base) {
  FrameFields fields = {stringField("room_id")};
  fields.insert(fields.end(), base.begin(), base.end());
  return fields;
}

}  // namespace

const FrameFields& participantFields() {
  static const FrameFields kFields = {
      stringField("participant_id"),
      stringField("uid"),
      stringField("device_id"),
      intField("joined_at_ms"),
  };
  return kFields;
}

const FrameFields& trackFields() {
  static const FrameFields kFields = {
      stringField("track_id"),
      stringField("participant_id"),
      stringField("uid"),
      enumField("kind", trackKinds(), "audio"),
      enumField("source", trackSources(), "microphone"),
      stringField("codec"),
      // 空数组 = 单层。**不能缺席**。
      enumArrayField("simulcast_layers", layers(), "l"),
      boolField("muted"),
  };
  return kFields;
}

const FrameFields& joinFields() {
  // **注意 auto_subscribe / publish_audio 默认是 true**：直接用零值对象发这一帧，
  // 线路上会变成 false，人进了房却收不到任何流。发送侧一律用 newFrameData()。
  static const FrameFields kFields = {
      stringField("room_id"),        stringField("room_token"),
      boolField("auto_subscribe", true), boolField("publish_audio", true),
      boolField("publish_video", false),
  };
  return kFields;
}

const FrameFields& joinOkFields() {
  static const FrameFields kFields = {
      stringField("room_id"),
      enumField("room_kind", roomKinds(), "call_group"),
      stringField("participant_id"),
      intField("max_participants"),
      intField("joined_at_ms"),
      objectArrayField("participants", participantFields()),
      objectArrayField("tracks", trackFields()),
  };
  return kFields;
}

const FrameFields& leaveFields() {
  static const FrameFields kFields = {stringField("room_id")};
  return kFields;
}

const FrameFields& publishFields() {
  // cid 必须出现在随后 pub offer 的 msid 里；服务端靠它把 SDP 的 m-line 认回 track_id。
  static const FrameFields kFields = {
      stringField("cid"),
      enumField("kind", trackKinds(), "audio"),
      enumField("source", trackSources(), "microphone"),
      boolField("simulcast"),
      intField("width"),
      intField("height"),
      intField("max_bitrate_kbps"),
  };
  return kFields;
}

const FrameFields& publishOkFields() {
  static const FrameFields kFields = {stringField("track_id"), stringField("cid")};
  return kFields;
}

const FrameFields& trackIdFields() {
  static const FrameFields kFields = {stringField("track_id")};
  return kFields;
}

const FrameFields& muteFields() {
  static const FrameFields kFields = {stringField("track_id"), boolField("muted")};
  return kFields;
}

const FrameFields& layerFields() {
  static const FrameFields kFields = {stringField("track_id"),
                                      enumField("max_layer", layers(), "l", "m")};
  return kFields;
}

const FrameFields& sdpFields() {
  static const FrameFields kFields = {enumField("pc", pcRoles(), "pub"), stringField("sdp")};
  return kFields;
}

const FrameFields& iceFields() {
  static const FrameFields kFields = {
      enumField("pc", pcRoles(), "pub"),
      stringField("candidate"),
      stringField("sdp_mid"),
      intField("sdp_mline_index"),
  };
  return kFields;
}

const FrameFields& participantJoinedFields() {
  static const FrameFields kFields = withRoomId(participantFields());
  return kFields;
}

const FrameFields& participantLeftFields() {
  static const FrameFields kFields = {
      stringField("room_id"),
      stringField("participant_id"),
      stringField("uid"),
      stringField("device_id"),
      // reason 取 §6 的子集。
      enumField("reason", reasonValues(), "error"),
      intField("duration_sec"),
  };
  return kFields;
}

const FrameFields& trackPublishedFields() {
  static const FrameFields kFields = withRoomId(trackFields());
  return kFields;
}

const FrameFields& trackUnpublishedFields() {
  static const FrameFields kFields = {
      stringField("room_id"),
      stringField("track_id"),
      stringField("participant_id"),
      stringField("uid"),
  };
  return kFields;
}

const FrameFields& trackMutedFields() {
  static const FrameFields kFields = {
      stringField("room_id"),        stringField("track_id"),
      stringField("participant_id"), stringField("uid"),
      enumField("kind", trackKinds(), "audio"), boolField("muted"),
  };
  return kFields;
}

const FrameFields& speakerFields() {
  static const FrameFields kFields = {
      stringField("participant_id"),
      stringField("uid"),
      intRangeField("volume", 0, 0, 100),
  };
  return kFields;
}

const FrameFields& activeSpeakersFields() {
  static const FrameFields kFields = {stringField("room_id"),
                                      objectArrayField("speakers", speakerFields())};
  return kFields;
}

const FrameFields& qualityEntryFields() {
  static const FrameFields kFields = {
      stringField("participant_id"),
      stringField("uid"),
      // 越界折成 0 = unknown，**不是钳到 6**——把未知说成「已断开」会误导 UI。
      intFoldField("level", kQualityUnknown, kQualityDisconnected, kQualityUnknown),
  };
  return kFields;
}

const FrameFields& qualityFields() {
  static const FrameFields kFields = {stringField("room_id"),
                                      objectArrayField("entries", qualityEntryFields())};
  return kFields;
}

const FrameFields& roomClosedFields() {
  static const FrameFields kFields = {
      stringField("room_id"),
      enumField("reason", reasonValues(), "error"),
      intField("duration_sec"),
  };
  return kFields;
}

}  // namespace imrtc
