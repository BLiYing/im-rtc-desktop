#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "EngineHarness.h"
#include "FakeTransport.h"
#include "TestHarness.h"
#include "imrtc/CallEngine.h"
#include "imrtc/Registry.h"

using enginetest::Harness;
using imrtc::ActionCompletion;
using imrtc::ActionResult;
using imrtc::CallEngine;
using imrtc::CallState;
using imrtc::Json;
using imrtc::RoomState;

/**
 * **调用结果回给调用方**（server `docs/design/ACTION_RESULT_DESIGN.md`，2.0.0）。
 *
 * 每个发起类方法四格：成功 / 本地拒绝 2005 / 服务端拒绝 / 等应答期间断线 2003；
 * 每格断言结果**恰好一次**、失败时**没有**多发 onError。另有：不传回调退回 onError（R7）、
 * 连锁帧失败走 onError(for_type)（R2）、退出类失败本地收场（D2）、析构时悬着的结果回 2005（R5）。
 */
namespace {

/** Captured 记下一次调用的结果，数一数回了几次。 */
struct Captured {
  int calls = 0;
  ActionResult last;

  ActionCompletion sink() {
    return [this](const ActionResult& result) {
      ++calls;
      last = result;
    };
  }
};

int errorCount(const Harness& harness) {
  int count = 0;
  for (const std::string& line : harness.recorder->log) {
    if (line.rfind("error:", 0) == 0) ++count;
  }
  return count;
}

void ringing(Harness& h) {
  h.event(imrtc::frame::kCallIncoming,
          Json::parse("{\"call_id\":\"call-9\",\"caller\":\"bob\",\"callee_ids\":[\"alice\"],"
                      "\"media_type\":\"audio\",\"is_group\":true}"));
}

void inviting(Harness& h) {
  h.engine->call({"bob"}, "audio", false);
  h.reply(imrtc::okType(imrtc::frame::kCallInvite),
          Json::parse("{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"room_token\":\"rt\"}"));
}

void inCall(Harness& h) {
  ringing(h);
  h.engine->accept();
  h.reply(imrtc::okType(imrtc::frame::kCallAccept), Json::makeObject());
  h.event(imrtc::frame::kCallConnected,
          Json::parse("{\"call_id\":\"call-9\",\"room_id\":\"r-9\",\"room_token\":\"tk\","
                      "\"media_type\":\"audio\",\"is_group\":true,"
                      "\"connected_at_ms\":1756876812000,\"accepted_by\":\"alice\"}"));
  h.reply(imrtc::okType(imrtc::frame::kRoomJoin),
          Json::parse("{\"room_id\":\"r-9\",\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));
}

void inMeeting(Harness& h) {
  h.engine->joinRoom("m-1", "tk");
  h.reply(imrtc::okType(imrtc::frame::kRoomJoin),
          Json::parse("{\"room_id\":\"m-1\",\"participant_id\":\"p-1\",\"participants\":[],\"tracks\":[]}"));
}

void idle(Harness&) {}

struct MethodCase {
  std::string name;
  std::function<void(Harness&)> ready;
  std::function<void(Harness&)> wrongState;
  std::function<void(CallEngine&, ActionCompletion)> invoke;
  /** 这次调用直接发出的那一帧。 */
  std::string frame;
  std::string okData = "{}";
  std::string value;
};

std::vector<MethodCase> methods() {
  return {
      {"call", idle, ringing,
       [](CallEngine& e, ActionCompletion d) { e.call({"bob"}, "audio", false, std::move(d)); },
       imrtc::frame::kCallInvite, "{\"call_id\":\"call-7\",\"room_id\":\"r-7\",\"room_token\":\"rt\"}",
       "call-7"},
      {"joinCall", idle, ringing,
       [](CallEngine& e, ActionCompletion d) { e.joinCall("call-5", std::move(d)); },
       imrtc::frame::kCallJoin, "{}", ""},
      {"accept", ringing, idle, [](CallEngine& e, ActionCompletion d) { e.accept(std::move(d)); },
       imrtc::frame::kCallAccept, "{}", ""},
      {"reject", ringing, idle, [](CallEngine& e, ActionCompletion d) { e.reject(std::move(d)); },
       imrtc::frame::kCallReject, "{}", ""},
      {"cancel", inviting, idle, [](CallEngine& e, ActionCompletion d) { e.cancel(std::move(d)); },
       imrtc::frame::kCallCancel, "{}", ""},
      {"hangup", inCall, idle, [](CallEngine& e, ActionCompletion d) { e.hangup(std::move(d)); },
       imrtc::frame::kCallHangup, "{}", ""},
      {"inviteMore", inCall, idle,
       [](CallEngine& e, ActionCompletion d) { e.inviteMore({"carol"}, std::move(d)); },
       imrtc::frame::kCallInviteMore, "{}", ""},
      {"joinRoom", idle, inMeeting,
       [](CallEngine& e, ActionCompletion d) { e.joinRoom("m-2", "tk", std::move(d)); },
       imrtc::frame::kRoomJoin,
       "{\"room_id\":\"m-2\",\"participant_id\":\"p-2\",\"participants\":[],\"tracks\":[]}", ""},
      {"leaveRoom", inMeeting, idle, [](CallEngine& e, ActionCompletion d) { e.leaveRoom(std::move(d)); },
       imrtc::frame::kRoomLeave, "{}", ""},
  };
}

void success(const MethodCase& m) {
  Harness h;
  h.login();
  m.ready(h);
  Captured captured;
  m.invoke(*h.engine, captured.sink());
  CHECK_EQ(h.lastType(), m.frame, m.name + " 成功：发出直接那一帧");
  CHECK_EQ(captured.calls, 0, m.name + " 成功：应答回来之前不结算");
  h.reply(imrtc::okType(m.frame), Json::parse(m.okData));
  CHECK_EQ(captured.calls, 1, m.name + " 成功：结果恰好一次");
  CHECK_EQ(captured.last.code, std::int32_t{0}, m.name + " 成功：code 0");
  CHECK_EQ(captured.last.value, m.value, m.name + " 成功值");
  CHECK_EQ(errorCount(h), 0, m.name + " 成功：没有 onError");
}

void localReject(const MethodCase& m) {
  Harness h;
  h.login();
  m.wrongState(h);
  const std::string before = h.lastType();
  Captured captured;
  m.invoke(*h.engine, captured.sink());
  CHECK_EQ(captured.calls, 1, m.name + " 本地拒绝：就地回一次");
  CHECK_EQ(captured.last.code, std::int32_t{2005}, m.name + " 本地拒绝：2005");
  CHECK_EQ(h.lastType(), before, m.name + " 本地拒绝：不发帧");
  CHECK_EQ(errorCount(h), 0, m.name + " 本地拒绝：没有 onError");
}

void serverReject(const MethodCase& m) {
  Harness h;
  h.login();
  m.ready(h);
  Captured captured;
  m.invoke(*h.engine, captured.sink());
  h.reply(imrtc::frame::kError,
          Json::parse("{\"code\":1501,\"name\":\"internal\",\"msg\":\"internal error\",\"for_type\":\"" +
                      m.frame + "\",\"retryable\":false}"));
  CHECK_EQ(captured.calls, 1, m.name + " 服务端拒绝：结果恰好一次");
  CHECK_EQ(captured.last.code, std::int32_t{1501}, m.name + " 服务端拒绝：那个码");
  CHECK_EQ(captured.last.forType, m.frame, m.name + " 服务端拒绝：for_type");
  CHECK_EQ(errorCount(h), 0, m.name + " 服务端拒绝：没有 onError");
}

void disconnected(const MethodCase& m) {
  Harness h;
  h.login();
  m.ready(h);
  Captured captured;
  m.invoke(*h.engine, captured.sink());
  h.net.remoteClose(1006);
  CHECK_EQ(captured.calls, 1, m.name + " 断线：结果恰好一次");
  CHECK_EQ(captured.last.code, std::int32_t{2003}, m.name + " 断线：2003");
  CHECK_EQ(errorCount(h), 0, m.name + " 断线：没有 onError");
}

}  // namespace

IMRTC_TEST(actionResultFourCells, "调用结果 —— 每个发起类方法：成功 / 本地拒绝 / 服务端拒绝 / 断线") {
  for (const MethodCase& m : methods()) {
    success(m);
    localReject(m);
    serverReject(m);
    disconnected(m);
  }
}

IMRTC_TEST(actionResultTimeout, "调用结果 —— 请求超时回 2004，不发 onError") {
  Harness h;
  h.login();
  Captured captured;
  h.engine->joinRoom("m-1", "tk", captured.sink());
  h.now += 11000;
  h.engine->tick();
  CHECK_EQ(captured.calls, 1, "结果恰好一次");
  CHECK_EQ(captured.last.code, std::int32_t{2004}, "2004 signaling_timeout");
  CHECK_EQ(h.engine->roomState(), RoomState::Idle, "进房超时照样退回 idle");
  CHECK_EQ(errorCount(h), 0, "没有 onError");
}

IMRTC_TEST(actionResultNotLoggedIn, "调用结果 —— 没登录就拨号：先 onCallEnd(error)，再回 2007") {
  Harness h;
  std::vector<std::string> order;
  h.engine->call({"bob"}, "audio", false, [&](const ActionResult& result) {
    order.push_back("result:" + std::to_string(result.code));
    order.insert(order.begin(), h.recorder->log.begin(), h.recorder->log.end());
  });
  CHECK_EQ(order, std::vector<std::string>({"callEnd:error/0", "result:2007"}),
           "状态事件先于结果，且没有 onError");
  CHECK_EQ(h.engine->callState(), CallState::Idle, "退回 idle");
}

IMRTC_TEST(actionResultNoCallbackFallsBackToOnError,
           "调用结果 —— 不传回调：失败退回 onError（R7），本地拒绝也是") {
  Harness h;
  h.login();
  h.engine->accept();
  CHECK_EQ(h.recorder->log.back(), std::string("error:2005/invalid_state"), "本地拒绝退回 onError");

  h.engine->joinCall("call-5");
  h.reply(imrtc::frame::kError,
          Json::parse("{\"code\":1409,\"name\":\"invite_denied\",\"msg\":\"invite denied by host\","
                      "\"for_type\":\"call.join\",\"retryable\":false}"));
  CHECK_EQ(h.recorder->log.at(h.recorder->log.size() - 2), std::string("error:1409/invite_denied"),
           "服务端拒绝退回 onError");
  CHECK_EQ(h.recorder->log.back(), std::string("callEnd:error/0"), "joinCall 被拒照发 callEnd(error)");
}

IMRTC_TEST(actionResultJoinCallRejectedEndsCall,
           "调用结果 —— joinCall 被拒：先 onCallEnd(error) 再回码，状态回 idle") {
  Harness h;
  h.login();
  Captured captured;
  h.engine->joinCall("call-5", [&](const ActionResult& result) {
    ++captured.calls;
    captured.last = result;
    CHECK_EQ(h.recorder->log.back(), std::string("callEnd:error/0"), "结果回来时 callEnd 已经抛过");
  });
  h.reply(imrtc::frame::kError,
          Json::parse("{\"code\":1202,\"name\":\"room_full\",\"msg\":\"room is full\","
                      "\"for_type\":\"call.join\",\"retryable\":false}"));
  CHECK_EQ(captured.calls, 1, "恰好一次");
  CHECK_EQ(captured.last.code, std::int32_t{1202}, "满员");
  CHECK_EQ(h.engine->callState(), CallState::Idle, "回 idle");
}

IMRTC_TEST(actionResultSelfInCallees, "调用结果 —— 名单里有自己：call 抛 callEnd(error) + 1004，inviteMore 只回 1004") {
  Harness h;
  h.login();
  Captured call;
  h.engine->call({"bob", "alice"}, "audio", true, call.sink());
  CHECK_EQ(call.last.code, std::int32_t{1004}, "call 回 1004");
  CHECK_EQ(call.last.forType, std::string("call.invite"), "for_type");
  CHECK_EQ(h.recorder->log.back(), std::string("callEnd:error/0"), "界面收场信号");
  CHECK_EQ(h.lastType(), std::string("sys.hello"), "不上线路");

  inCall(h);
  const std::size_t before = h.recorder->log.size();
  Captured invite;
  h.engine->inviteMore({"alice"}, invite.sink());
  CHECK_EQ(invite.last.code, std::int32_t{1004}, "inviteMore 回 1004");
  CHECK_EQ(h.recorder->log.size(), before, "加人被拒不动通话、不发任何事件");
  CHECK_EQ(errorCount(h), 0, "没有 onError");
}

IMRTC_TEST(actionResultChainedFrameGoesToOnError,
           "调用结果 —— accept 已成功；随后自动发的 room.join 被拒走 onError(for_type=room.join)") {
  Harness h;
  h.login();
  ringing(h);
  Captured captured;
  h.engine->accept(captured.sink());
  h.reply(imrtc::okType(imrtc::frame::kCallAccept), Json::makeObject());
  CHECK_EQ(captured.last.code, std::int32_t{0}, "accept 在 call.accept.ok 时就成功");
  h.event(imrtc::frame::kCallConnected,
          Json::parse("{\"call_id\":\"call-9\",\"room_id\":\"r-9\",\"room_token\":\"tk\","
                      "\"media_type\":\"audio\",\"is_group\":true,"
                      "\"connected_at_ms\":1756876812000,\"accepted_by\":\"alice\"}"));
  h.reply(imrtc::frame::kError,
          Json::parse("{\"code\":1201,\"name\":\"room_not_found\",\"msg\":\"room not found\","
                      "\"for_type\":\"room.join\",\"retryable\":false}"));
  CHECK_EQ(captured.calls, 1, "accept 的结果不会因为连锁帧再回一次");
  CHECK_EQ(errorCount(h), 1, "连锁帧失败找不到调用方，发一条 onError");
  CHECK_EQ(h.recorder->errorForTypes.back(), std::string("room.join"), "带 for_type");
}

IMRTC_TEST(actionResultExitFailureEndsLocally,
           "调用结果 —— hangup 被拒（1402）：本地照样收场、callEnd 只一次，迟到的 call.ended 不再抛") {
  Harness h;
  h.login();
  inCall(h);
  Captured captured;
  h.engine->hangup(captured.sink());
  h.reply(imrtc::frame::kError,
          Json::parse("{\"code\":1402,\"name\":\"call_ended\",\"msg\":\"call already ended\","
                      "\"for_type\":\"call.hangup\",\"retryable\":false}"));
  CHECK_EQ(captured.last.code, std::int32_t{1402}, "错误只供日志");
  CHECK_EQ(h.engine->callState(), CallState::Idle, "通话回 idle");
  CHECK_EQ(h.engine->roomState(), RoomState::Idle, "房间回 idle");
  CHECK_EQ(h.recorder->log.back(), std::string("callEnd:hangup/0"), "callEnd 照发");

  h.event(imrtc::frame::kCallEnded,
          Json::parse("{\"call_id\":\"call-9\",\"reason\":\"hangup\",\"duration_sec\":3,\"ended_by\":\"alice\"}"));
  CHECK_EQ(h.recorder->log.back(), std::string("callEnd:hangup/0"), "不抛第二次");
}

IMRTC_TEST(actionResultLeaveOnDisconnectEndsLocally, "调用结果 —— leaveRoom 等应答时断线：回 2003，roomLeft 照发") {
  Harness h;
  h.login();
  inMeeting(h);
  Captured captured;
  h.engine->leaveRoom(captured.sink());
  h.net.remoteClose(1006);
  CHECK_EQ(captured.last.code, std::int32_t{2003}, "2003");
  CHECK_EQ(h.engine->roomState(), RoomState::Idle, "房间回 idle");
  CHECK_TRUE(std::find(h.recorder->log.begin(), h.recorder->log.end(), "roomLeft:m-1") != h.recorder->log.end(),
             "roomLeft 照发");
}

IMRTC_TEST(actionResultLogin, "调用结果 —— login：首次握手成功回 session_id；握手被拒回那个码且不发 onError") {
  Harness ok;
  Captured okResult;
  ok.engine->login("tk-1", okResult.sink());
  ok.net.open();
  ok.reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData("s-7", false));
  CHECK_EQ(okResult.calls, 1, "恰好一次");
  CHECK_EQ(okResult.last.value, std::string("s-7"), "session_id");

  Harness denied;
  Captured deniedResult;
  denied.engine->login("tk-1", deniedResult.sink());
  denied.net.open();
  denied.reply(imrtc::frame::kError,
               Json::parse("{\"code\":1101,\"name\":\"token_invalid\",\"msg\":\"token invalid\","
                           "\"for_type\":\"sys.hello\",\"retryable\":false}"));
  CHECK_EQ(deniedResult.calls, 1, "恰好一次");
  CHECK_EQ(deniedResult.last.code, std::int32_t{1101}, "那个码");
  CHECK_EQ(errorCount(denied), 0, "不再多发 onError");

  Harness pending;
  Captured pendingResult;
  pending.engine->login("tk-1", pendingResult.sink());
  pending.engine->logout();
  CHECK_EQ(pendingResult.calls, 1, "还没握手就 logout 也要回");
  CHECK_EQ(pendingResult.last.code, std::int32_t{2005}, "2005");
}

IMRTC_TEST(actionResultDestroySettlesPending, "调用结果 —— 引擎析构时还没回来的结果一律回 2005（R5）") {
  Captured captured;
  {
    Harness h;
    h.login();
    h.engine->joinRoom("m-1", "tk", captured.sink());
    CHECK_EQ(captured.calls, 0, "还在等应答");
    h.engine.reset();
  }
  CHECK_EQ(captured.calls, 1, "析构时恰好回一次");
  CHECK_EQ(captured.last.code, std::int32_t{2005}, "2005");
}
