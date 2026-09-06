#include "imrtc/Envelope.h"

#include "imrtc/Enums.h"
#include "imrtc/Errors.h"

namespace imrtc {
namespace {

const char* const kRequiredKeys[] = {"type", "req_id", "ts", "data"};

[[noreturn]] void badEnvelope(const std::string& reason) {
  throw RtcError(ErrorCode::BadEnvelope, reason);
}

}  // namespace

const char* const kOkSuffix = ".ok";

std::string okType(const std::string& requestType) { return requestType + kOkSuffix; }

Envelope decodeEnvelope(const std::string& raw) {
  if (raw.size() > kMaxFrameBytes) {
    throw RtcError(ErrorCode::FrameTooLarge,
                   "帧 " + std::to_string(raw.size()) + " 字节 > 上限 " +
                       std::to_string(kMaxFrameBytes));
  }

  const Json parsed = Json::parse(raw);
  if (!parsed.isObject()) badEnvelope("一帧必须是一个 JSON 对象");

  for (const char* key : kRequiredKeys) {
    if (!parsed.contains(key)) {
      badEnvelope(std::string("缺少必填字段 \"") + key + "\"");
    }
  }

  const Json& type = *parsed.find("type");
  if (!type.isString() || type.asString().empty()) badEnvelope("字段 type 必须是非空字符串");

  // req_id 允许是 ""（事件就是 ""），但不允许缺失或非字符串。
  const Json& reqId = *parsed.find("req_id");
  if (!reqId.isString()) badEnvelope("字段 req_id 必须是字符串");

  const Json& ts = *parsed.find("ts");
  if (!ts.isInt()) badEnvelope("字段 ts 必须是整数");

  const Json& data = *parsed.find("data");
  if (data.isNull()) badEnvelope("字段 data 不能是 null，无内容时用 {}");
  if (!data.isObject()) badEnvelope("字段 data 必须是对象，无内容时用 {}");

  checkDiscipline(data);

  Envelope envelope;
  envelope.type = type.asString();
  envelope.reqId = reqId.asString();
  envelope.ts = ts.asInt();
  envelope.data = data;
  return envelope;
}

std::string encodeEnvelope(const std::string& type, const std::string& reqId, const Json& data,
                           std::int64_t ts) {
  if (type.empty()) badEnvelope("type 不能为空");

  Json frame = Json::makeObject();
  frame.set("type", Json::make(type));
  frame.set("req_id", Json::make(reqId));
  frame.set("ts", Json::make(ts));
  frame.set("data", data.isObject() ? data : Json::makeObject());

  std::string raw = frame.dump();
  if (raw.size() > kMaxFrameBytes) {
    throw RtcError(ErrorCode::FrameTooLarge,
                   type + " 编码后 " + std::to_string(raw.size()) + " 字节 > 上限 " +
                       std::to_string(kMaxFrameBytes),
                   type);
  }
  return raw;
}

}  // namespace imrtc
