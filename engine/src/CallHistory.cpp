#include "imrtc/CallHistory.h"

#include <algorithm>
#include <cctype>
#include <exception>

#include "imrtc/Errors.h"
#include "imrtc/Json.h"

namespace imrtc {
namespace {

ActionResult failure(ErrorCode code) {
  return ActionResult{codeValue(code), errorName(code), "", ""};
}

std::string str(const Json& object, const char* key) {
  const Json* value = object.find(key);
  return value != nullptr && value->isString() ? value->asString() : std::string();
}

std::int64_t num(const Json& object, const char* key) {
  const Json* value = object.find(key);
  return value != nullptr && value->isInt() ? value->asInt() : 0;
}

CallHistoryRecord toRecord(const Json& object) {
  CallHistoryRecord record;
  record.callId = str(object, "call_id");
  record.roomId = str(object, "room_id");
  record.caller = str(object, "caller");
  record.mediaType = str(object, "media_type");
  const Json* isGroup = object.find("is_group");
  record.isGroup = isGroup != nullptr && isGroup->isBool() && isGroup->asBool();
  record.reason = str(object, "reason");
  record.endedBy = str(object, "ended_by");
  record.durationSec = num(object, "duration_sec");
  record.startedAtMs = num(object, "started_at_ms");
  record.connectedAtMs = num(object, "connected_at_ms");
  record.endedAtMs = num(object, "ended_at_ms");
  record.userData = str(object, "user_data");
  record.chatGroupId = str(object, "chat_group_id");
  const Json* members = object.find("members");
  if (members != nullptr && members->isArray()) {
    for (const Json& item : members->items()) {
      if (item.isObject()) record.members.push_back(CallHistoryMember{str(item, "uid"), str(item, "state")});
    }
  }
  return record;
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

}  // namespace

std::int64_t clampCallHistoryLimit(std::int64_t limit) {
  return std::min(std::max(limit, static_cast<std::int64_t>(1)), kMaxCallHistoryLimit);
}

std::string restBaseUrl(const std::string& signalingUrl) {
  const std::size_t sep = signalingUrl.find("://");
  if (sep == std::string::npos) return "";
  std::string scheme = lower(signalingUrl.substr(0, sep));
  if (scheme == "ws") {
    scheme = "http";
  } else if (scheme == "wss") {
    scheme = "https";
  } else if (scheme != "http" && scheme != "https") {
    return "";
  }
  std::string rest = signalingUrl.substr(sep + 3);
  // 查询串与片段与 REST 根无关。
  rest = rest.substr(0, rest.find_first_of("?#"));
  static const std::string kWsPath = "/v1/ws";
  if (rest.size() >= kWsPath.size() &&
      rest.compare(rest.size() - kWsPath.size(), kWsPath.size(), kWsPath) == 0) {
    rest.erase(rest.size() - kWsPath.size());
  }
  while (!rest.empty() && rest.back() == '/') rest.pop_back();
  // 必须有主机部分：`ws:///v1/ws` 这类推不出根。
  if (rest.empty() || rest.front() == '/') return "";
  return scheme + "://" + rest;
}

std::string callHistoryUrl(const std::string& signalingUrl, std::int64_t limit, std::int64_t cursor) {
  const std::string base = restBaseUrl(signalingUrl);
  if (base.empty()) return "";
  std::string url = base + "/v1/calls?limit=" + std::to_string(limit);
  if (cursor > 0) url += "&cursor=" + std::to_string(cursor);
  return url;
}

CallHistoryOutcome parseCallHistory(std::int32_t status, const std::string& body, std::int64_t limit) {
  CallHistoryOutcome outcome;
  if (status == 401) {
    outcome.result = failure(ErrorCode::TokenInvalid);
    return outcome;
  }
  if (status != 200) {
    outcome.result = failure(ErrorCode::Internal);
    return outcome;
  }
  Json root;
  try {
    root = Json::parse(body);
  } catch (const std::exception&) {
    outcome.result = failure(ErrorCode::Internal);
    return outcome;
  }
  if (!root.isObject()) {
    outcome.result = failure(ErrorCode::Internal);
    return outcome;
  }
  const Json* calls = root.find("calls");
  if (calls != nullptr && calls->isArray()) {
    for (const Json& item : calls->items()) {
      if (item.isObject()) outcome.page.records.push_back(toRecord(item));
    }
  }
  const Json* next = root.find("next_cursor");
  const bool full = static_cast<std::int64_t>(outcome.page.records.size()) >= limit;
  if (full && next != nullptr && next->isInt() && next->asInt() > 0) {
    outcome.page.hasNext = true;
    outcome.page.nextCursor = next->asInt();
  }
  return outcome;
}

}  // namespace imrtc
