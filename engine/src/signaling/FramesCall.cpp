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
      // 宿主自己的群号，opaque、≤64 字节，可空（HOST_INTEGRATION_DESIGN §3.2，2026-09-15）。
      // 服务端不解析、不校验群成员关系，只原样带出去；通话期间不可改。
      stringField("chat_group_id"),
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
      // 原样带上：被叫与中途加入者靠它决定「添加成员」列哪个群的人（§4.2）。
      stringField("chat_group_id"),
      // 这次邀请是谁发的（2026-09-16）。**与 caller 不是一回事**：caller 恒为发起人，
      // invite_more 拉人进来时 inviter 才是按下「添加成员」的那个人。空串 = 旧服务端不带，
      // 由 handleIncoming 回落成 caller。
      stringField("inviter"),
      // 此刻已在通话里的人（不含收件人，2026-09-20）。**必须列在这里**：解码只认表里的字段。
      // 空 = 旧服务端不带，宿主回落成「只有 caller 在通话里」。
      stringArrayField("joined_ids"),
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
      // 发起人。call.join 进来的人没收过 call.incoming，只能从这里知道（§4.2）。
      stringField("caller"),
      // 同 call.invite，中途加入与断线恢复后也拿得到群号（§3.2）。
      stringField("chat_group_id"),
      // 同 call.invite，原样回显。
      stringField("user_data"),
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
