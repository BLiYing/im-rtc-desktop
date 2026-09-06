#include "imrtc/Reasons.h"

#include <algorithm>

#include "imrtc/Enums.h"
#include "imrtc/Json.h"

namespace imrtc {

namespace reason {
const char* const kHangup = "hangup";
const char* const kCancel = "cancel";
const char* const kReject = "reject";
const char* const kNoAnswer = "no_answer";
const char* const kBusy = "busy";
const char* const kOffline = "offline";
const char* const kAnsweredElsewhere = "answered_elsewhere";
const char* const kRejectedElsewhere = "rejected_elsewhere";
const char* const kKicked = "kicked";
const char* const kRoomClosed = "room_closed";
const char* const kNetwork = "network";
const char* const kError = "error";
}  // namespace reason

std::string normalizeReason(const std::string& value) {
  const EnumValues& known = reasonValues();
  return std::find(known.begin(), known.end(), value) == known.end() ? reason::kError : value;
}

std::string normalizeReason(const Json& value) {
  // 非字符串（缺失、数字、对象）一律折成 error——**不许崩，也不许透传**。
  return value.isString() ? normalizeReason(value.asString()) : std::string(reason::kError);
}

const std::vector<std::string>& groupDominantPriority() {
  static const std::vector<std::string> kPriority = {reason::kReject, reason::kBusy,
                                                     reason::kNoAnswer, reason::kOffline};
  return kPriority;
}

std::string dominantReason(const std::vector<std::string>& outcomes) {
  for (const std::string& candidate : groupDominantPriority()) {
    if (std::find(outcomes.begin(), outcomes.end(), candidate) != outcomes.end()) {
      return candidate;
    }
  }
  return reason::kNoAnswer;
}

std::int64_t callDurationSec(std::int64_t connectedAtMs, std::int64_t endedAtMs) {
  if (connectedAtMs <= 0) return 0;
  const std::int64_t elapsedMs = endedAtMs - connectedAtMs;
  if (elapsedMs <= 0) return 0;
  return elapsedMs / 1000;
}

}  // namespace imrtc
