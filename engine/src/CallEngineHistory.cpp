#include <utility>

#include "imrtc/CallEngine.h"
#include "imrtc/Errors.h"

namespace imrtc {

/**
 * 通话记录查询（`GET /v1/calls`）。与 CallEngineSession.cpp 一样是从门面拆出来的一块：
 * 它跟状态机没有耦合，只用票、信令地址和一个可注入的 HttpClient。
 * URL 拼装与应答解析在 CallHistory.cpp（纯函数、可单测），这里只做接线与结果投递。
 */

namespace {

void deliver(const CallHistoryCompletion& done, ErrorCode code) {
  if (done) done(ActionResult{codeValue(code), errorName(code), "", ""}, CallHistoryPage());
}

}  // namespace

void CallEngine::fetchCallHistory(std::int64_t limit, std::int64_t cursor, CallHistoryCompletion done) {
  if (ticket_.empty()) return deliver(done, ErrorCode::NotLoggedIn);
  if (!options_.httpClientFactory) return deliver(done, ErrorCode::InvalidState);
  const std::int64_t clamped = clampCallHistoryLimit(limit);
  const std::string url = callHistoryUrl(options_.url, clamped, cursor);
  if (url.empty()) return deliver(done, ErrorCode::BadParams);
  if (!http_) http_ = options_.httpClientFactory();
  if (!http_) return deliver(done, ErrorCode::InvalidState);

  const std::uint64_t id = ++historySeq_;
  historyPending_[id] = std::move(done);
  http_->get(url, ticket_, options_.requestTimeoutMs, [this, id, clamped](const HttpResponse& response) {
    const auto entry = historyPending_.find(id);
    if (entry == historyPending_.end()) return;
    CallHistoryCompletion finish = std::move(entry->second);
    historyPending_.erase(entry);
    if (!finish) return;
    if (response.failed()) {
      const ErrorCode code = ErrorCode::NetworkUnreachable;
      finish(ActionResult{codeValue(code), errorName(code), "", ""}, CallHistoryPage());
      return;
    }
    const CallHistoryOutcome outcome = parseCallHistory(response.status, response.body, clamped);
    finish(outcome.result, outcome.page);
  });
}

}  // namespace imrtc
