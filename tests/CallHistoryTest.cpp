#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "EngineHarness.h"
#include "TestHarness.h"
#include "imrtc/CallHistory.h"
#include "imrtc/HttpClient.h"

using enginetest::Harness;
using imrtc::ActionResult;
using imrtc::CallHistoryPage;
using imrtc::HttpResponse;

/**
 * 通话记录（`fetchCallHistory`，`GET /v1/calls`）：地址推导、请求拼装、分页「到底」判据、
 * 错误映射、未登录 / 没注入 HTTP / 析构时悬着的结果。与 iOS / Android / Web 四端同一批用例。
 */
namespace {

/** 假 HttpClient：请求记下来，结果由测试在 poll() 里放出（与真实实现同一个「只在 poll 里回」的约定）。 */
struct HttpState {
  struct Request {
    std::string url;
    std::string token;
    imrtc::HttpCompletion done;
  };
  std::vector<Request> requests;
  std::deque<std::pair<std::size_t, HttpResponse>> ready;  // 请求下标 → 应答
};

class FakeHttp : public imrtc::HttpClient {
public:
  explicit FakeHttp(std::shared_ptr<HttpState> state) : state_(std::move(state)) {}
  void get(const std::string& url, const std::string& token, std::int64_t, imrtc::HttpCompletion done) override {
    state_->requests.push_back({url, token, std::move(done)});
  }
  void poll() override {
    while (!state_->ready.empty()) {
      auto item = state_->ready.front();
      state_->ready.pop_front();
      state_->requests[item.first].done(item.second);
    }
  }

private:
  std::shared_ptr<HttpState> state_;
};

imrtc::HttpClientFactory factoryOf(const std::shared_ptr<HttpState>& state) {
  return [state]() -> std::unique_ptr<imrtc::HttpClient> { return std::unique_ptr<imrtc::HttpClient>(new FakeHttp(state)); };
}

HttpResponse ok(const std::string& body) {
  HttpResponse response;
  response.status = 200;
  response.body = body;
  return response;
}

std::string body(int count, std::int64_t next = 0) {
  std::string calls;
  for (int i = 0; i < count; ++i) {
    if (i > 0) calls += ",";
    calls += "{\"call_id\":\"c" + std::to_string(i) +
             "\",\"caller\":\"alice\",\"media_type\":\"video\",\"is_group\":false,\"reason\":\"hangup\","
             "\"duration_sec\":12,\"started_at_ms\":" + std::to_string(1000 - i) +
             ",\"members\":[{\"uid\":\"bob\",\"state\":\"joined\"}]}";
  }
  std::string text = "{\"calls\":[" + calls + "]";
  if (next > 0) text += ",\"next_cursor\":" + std::to_string(next);
  return text + "}";
}

/** Captured 记下一次查询的结果，数一数回了几次。 */
struct Captured {
  int calls = 0;
  ActionResult result;
  CallHistoryPage page;

  imrtc::CallHistoryCompletion sink() {
    return [this](const ActionResult& r, const CallHistoryPage& p) {
      ++calls;
      result = r;
      page = p;
    };
  }
};

}  // namespace

IMRTC_TEST(callHistoryRestBase, "通话记录 —— 信令地址推出 REST 根") {
  CHECK_EQ(imrtc::restBaseUrl("ws://127.0.0.1:8787/v1/ws"), std::string("http://127.0.0.1:8787"), "ws");
  CHECK_EQ(imrtc::restBaseUrl("wss://rtc.example.com/v1/ws"), std::string("https://rtc.example.com"), "wss");
  CHECK_EQ(imrtc::restBaseUrl("wss://rtc.example.com/gw/v1/ws?x=1"), std::string("https://rtc.example.com/gw"),
           "前缀路径 + 查询串");
  CHECK_EQ(imrtc::restBaseUrl("https://rtc.example.com/v1/ws"), std::string("https://rtc.example.com"), "https");
  CHECK_EQ(imrtc::restBaseUrl("ftp://h/v1/ws"), std::string(), "不认识的协议");
  CHECK_EQ(imrtc::restBaseUrl("not a url"), std::string(), "不是地址");
  CHECK_EQ(imrtc::restBaseUrl("ws:///v1/ws"), std::string(), "没有主机");
}

