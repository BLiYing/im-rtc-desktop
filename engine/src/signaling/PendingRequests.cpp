#include "imrtc/PendingRequests.h"

#include <utility>
#include <vector>

#include "imrtc/Errors.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/** failureOf 拼一个失败结果。 */
RequestResult failureOf(std::int32_t code, const std::string& forType) {
  RequestResult result;
  result.ok = false;
  result.errorCode = code;
  result.errorName = errorName(code);
  result.forType = forType;
  return result;
}

}  // namespace

PendingRequests::PendingRequests(std::int64_t timeoutMs) : timeoutMs_(timeoutMs) {}

void PendingRequests::track(const std::string& reqId, const std::string& type, std::int64_t nowMs,
                            ResponseHandler handler) {
  waiters_[reqId] = Waiter{type, nowMs + timeoutMs_, std::move(handler)};
}

void PendingRequests::abandon(const std::string& reqId) { waiters_.erase(reqId); }

void PendingRequests::finish(const std::string& reqId, const RequestResult& result) {
  const auto it = waiters_.find(reqId);
  if (it == waiters_.end()) return;
  // **先摘表再回调**：回调里很可能会发下一个请求，表还留着旧条目就会被重复结算。
  ResponseHandler handler = std::move(it->second.handler);
  waiters_.erase(it);
  if (handler) handler(result);
}

bool PendingRequests::settle(const Envelope& envelope, const DecodeFn& decode) {
  const auto it = waiters_.find(envelope.reqId);
  if (it == waiters_.end()) return false;

  const std::string type = it->second.type;
  if (envelope.type == frame::kError) {
    const Json* code = envelope.data.find("code");
    const Json* forType = envelope.data.find("for_type");
    finish(envelope.reqId,
           failureOf(code != nullptr && code->isInt() ? static_cast<std::int32_t>(code->asInt())
                                                      : codeValue(ErrorCode::Internal),
                     forType != nullptr && forType->isString() ? forType->asString() : type));
    return true;
  }

  RequestResult result;
  result.ok = true;
  result.forType = type;
  result.envelope = envelope;
  result.data = decode ? decode(envelope) : envelope.data;
  finish(envelope.reqId, result);
  return true;
}

void PendingRequests::expire(std::int64_t nowMs) {
  // 先收集再结算：回调里可能会 track 新请求，边遍历边改 map 是未定义行为。
  std::vector<std::pair<std::string, std::string>> expired;
  for (const auto& entry : waiters_) {
    if (entry.second.deadlineMs <= nowMs) expired.emplace_back(entry.first, entry.second.type);
  }
  for (const auto& entry : expired) {
    finish(entry.first, failureOf(codeValue(ErrorCode::SignalingTimeout), entry.second));
  }
}

void PendingRequests::failAll(std::int32_t errorCode) {
  std::vector<std::pair<std::string, std::string>> all;
  for (const auto& entry : waiters_) all.emplace_back(entry.first, entry.second.type);
  for (const auto& entry : all) finish(entry.first, failureOf(errorCode, entry.second));
}

}  // namespace imrtc
