#pragma once

/**
 * CallEngineObserver.hpp —— `CallEngine.hpp` 的回调基类 `Observer` 与 `CallSummaryInfo`。
 *
 * 从 `CallEngine.hpp` 拆出来是体量红线（CONVENTIONS §3，600 行）：回调基类是独立的一块。
 * **宿主照旧只 include `CallEngine.hpp`**，它会带上这个头。只依赖 `imrtc_c.h` 与值类型头。
 */

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/CallEngineTypes.hpp"
#include "imrtc/imrtc_c.h"

namespace imrtc {
namespace capi {

/** `Observer::onCallSummary` 的参数（字段含义见 imrtc_c.h 的 imrtc_v1_call_summary）。 */
struct CallSummaryInfo {
  std::string callId;
  std::string reason;
  imrtc_v1_end_reason reasonCode = IMRTC_V1_END_ERROR;
  std::int64_t durationSec = 0;
  std::string endedBy;
  std::string mediaType;
  bool isGroup = false;
  std::string chatGroupId;
  std::string caller;
  std::string role;
  std::string peer;
  std::string userData;
};

/**
 * Observer 是回调基类。方法都有空实现，只覆盖关心的那几个。
 *
 * 与 C 那张函数指针表一一对应；参数用 `std::string` 是**拷贝**——
 * C 那边承诺指针只在回调期间有效，拷一份才敢往后传。
 */
class Observer {
public:
  virtual ~Observer() = default;

  virtual void onConnected(const std::string& sessionId, bool resumed) { (void)sessionId; (void)resumed; }
  /**
   * 断线。`code` 是 WebSocket 关闭码，`willReconnect` 是引擎判断的这次断开
   * 会不会自动重连（2026-09-15 追加）。链到旧引擎（不认识 `on_disconnected_ex`）
   * 时两者拿不到，收到的是 `(0, false)`。
   */
  virtual void onDisconnected(std::int32_t code, bool willReconnect) {
    (void)code;
    (void)willReconnect;
  }
  /** 被踢 / 鉴权用尽 / 握手被拒。`reason` 决定宿主该做什么，见 imrtc_v1_kicked_reason。 */
  virtual void onKickedOut(imrtc_v1_kicked_reason reason) { (void)reason; }
  virtual void onError(std::int32_t code, const std::string& name, const std::string& forType) {
    (void)code; (void)name; (void)forType;
  }
  /**
   * `inviter` 是这次邀请**是谁发的**（2026-09-16 追加）：`caller` 恒为发起人，而群通话里
   * 被别人 `invite_more` 拉进来时，`inviter` 才是按下「添加成员」的那个人。界面上
   * 「谁邀请你」显示 `inviter`。服务端没带时引擎已回落成 `caller`，不会是空串。
   */
  virtual void onCallReceived(const std::string& callId, const std::string& caller,
                              const std::vector<std::string>& calleeIds,
                              const std::string& mediaType, bool isGroup,
                              const std::string& chatGroupId, const std::string& userData,
                              const std::string& inviter,
                              const std::vector<std::string>& joinedIds) {
    (void)callId; (void)caller; (void)calleeIds; (void)mediaType; (void)isGroup;
    (void)chatGroupId; (void)userData; (void)inviter; (void)joinedIds;
  }
  /**
   * `caller` / `chatGroupId` / `userData` 取 `call.connected` 里的值，为空时回落到
   * 本通 `call.incoming` / `callEx` 选项记下的值（HOST_INTEGRATION_DESIGN §3.3）。
   */
  virtual void onCallBegin(const std::string& callId, const std::string& roomId,
                           const std::string& role, const std::string& caller,
                           const std::string& chatGroupId, const std::string& userData) {
    (void)callId; (void)roomId; (void)role; (void)caller; (void)chatGroupId; (void)userData;
  }
  /** `reasonCode` 是 `reason` 的类型化版本，同一份数据，二选一即可（2026-09-15 追加）。 */
  virtual void onCallEnd(const std::string& callId, const std::string& reason,
                         std::int64_t durationSec, const std::string& endedBy,
                         imrtc_v1_end_reason reasonCode) {
    (void)callId; (void)reason; (void)durationSec; (void)endedBy; (void)reasonCode;
  }
  /**
   * 通话事实汇总（通话记录设计 §4），紧跟 onCallEnd、每通拿到 call_id 的电话恰好一次。
   * 宿主要发通话记录消息的话，只在 `role == "caller"` 时发。
   */
  virtual void onCallSummary(const CallSummaryInfo& summary) { (void)summary; }
  /** 通话中被第三个人呼叫、服务端已替你回了忙线。**不是**一次需要你处理的来电。 */
  virtual void onCallMissed(const std::string& callId, const std::string& caller,
                            const std::string& reason) {
    (void)callId; (void)caller; (void)reason;
  }
  virtual void onCallCancelled(const std::string& uid) { (void)uid; }
  virtual void onCallRejected(const std::string& uid) { (void)uid; }
  virtual void onCallBusy(const std::string& uid) { (void)uid; }
  virtual void onCallNoAnswer(const std::string& uid) { (void)uid; }
  /** 同一账号的另一台设备接了或拒了。action 是 "accept" / "reject"。 */
  virtual void onHandledOnOtherDevice(const std::string& callId, const std::string& action) {
    (void)callId; (void)action;
  }

  virtual void onUserEnter(const std::string& uid) { (void)uid; }
  virtual void onUserLeave(const std::string& uid) { (void)uid; }
  /** 某人的设备开始响铃，通话里的人都收到（2026-09-17 增）。见 imrtc_c.h 的 on_user_ringing。 */
  virtual void onUserRinging(const std::string& uid) { (void)uid; }
  virtual void onUserAccept(const std::string& uid) { (void)uid; }
  virtual void onUserReject(const std::string& uid) { (void)uid; }
  virtual void onUserNoResponse(const std::string& uid) { (void)uid; }
  virtual void onUserAudioAvailable(const std::string& uid, bool available) { (void)uid; (void)available; }
  virtual void onUserVideoAvailable(const std::string& uid, bool available) { (void)uid; (void)available; }

  /** 数组已经拷成 vector，随便往后传。 */
  virtual void onActiveSpeakers(const std::vector<Speaker>& speakers) { (void)speakers; }
  virtual void onNetworkQuality(const std::vector<Quality>& entries) { (void)entries; }

  virtual void onRoomJoined(const std::string& roomId) { (void)roomId; }
  virtual void onRoomLeft(const std::string& roomId) { (void)roomId; }
  virtual void onRoomClosed(const std::string& roomId, const std::string& reason) {
    (void)roomId; (void)reason;
  }
};

}  // namespace capi
}  // namespace imrtc