IMRTC_TEST(callHistoryUrl, "通话记录 —— 请求带 limit，cursor 只在 >0 时带，limit 夹在 1..200") {
  CHECK_EQ(imrtc::callHistoryUrl("ws://h:8787/v1/ws", 20, 0), std::string("http://h:8787/v1/calls?limit=20"), "首页");
  CHECK_EQ(imrtc::callHistoryUrl("ws://h:8787/v1/ws", 20, 1700000000000),
           std::string("http://h:8787/v1/calls?limit=20&cursor=1700000000000"), "下一页");
  CHECK_EQ(imrtc::callHistoryUrl("ftp://h", 20, 0), std::string(), "推不出根");
  CHECK_EQ(imrtc::clampCallHistoryLimit(9999), static_cast<std::int64_t>(200), "上限");
  CHECK_EQ(imrtc::clampCallHistoryLimit(0), static_cast<std::int64_t>(1), "下限");
  CHECK_EQ(imrtc::clampCallHistoryLimit(-5), static_cast<std::int64_t>(1), "负数");
  CHECK_EQ(imrtc::clampCallHistoryLimit(20), static_cast<std::int64_t>(20), "原值");
}

IMRTC_TEST(callHistoryPaging, "通话记录 —— 满页交出游标，不满页就是到底") {
  const imrtc::CallHistoryOutcome full = imrtc::parseCallHistory(200, body(2, 999), 2);
  CHECK_TRUE(full.result.ok(), "满页成功");
  CHECK_EQ(full.page.records.size(), static_cast<std::size_t>(2), "满页条数");
  CHECK_TRUE(full.page.hasNext, "满页有下一页");
  CHECK_EQ(full.page.nextCursor, static_cast<std::int64_t>(999), "游标");
  CHECK_EQ(full.page.records[0].callId, std::string("c0"), "call_id");
  CHECK_EQ(full.page.records[0].durationSec, static_cast<std::int64_t>(12), "时长");
  CHECK_EQ(full.page.records[0].mediaType, std::string("video"), "媒体");
  CHECK_EQ(full.page.records[0].members.size(), static_cast<std::size_t>(1), "成员数");
  CHECK_EQ(full.page.records[0].members[0].uid, std::string("bob"), "成员");

  CHECK_TRUE(!imrtc::parseCallHistory(200, body(1, 999), 2).page.hasNext, "不满一页不能再给游标");
  const imrtc::CallHistoryOutcome empty = imrtc::parseCallHistory(200, "{\"calls\":[]}", 2);
  CHECK_TRUE(empty.result.ok() && empty.page.records.empty() && !empty.page.hasNext, "空页到底");
}

IMRTC_TEST(callHistoryMissingFields, "通话记录 —— 缺字段一律解成零值") {
  const imrtc::CallHistoryOutcome outcome = imrtc::parseCallHistory(200, "{\"calls\":[{\"call_id\":\"c1\"}]}", 20);
  CHECK_TRUE(outcome.result.ok(), "成功");
  const imrtc::CallHistoryRecord& record = outcome.page.records.at(0);
  CHECK_EQ(record.callId, std::string("c1"), "call_id");
  CHECK_EQ(record.reason, std::string(), "reason");
  CHECK_EQ(record.durationSec, static_cast<std::int64_t>(0), "时长");
  CHECK_TRUE(!record.isGroup && record.members.empty(), "群标记与成员");
}

IMRTC_TEST(callHistoryStatusMapping, "通话记录 —— 状态码与坏应答映射到错误码") {
  CHECK_EQ(imrtc::parseCallHistory(401, "", 20).result.code, 1101, "401");
  CHECK_EQ(imrtc::parseCallHistory(500, "", 20).result.code, 1501, "500");
  CHECK_EQ(imrtc::parseCallHistory(200, "nope", 20).result.code, 1501, "坏 JSON");
  CHECK_EQ(imrtc::parseCallHistory(200, "[1,2]", 20).result.code, 1501, "顶层不是对象");
}

IMRTC_TEST(callHistoryNotLoggedIn, "通话记录 —— 没登录 2007（不发请求）；logout 之后同样") {
  auto http = std::make_shared<HttpState>();
  Harness h("mac-8f3a", factoryOf(http));
  Captured before;
  h.engine->fetchCallHistory(20, 0, before.sink());
  CHECK_EQ(before.calls, 1, "就地回一次");
  CHECK_EQ(before.result.code, 2007, "没登录");
  CHECK_TRUE(http->requests.empty(), "没发请求");

  h.login();
  h.engine->logout();
  Captured after;
  h.engine->fetchCallHistory(20, 0, after.sink());
  CHECK_EQ(after.result.code, 2007, "logout 之后");
  CHECK_TRUE(http->requests.empty(), "仍没发请求");
}

