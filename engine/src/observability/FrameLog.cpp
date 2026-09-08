#include "imrtc/FrameLog.h"

namespace imrtc {
namespace {

/** appendIfPresent 只在字段真的在帧里时才带上——空字段会把日志撑得没法读。 */
void appendIfPresent(LogFields& fields, const Json& data, const char* key) {
  const Json* value = data.find(key);
  if (value == nullptr) return;
  if (value->isString()) {
    if (value->asString().empty()) return;
    fields.emplace_back(key, value->asString());
  } else if (value->isInt()) {
    fields.emplace_back(key, std::to_string(value->asInt()));
  }
}

}  // namespace

LogFields frameLogFields(const std::string& type, const std::string& reqId, const Json& data) {
  LogFields fields;
  fields.emplace_back(logfield::kType, type);
  // req_id 是把一次请求串起来的那根线（协议 §2.1）。事件帧恒为空，就不占一格了。
  if (!reqId.empty()) fields.emplace_back(logfield::kRequestId, reqId);
  appendIfPresent(fields, data, logfield::kCallId);
  appendIfPresent(fields, data, logfield::kRoomId);
  appendIfPresent(fields, data, logfield::kTrackId);
  appendIfPresent(fields, data, logfield::kCode);
  return fields;
}

}  // namespace imrtc
