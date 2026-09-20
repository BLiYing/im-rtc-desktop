#pragma once

#include <memory>
#include <string>
#include <vector>

#include "FakeTransport.h"
#include "TestHarness.h"
#include "imrtc/CallEngine.h"
#include "imrtc/Registry.h"

/**
 * 门面端到端测试共用的假宿主：Recorder 把回调摊成字符串，Harness 装好假 Transport + 假时钟。
 * 从 CallEngineTest.cpp 挪出来给 ForceEndTest.cpp 复用（体量红线，CONVENTIONS §3）。
 */
namespace enginetest {

using imrtc::CallEngine;
using imrtc::CallEngineObserver;
using imrtc::CallEngineOptions;
using imrtc::Json;

inline constexpr std::int64_t kT0 = 1756876800000;

/** Recorder 把收到的回调摊成一串可比对的字符串。 */
class Recorder : public CallEngineObserver {
public:
  void onConnected(const std::string& sessionId, bool resumed) override {
    log.push_back("connected:" + sessionId + (resumed ? "/resumed" : "/fresh"));
  }
  void onDisconnected(std::int32_t code, bool willReconnect) override {
    log.push_back("disconnected:" + std::to_string(code) + "/" +
                  (willReconnect ? "retry" : "stop"));
  }
  void onKickedOut(imrtc::KickedReason reason) override {
    // 原因一起记：合并成一句「被踢」正是这一条要防的事。
    log.push_back(std::string("kickedOut:") + imrtc::kickedReasonName(reason));
  }
  void onError(std::int32_t code, const std::string& name, const std::string& forType) override {
    log.push_back("error:" + std::to_string(code) + "/" + name);
    errorForTypes.push_back(forType);
  }
  /** 每条 onError 的 for_type，与 log 里的 error 条目同序。 */
  std::vector<std::string> errorForTypes;
  void onCallBegin(const imrtc::CallBegin& begin) override {
    log.push_back("callBegin:" + begin.callId + "/" + begin.role);
  }
  void onCallEnd(const imrtc::CallEnd& end) override {
    log.push_back("callEnd:" + end.reason + "/" + std::to_string(end.durationSec));
    lastCallEndReasonCode = end.reasonCode;
  }
  imrtc::EndReason lastCallEndReasonCode = imrtc::EndReason::Error;
  void onCallMissed(const imrtc::CallMissed& missed) override {
    log.push_back("callMissed:" + missed.caller);
  }
  void onUserAccept(const std::string& uid) override { log.push_back("userAccept:" + uid); }
  void onUserEnter(const std::string& uid) override { log.push_back("userEnter:" + uid); }
  void onUserLeave(const std::string& uid) override { log.push_back("userLeave:" + uid); }
  void onCallReceived(const imrtc::CallInvite& invite) override {
    log.push_back("callReceived:" + invite.callId + "/" + invite.caller + "/" + invite.mediaType);
    lastJoinedIds = invite.joinedIds;
  }
  std::vector<std::string> lastJoinedIds;
  void onRoomJoined(const std::string& roomId) override { log.push_back("roomJoined:" + roomId); }
  void onRoomLeft(const std::string& roomId) override { log.push_back("roomLeft:" + roomId); }

  std::vector<std::string> log;
};

struct Harness {
  imtest::FakeNet net;
  std::shared_ptr<Recorder> recorder = std::make_shared<Recorder>();
  std::int64_t now = kT0;
  std::unique_ptr<CallEngine> engine;

  explicit Harness(const std::string& deviceId = "mac-8f3a", imrtc::HttpClientFactory http = {}) {
    CallEngineOptions options;
    options.url = "wss://rtc.example.com/v1/ws";
    options.deviceId = deviceId;
    options.transportFactory = net.factory();
    options.httpClientFactory = std::move(http);
    options.random = []() { return 0.5; };
    options.clock = [this]() { return now; };
    engine.reset(new CallEngine(options));
    engine->setObserver(recorder);
  }

  /** login 走完握手，停在 connected。 */
  void login(bool resumed = false, const std::string& sessionId = "s-1") {
    engine->login("tk-1");
    net.open();
    reply(imrtc::okType(imrtc::frame::kHello), imtest::helloOkData(sessionId, resumed));
  }

  /** lastType 是最后一帧的 type。 */
  std::string lastType() { return imtest::field(imtest::lastSent(net.current()), "type"); }
  /** lastReqId 是最后一帧的 req_id。 */
  std::string lastReqId() { return imtest::field(imtest::lastSent(net.current()), "req_id"); }

  /** reply 用最后一帧的 req_id 回一条应答。 */
  void reply(const std::string& type, Json data) {
    net.deliver(imtest::replyFrame(type, lastReqId(), std::move(data)));
  }
  /** event 投一条服务端主动事件（req_id 为空）。 */
  void event(const std::string& type, Json data) {
    net.deliver(imtest::replyFrame(type, "", std::move(data)));
  }
};


}  // namespace enginetest