IMRTC_TEST(callHistoryNoHttpClient, "通话记录 —— 没注入 HttpClient 回 2005") {
  Harness h;
  h.login();
  Captured got;
  h.engine->fetchCallHistory(20, 0, got.sink());
  CHECK_EQ(got.calls, 1, "回一次");
  CHECK_EQ(got.result.code, 2005, "invalid_state");
}

IMRTC_TEST(callHistoryRequestAndDelivery, "通话记录 —— 带当前票（含 updateToken）与夹住的 limit，结果只在 tick 里回、恰好一次") {
  auto http = std::make_shared<HttpState>();
  Harness h("mac-8f3a", factoryOf(http));
  h.login();
  h.engine->updateToken("tk-2");

  Captured first;
  h.engine->fetchCallHistory(9999, 0, first.sink());
  CHECK_EQ(http->requests.size(), static_cast<std::size_t>(1), "发了一个请求");
  CHECK_EQ(http->requests[0].url, std::string("https://rtc.example.com/v1/calls?limit=200"), "地址");
  CHECK_EQ(http->requests[0].token, std::string("tk-2"), "用换过的票");
  CHECK_EQ(first.calls, 0, "tick 之前不回");

  http->ready.emplace_back(0, ok(body(2, 999)));
  h.engine->tick();
  CHECK_EQ(first.calls, 1, "tick 里回一次");
  CHECK_TRUE(first.result.ok(), "成功");
  CHECK_EQ(first.page.records.size(), static_cast<std::size_t>(2), "两条");
  h.engine->tick();
  CHECK_EQ(first.calls, 1, "不会回第二次");

  Captured next;
  h.engine->fetchCallHistory(2, 999, next.sink());
  CHECK_EQ(http->requests[1].url, std::string("https://rtc.example.com/v1/calls?limit=2&cursor=999"), "下一页地址");
  http->ready.emplace_back(1, ok(body(2, 500)));
  h.engine->tick();
  CHECK_TRUE(next.page.hasNext && next.page.nextCursor == 500, "满页交出游标");
}

IMRTC_TEST(callHistoryNetworkAndAuthErrors, "通话记录 —— 网络错误 2003，401 回 1101") {
  auto http = std::make_shared<HttpState>();
  Harness h("mac-8f3a", factoryOf(http));
  h.login();

  Captured down;
  h.engine->fetchCallHistory(20, 0, down.sink());
  HttpResponse failed;
  failed.error = "cannot connect";
  http->ready.emplace_back(0, failed);
  h.engine->tick();
  CHECK_EQ(down.result.code, 2003, "网络不通");
  CHECK_TRUE(down.page.records.empty(), "失败没有记录");

  Captured denied;
  h.engine->fetchCallHistory(20, 0, denied.sink());
  HttpResponse unauthorized;
  unauthorized.status = 401;
  http->ready.emplace_back(1, unauthorized);
  h.engine->tick();
  CHECK_EQ(denied.result.code, 1101, "票被拒");
}

IMRTC_TEST(callHistoryLoginRejected, "通话记录 —— 登录被拒后回 2007，不拿被拒的票发请求") {
  auto http = std::make_shared<HttpState>();
  Harness h("mac-8f3a", factoryOf(http));
  std::int32_t loginCode = 0;
  h.engine->login("tk-bad", [&loginCode](const imrtc::ActionResult& r) { loginCode = r.code; });
  h.net.open();
  h.reply(imrtc::frame::kError,
          enginetest::Json::parse("{\"code\":1101,\"name\":\"token_invalid\",\"msg\":\"token invalid\","
                      "\"for_type\":\"sys.hello\",\"retryable\":false}"));
  CHECK_EQ(loginCode, std::int32_t{1101}, "登录被拒");

  Captured got;
  h.engine->fetchCallHistory(20, 0, got.sink());
  CHECK_EQ(got.result.code, 2007, "没登录");
  CHECK_TRUE(http->requests.empty(), "没发请求");
}

IMRTC_TEST(callHistoryDestroyedWhilePending, "通话记录 —— 引擎析构时还没回来的查询回 2005") {
  auto http = std::make_shared<HttpState>();
  Captured got;
  {
    Harness h("mac-8f3a", factoryOf(http));
    h.login();
    h.engine->fetchCallHistory(20, 0, got.sink());
    CHECK_EQ(got.calls, 0, "还在路上");
  }
  CHECK_EQ(got.calls, 1, "析构时回一次");
  CHECK_EQ(got.result.code, 2005, "invalid_state");
}
