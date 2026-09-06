#include "imrtc/Enums.h"
#include "imrtc/Frames.h"

namespace imrtc {

const FrameFields& inviteFields() {
  static const FrameFields kFields = {
      // 1v1 恰好 1 个；群 ≤8（房内含主叫共 9 人）。
      stringArrayField("callee_ids"),
      enumField("media_type", mediaTypes(), "audio"),
      boolField("is_group"),
      // "" = 服务端建房。
      stringField("room_id"),
      intRangeField("timeout_sec", kDefaultTimeoutSec, kMinTimeoutSec, kMaxTimeoutSec),
      stringField("user_data"),
  };
  return kFields;
}

const FrameFields& inviteOkFields() {
  static const FrameFields kFields = {
      stringField("call_id"),
      stringField("room_id"),
      intField("invited_at_ms"),
  };
  return kFields;
}

const FrameFields& callIdFields() {
  // 接通后主叫也用 hangup，**不用 cancel**——两个词不共用一条路径，
  // 避免「取消一通已接通的电话」这种歧义。
  static const FrameFields kFields = {stringField("call_id")};
  return kFields;
}

const FrameFields& inviteMoreFields() {
  static const FrameFields kFields = {stringField("call_id"), stringArrayField("callee_ids")};
  return kFields;
}

const FrameFields& incomingFields() {
  static const FrameFields kFields = {
      stringField("call_id"),
      stringField("room_id"),
      stringField("caller"),
      stringArrayField("callee_ids"),
      enumField("media_type", mediaTypes(), "audio"),
      boolField("is_group"),
      intRangeField("timeout_sec", kDefaultTimeoutSec, kMinTimeoutSec, kMaxTimeoutSec),
      intField("invited_at_ms"),
      stringField("user_data"),
  };
  return kFields;
}

const FrameFields& ringingFields() {
  static const FrameFields kFields = {
      stringField("call_id"),
      stringField("uid"),
      intField("device_count"),
  };
  return kFields;
}

const FrameFields& memberOutcomeFields() {
  // call.accepted / rejected / busy / no_answer 共用。
  static const FrameFields kFields = {stringField("call_id"), stringField("uid")};
  return kFields;
}

const FrameFields& cancelledFields() {
  static const FrameFields kFields = {stringField("call_id"), stringField("by")};
  return kFields;
}

const FrameFields& connectedFields() {
  // call.accept.ok 是纯 ack，房间信息只在这一条帧里——一个东西一条路径。
  static const FrameFields kFields = {
      stringField("call_id"),
      stringField("room_id"),
      // 绑定 (room_id, uid, device_id)，TTL 5 分钟、一次性。**不要整条打日志**。
      stringField("room_token"),
      enumField("media_type", mediaTypes(), "audio"),
      boolField("is_group"),
      // 通话时长的起点，服务端时钟。
      intField("connected_at_ms"),
      stringField("accepted_by"),
  };
  return kFields;
}

const FrameFields& handledElsewhereFields() {
  static const FrameFields kFields = {
      stringField("call_id"),
      enumField("action", handledActions(), "accept"),
      stringField("device_id"),
  };
  return kFields;
}

const FrameFields& endedFields() {
  // 铁律：每个成员设备收到且仅收到一条；所有结局都走它；
  // 宿主只监听它也必须能完整记录一通电话。
  static const FrameFields kFields = {
      stringField("call_id"),
      stringField("room_id"),
      enumField("reason", reasonValues(), "error"),
      // 未接通恒为 0。**五端禁止自己算时长**（时钟偏移），一律用这个值。
      intField("duration_sec"),
      // 动作发起人 uid；服务端自行判定时为 ""。
      stringField("ended_by"),
      // 这通电话是谁打的。**忙线那条不振铃**，被叫拿到的第一帧也是最后一帧就是它，
      // 没有这个字段就说不出「谁来过电话」（协议 §4.2）。
      stringField("caller"),
  };
  return kFields;
}

}  // namespace imrtc
