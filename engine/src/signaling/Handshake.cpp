#include "imrtc/Handshake.h"

#include "imrtc/Errors.h"

namespace imrtc {
namespace {

constexpr char kTakenOver[] = "taken_over";
constexpr char kAuthExpired[] = "auth_expired";
constexpr char kConfigRejected[] = "config_rejected";

/** allowedChar 是协议 §2.5 的 charset `[A-Za-z0-9_-]`。 */
bool allowedChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
         c == '-';
}

}  // namespace

const char* kickedReasonName(KickedReason reason) {
  switch (reason) {
    case KickedReason::AuthExpired: return kAuthExpired;
    case KickedReason::ConfigRejected: return kConfigRejected;
    case KickedReason::TakenOver: break;
  }
  return kTakenOver;
}

KickedReason parseKickedReason(const std::string& name) {
  if (name == kAuthExpired) return KickedReason::AuthExpired;
  if (name == kConfigRejected) return KickedReason::ConfigRejected;
  return KickedReason::TakenOver;
}

bool handshakeGiveUpReason(std::int32_t code, bool wireRetryable, KickedReason& out) {
  // 边界 1：local 组的码不是服务端的裁决（logout / 超时 / 断线都在这一组）。
  if (isLocalError(code)) return false;

  const ErrorDefinition* def = lookupError(code);
  // 边界 2：本端不认识的码信帧上那一位，别信折算后的 1501（它恒为 true）。
  const bool retryable = def != nullptr ? def->retryable : wireRetryable;
  if (retryable) return false;

  if (code == codeValue(ErrorCode::TokenInvalid)) {
    out = KickedReason::AuthExpired;
  } else if (code == codeValue(ErrorCode::KickedOut)) {
    out = KickedReason::TakenOver;
  } else {
    out = KickedReason::ConfigRejected;
  }
  return true;
}

bool deviceIdValid(const std::string& deviceId) {
  if (deviceId.empty() || deviceId.size() > kDeviceIdMaxBytes) return false;
  for (const char c : deviceId) {
    if (!allowedChar(c)) return false;
  }
  return true;
}

}  // namespace imrtc
