#include "imrtc/Frames.h"

namespace imrtc {

const FrameFields& emptyFields() {
  static const FrameFields kFields = {};
  return kFields;
}

const FrameFields& helloFields() {
  // token 走首帧而不是 URL 查询串：查询串会进网关日志、Referer 与浏览器历史。
  static const FrameFields kFields = {
      intField("protocol_version", 1),
      stringField("token"),
      stringField("device_id"),
      // 重连恢复用；首次连接为 ""。
      stringField("session_id"),
      // 仅用于日志与灰度，**禁止参与逻辑**。
      stringField("sdk"),
  };
  return kFields;
}

const FrameFields& limitsFields() {
  static const FrameFields kFields = {
      intField("max_frame_bytes"),      intField("max_callees"),
      intField("max_room_participants"), intField("max_user_data_bytes"),
      intField("ring_timeout_sec_default"),
  };
  return kFields;
}

const FrameFields& helloOkFields() {
  static const FrameFields kFields = {
      stringField("uid"),
      stringField("device_id"),
      stringField("session_id"),
      // 供客户端算时钟偏移，**只做展示**。
      intField("server_time_ms"),
      boolField("resumed"),
      intField("ping_interval_sec"),
      objectField("limits", limitsFields()),
  };
  return kFields;
}

const FrameFields& errorFields() {
  static const FrameFields kFields = {
      intField("code"),
      stringField("name"),
      // 英文固定短语，给开发者看；**禁止直接显示给用户**。
      stringField("msg"),
      stringField("for_type"),
      boolField("retryable"),
  };
  return kFields;
}

}  // namespace imrtc
