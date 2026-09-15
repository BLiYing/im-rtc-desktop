#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/CallEngine.h"
#include "imrtc/imrtc_c.h"

/**
 * CObserver 把 §7.5 的回调表转发到 C 的函数指针。
 *
 * 从 imrtc_c.cpp 拆出来（CONVENTIONS §3 体量红线）——它是那个文件里最大的一块，
 * 而且是自成一体的翻译表，不掺任何 `imrtc_v1_engine_*` 入口点的控制流。
 *
 * **每个回调都先判空**：宿主只关心几个事件是常态，留 NULL 不该崩。
 * 字符串直接用 `std::string::c_str()`——它们活到回调返回为止，正是头文件承诺的
 * 「指针只在该次回调期间有效」。
 */
namespace imrtc {
namespace capi_detail {

class CObserver : public CallEngineObserver {
public:
  explicit CObserver(const imrtc_v1_observer& table) : table_(table) {}

  void onConnected(const std::string& sessionId, bool resumed) override;
  void onDisconnected(std::int32_t code, bool willReconnect) override;
  void onKickedOut(KickedReason reason) override;
  void onError(std::int32_t code, const std::string& name, const std::string& forType) override;

  void onCallReceived(const CallInvite& invite) override;
  void onCallBegin(const CallBegin& begin) override;
  void onCallEnd(const CallEnd& end) override;
  void onCallMissed(const CallMissed& missed) override;

  void onCallCancelled(const std::string& uid) override;
  void onCallRejected(const std::string& uid) override;
  void onCallBusy(const std::string& uid) override;
  void onCallNoAnswer(const std::string& uid) override;
  void onUserEnter(const std::string& uid) override;
  void onUserLeave(const std::string& uid) override;
  void onUserAccept(const std::string& uid) override;
  void onUserReject(const std::string& uid) override;
  void onUserNoResponse(const std::string& uid) override;
  void onRoomJoined(const std::string& roomId) override;
  void onRoomLeft(const std::string& roomId) override;

  void onHandledOnOtherDevice(const std::string& callId, const std::string& action) override;
  void onRoomClosed(const std::string& roomId, const std::string& reason) override;
  void onUserAudioAvailable(const std::string& uid, bool available) override;
  void onUserVideoAvailable(const std::string& uid, bool available) override;

  void onActiveSpeakers(const std::vector<Speaker>& speakers) override;
  void onNetworkQuality(const std::vector<QualityEntry>& entries) override;

private:
  using OneArg = void (*)(void*, const char*);
  void one(OneArg fn, const std::string& value) const;

  imrtc_v1_observer table_;
};

}  // namespace capi_detail
}  // namespace imrtc
