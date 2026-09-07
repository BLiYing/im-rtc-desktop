#include "imrtc/Errors.h"

#include <unordered_map>

namespace imrtc {
namespace {

/** kUnknownName 是查不到 code 时的机读名。 */
const std::string kUnknownName = "unknown";

std::unordered_map<std::int32_t, const ErrorDefinition*> buildIndex() {
  std::unordered_map<std::int32_t, const ErrorDefinition*> index;
  for (const ErrorDefinition& def : errorDefinitions()) {
    index.emplace(def.code, &def);
  }
  return index;
}

const std::unordered_map<std::int32_t, const ErrorDefinition*>& index() {
  static const std::unordered_map<std::int32_t, const ErrorDefinition*> kIndex = buildIndex();
  return kIndex;
}

}  // namespace

const std::vector<ErrorDefinition>& errorDefinitions() {
  // 顺序与 conformance/error_codes.json 一致，便于逐行对读。
  static const std::vector<ErrorDefinition> kDefinitions = {
      {1001, "bad_envelope", "malformed envelope", false, false},
      {1002, "unknown_type", "unknown frame type", false, false},
      {1003, "not_implemented", "frame not implemented", false, false},
      {1004, "bad_params", "invalid frame parameters", false, false},
      {1005, "frame_too_large", "frame too large", false, false},
      {1006, "protocol_version_unsupported", "protocol version unsupported", false, false},
      {1007, "rate_limited", "rate limited", true, false},
      {1101, "token_invalid", "token invalid", false, false},
      {1102, "token_expired", "token expired", true, false},
      {1103, "not_authenticated", "not authenticated", false, false},
      {1104, "kicked_out", "kicked out", false, false},
      {1105, "session_not_resumable", "session not resumable", false, false},
      // 票据合法但该 app_id 已被停用（宿主在控制台停用了整个应用）。
      // 与 1101/1102 不同：那两个是「换张票再来」，这个是「这个应用被停了」。
      {1106, "app_disabled", "application disabled", false, false},
      {1201, "room_not_found", "room not found", false, false},
      {1202, "room_full", "room is full", false, false},
      {1203, "not_in_room", "not in room", false, false},
      {1204, "already_in_room", "already in room", false, false},
      {1205, "room_closed", "room closed", false, false},
      {1206, "permission_denied", "permission denied", false, false},
      {1207, "participant_not_found", "participant not found", false, false},
      {1301, "track_not_found", "track not found", false, false},
      {1302, "publish_denied", "publish denied", false, false},
      {1303, "subscribe_denied", "subscribe denied", false, false},
      {1304, "sdp_invalid", "sdp invalid", false, false},
      {1305, "pc_not_found", "peer connection not found", false, false},
      {1306, "layer_unavailable", "layer unavailable", false, false},
      {1307, "codec_unsupported", "codec unsupported", false, false},
      {1401, "call_not_found", "call not found", false, false},
      {1402, "call_ended", "call already ended", false, false},
      {1403, "callee_offline", "callee offline", false, false},
      {1404, "callee_busy", "callee busy", false, false},
      {1405, "invalid_call_state", "invalid call state", false, false},
      {1406, "too_many_callees", "too many callees", false, false},
      {1407, "not_call_owner", "not call owner", false, false},
      {1408, "already_in_call", "already in call", false, false},
      {1501, "internal", "internal error", true, false},
      {1502, "sfu_unavailable", "sfu unavailable", true, false},
      {1503, "shutting_down", "server shutting down", true, false},
      {1504, "store_error", "store error", true, false},
      {2001, "device_permission_denied", "device permission denied", false, true},
      {2002, "device_not_found", "device not found", false, true},
      {2003, "network_unreachable", "network unreachable", true, true},
      {2004, "signaling_timeout", "signaling timeout", true, true},
      {2005, "invalid_state", "invalid state", false, true},
      {2006, "media_negotiation_failed", "media negotiation failed", false, true},
      {2007, "not_logged_in", "not logged in", false, true},
  };
  return kDefinitions;
}

const ErrorDefinition* lookupError(std::int32_t code) {
  const auto it = index().find(code);
  return it == index().end() ? nullptr : it->second;
}

std::string errorName(std::int32_t code) {
  const ErrorDefinition* def = lookupError(code);
  return def == nullptr ? kUnknownName : def->name;
}

bool isRetryable(std::int32_t code) {
  const ErrorDefinition* def = lookupError(code);
  return def != nullptr && def->retryable;
}

bool isLocalError(std::int32_t code) {
  const ErrorDefinition* def = lookupError(code);
  return def != nullptr && def->local;
}

namespace {

/** describe 拼出异常的 what()：`name(code): msg — detail`。detail 只进日志。 */
std::string describe(const ErrorDefinition& def, const std::string& detail) {
  std::string text = def.name + "(" + std::to_string(def.code) + "): " + def.msg;
  if (!detail.empty()) {
    text += " —— " + detail;
  }
  return text;
}

/** resolve 兜底到 internal —— 表里一定有它，所以返回值必然非空。 */
const ErrorDefinition& resolve(ErrorCode code) {
  const ErrorDefinition* def = lookupError(codeValue(code));
  if (def != nullptr) {
    return *def;
  }
  const ErrorDefinition* fallback = lookupError(codeValue(ErrorCode::Internal));
  return *fallback;
}

}  // namespace

RtcError::RtcError(ErrorCode code, std::string detail, std::string forType)
    : std::runtime_error(describe(resolve(code), detail)),
      code_(resolve(code).code),
      name_(resolve(code).name),
      retryable_(resolve(code).retryable),
      forType_(std::move(forType)),
      detail_(std::move(detail)) {}

}  // namespace imrtc
