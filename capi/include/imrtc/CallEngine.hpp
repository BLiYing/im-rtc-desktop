#pragma once

/**
 * CallEngine.hpp —— 给 C++ 宿主（Qt / MFC）用着顺手的 header-only RAII 包装。
 *
 * # 它自己也走 C ABI
 *
 * 这一点是刻意的，也是本仓最容易被「优化」掉的一条：直接包 `imrtc::CallEngine`
 * 显然更省事，但那样 Qt Demo 就绕开了真正的交付边界——**Demo 跑通不再等于宿主接得通**，
 * ABI 上的问题全部要等集成方去撞。所以这个头只 include `imrtc_c.h`，
 * 一行 engine 内部的东西都不碰。
 *
 * # 用法
 *
 *   imrtc::capi::Engine engine("wss://…/v1/ws", "mac-8f3a");
 *   engine.setObserver(&myObserver);      // 只要求你的类有那几个方法
 *   engine.login(token);
 *   … 在你的定时器里 engine.tick();
 *
 * **回调在 Engine 的线程上抛出，切回 UI 线程是你的事**
 * （Qt 用 `QMetaObject::invokeMethod` 或 `Qt::QueuedConnection`）。
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imrtc/CallEngineTypes.hpp"
#include "imrtc/imrtc_c.h"

namespace imrtc {
namespace capi {

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

/** Engine 是句柄的 RAII 包装：构造即创建，析构即销毁（并等回调静默）。 */
class Engine {
public:
  Engine(const std::string& url, const std::string& deviceId,
         const std::string& sdk = std::string("desktop-cpp/") + imrtc_v1_version()) {
    imrtc_v1_options options{};
    options.struct_size = sizeof(options);
    options.url = url.c_str();
    options.device_id = deviceId.c_str();
    options.sdk = sdk.c_str();
    options.request_timeout_ms = 0;
    lastError_ = Error(imrtc_v1_engine_create(&options, &handle_));
  }

  ~Engine() { imrtc_v1_engine_destroy(handle_); }

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  /** valid 报告构造是否成功；失败原因见 `lastError()`。 */
  bool valid() const { return handle_ != nullptr; }
  Error lastError() const { return lastError_; }

  /**
   * setObserver 注册回调。**Engine 不持有 observer 的所有权**——
   * 它必须活得比 Engine 久，或者在销毁前调 `setObserver(nullptr)`。
   */
  Error setObserver(Observer* observer) {
    observer_ = observer;
    if (observer == nullptr) return call(imrtc_v1_engine_set_observer(handle_, nullptr));

    imrtc_v1_observer table{};
    table.struct_size = sizeof(table);
    table.user_data = this;
    table.on_connected = &Engine::cbConnected;
    table.on_disconnected = &Engine::cbDisconnected;  // 兜底：链到不认识 _ex 的旧引擎时用
    table.on_disconnected_ex = &Engine::cbDisconnectedEx;
    table.on_kicked_out = &Engine::cbKickedOut;
    table.on_error = &Engine::cbError;
    table.on_call_received = &Engine::cbCallReceived;
    table.on_call_begin = &Engine::cbCallBegin;
    table.on_call_end = &Engine::cbCallEnd;
    table.on_call_missed = &Engine::cbCallMissed;
    table.on_call_cancelled = &Engine::cbCallCancelled;
    table.on_call_rejected = &Engine::cbCallRejected;
    table.on_call_busy = &Engine::cbCallBusy;
    table.on_call_no_answer = &Engine::cbCallNoAnswer;
    table.on_handled_on_other_device = &Engine::cbHandledOnOtherDevice;
    table.on_user_enter = &Engine::cbUserEnter;
    table.on_user_leave = &Engine::cbUserLeave;
    table.on_user_accept = &Engine::cbUserAccept;
    table.on_user_reject = &Engine::cbUserReject;
    table.on_user_no_response = &Engine::cbUserNoResponse;
    table.on_user_audio_available = &Engine::cbAudioAvailable;
    table.on_user_video_available = &Engine::cbVideoAvailable;
    table.on_active_speakers = &Engine::cbActiveSpeakers;
    table.on_network_quality = &Engine::cbNetworkQuality;
    table.on_room_joined = &Engine::cbRoomJoined;
    table.on_room_left = &Engine::cbRoomLeft;
    table.on_room_closed = &Engine::cbRoomClosed;
    table.on_user_ringing = &Engine::cbUserRinging;
    return call(imrtc_v1_engine_set_observer(handle_, &table));
  }

  /*
    发起类方法的 `done`（2.0.0）：结果恰好回一次，排在这次调用引起的状态回调之后；**可以不传**，
    不传时失败退回 `Observer::onError`。返回值只表示「根本没受理」（空句柄 / 参数非法）——
    那种情况下 `done` 也会就地收到同一个码，不会悬着。规则见 imrtc_c.h 的 `imrtc_v1_result_cb`。
  */
  Error login(const std::string& token, std::function<void(Result<std::string>)> done = {}) {
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) {
      return imrtc_v1_login(handle_, token.c_str(), cb, ud);
    });
  }
  /**
   * fetchCallHistory 查自己的通话记录（`GET /v1/calls`），按发起时间倒序，游标翻页。
   * `limit` 夹在 1..200；`cursor` 首页传 0，下一页传上一页的 `nextCursor`，`hasNext()` 为假就是到底。
   * 必须已登录；只返回本人参与过的通话。`done` **必须传**（没有 onError 兜底）；没受理时就地收到同一个码。
   * 见 imrtc_c.h 的 `imrtc_v1_call_history_cb`。
   */
  Error fetchCallHistory(std::int32_t limit, std::int64_t cursor,
                         std::function<void(Result<CallHistoryPage>)> done) {
    if (!done) return call(IMRTC_V1_ERR_BAD_PARAMS);
    std::unique_ptr<PendingHistory> pending(new PendingHistory{std::move(done)});
    const std::int32_t code =
        imrtc_v1_fetch_call_history(handle_, limit, cursor, &Engine::cbHistory, pending.get());
    if (code == IMRTC_V1_OK) {
      static_cast<void>(pending.release());  // 所有权交给 C 那层，cbHistory 里删
    } else {
      Result<CallHistoryPage> result;
      result.code = code;
      result.name = imrtc_v1_error_name(code);
      pending->fn(result);
    }
    return call(code);
  }
  Error logout() { return call(imrtc_v1_logout(handle_)); }
  Error updateToken(const std::string& token) {
    return call(imrtc_v1_update_token(handle_, token.c_str()));
  }
  /** 回到前台（含睡眠唤醒）/ 系统网络变了：断线后不再按退避白等。见 imrtc_c.h。 */
  Error setAppForeground(bool foreground) {
    return call(imrtc_v1_set_app_foreground(handle_, foreground ? 1 : 0));
  }
  Error notifyNetworkChanged() { return call(imrtc_v1_notify_network_changed(handle_)); }

  Error call(const std::vector<std::string>& calleeIds, const std::string& mediaType,
             bool isGroup, std::function<void(Result<std::string>)> done = {}) {
    std::vector<const char*> ids = raw(calleeIds);
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) {
      return imrtc_v1_call(handle_, ids.data(), static_cast<std::uint32_t>(ids.size()),
                           mediaType.c_str(), isGroup ? 1 : 0, cb, ud);
    });
  }
  /**
   * callEx 带选项发起通话：群号 / user_data / 振铃超时（HOST_INTEGRATION_DESIGN §3.3）。
   * 返回值只报「这次调用本身合不合法」，`options` 里的业务校验错误从 `done` 回来（1004）。
   */
  Error callEx(const std::vector<std::string>& calleeIds, const std::string& mediaType,
              bool isGroup, const CallOptions& options,
              std::function<void(Result<std::string>)> done = {}) {
    std::vector<const char*> ids = raw(calleeIds);
    imrtc_v1_call_options table{};
    table.struct_size = sizeof(table);
    table.is_group = isGroup ? 1 : 0;
    table.chat_group_id = options.chatGroupId.c_str();
    table.user_data = options.userData.c_str();
    table.timeout_sec = options.timeoutSec;
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) {
      return imrtc_v1_call_ex(handle_, ids.data(), static_cast<std::uint32_t>(ids.size()),
                              mediaType.c_str(), &table, cb, ud);
    });
  }
  Error accept(std::function<void(Result<>)> done = {}) {
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) { return imrtc_v1_accept(handle_, cb, ud); });
  }
  /** 退出类（reject / cancel / hangup / leaveRoom）失败时本地照样收场，结果只供日志。 */
  Error reject(std::function<void(Result<>)> done = {}) {
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) { return imrtc_v1_reject(handle_, cb, ud); });
  }
  Error cancel(std::function<void(Result<>)> done = {}) {
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) { return imrtc_v1_cancel(handle_, cb, ud); });
  }
  Error hangup(std::function<void(Result<>)> done = {}) {
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) { return imrtc_v1_hangup(handle_, cb, ud); });
  }
  /** forceEnd 强制收场，不等服务端。见 `imrtc_v1_force_end`。 */
  Error forceEnd() { return call(imrtc_v1_force_end(handle_)); }
  Error inviteMore(const std::vector<std::string>& calleeIds, std::function<void(Result<>)> done = {}) {
    std::vector<const char*> ids = raw(calleeIds);
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) {
      return imrtc_v1_invite_more(handle_, ids.data(), static_cast<std::uint32_t>(ids.size()), cb, ud);
    });
  }
  Error joinCall(const std::string& callId, std::function<void(Result<>)> done = {}) {
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) {
      return imrtc_v1_join_call(handle_, callId.c_str(), cb, ud);
    });
  }
  Error joinRoom(const std::string& roomId, const std::string& roomToken,
                 std::function<void(Result<>)> done = {}) {
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) {
      return imrtc_v1_join_room(handle_, roomId.c_str(), roomToken.c_str(), cb, ud);
    });
  }
  Error leaveRoom(std::function<void(Result<>)> done = {}) {
    return submit(std::move(done), [&](imrtc_v1_result_cb cb, void* ud) { return imrtc_v1_leave_room(handle_, cb, ud); });
  }
  /**
   * setRemoteLayer 报某人画面的层上界（`"none"|"l"|"m"|"h"`，协议 §3.5）。
   * 九宫格报 `"l"`、放大那一格报 `"h"`。轨道还没发布时这次会被丢掉且返回 0——
   * 要在 `onUserVideoAvailable` 里再报一次。详见 imrtc_c.h。
   */
  Error setRemoteLayer(const std::string& uid, const std::string& layer) {
    return call(imrtc_v1_set_remote_layer(handle_, uid.c_str(), layer.c_str()));
  }

  /** 开麦克风。**不是 unpublish**，轨道与协商都保留。 */
  Error openMicrophone() { return call(imrtc_v1_open_microphone(handle_)); }
  /** 关麦克风。 */
  Error closeMicrophone() { return call(imrtc_v1_close_microphone(handle_)); }
  Error openCamera() { return call(imrtc_v1_open_camera(handle_)); }
  Error closeCamera() { return call(imrtc_v1_close_camera(handle_)); }
  /** attachView：Windows 传 `HWND`、macOS 传 `NSView*`；传 nullptr 卸载。 */
  Error attachView(const std::string& uid, void* nativeHandle) {
    return call(imrtc_v1_attach_view(handle_, uid.c_str(), nativeHandle));
  }

  /** 本端摄像头预览。见 imrtc_c.h 里为什么它不是 attachView(自己的 uid, …)。 */
  Error attachLocalView(void* nativeHandle) {
    return call(imrtc_v1_attach_local_view(handle_, nativeHandle));
  }

  /** tick 推进时间。在你的定时器里按 ~200ms~1s 调。 */
  Error tick() { return call(imrtc_v1_engine_tick(handle_)); }

  imrtc_v1_call_state callState() {
    std::int32_t state = IMRTC_V1_CALL_IDLE;
    call(imrtc_v1_get_call_state(handle_, &state));
    return static_cast<imrtc_v1_call_state>(state);
  }
  imrtc_v1_room_state roomState() {
    std::int32_t state = IMRTC_V1_ROOM_IDLE;
    call(imrtc_v1_get_room_state(handle_, &state));
    return static_cast<imrtc_v1_room_state>(state);
  }

private:
  Error call(std::int32_t code) {
    lastError_ = Error(code);
    return lastError_;
  }

  /** PendingResult 是交给 C 那层的 user_data：结果回来时自己删掉自己（恰好一次）。 */
  struct PendingResult { std::function<void(std::int32_t code, const char* name, const char* value)> fn; };

  static void cbResult(void* u, std::int32_t code, const char* name, const char* value) {
    std::unique_ptr<PendingResult> pending(static_cast<PendingResult*>(u));
    if (pending && pending->fn) pending->fn(code, name, value);
  }

  /** PendingHistory 同 PendingResult：结果回来时自己删掉自己（恰好一次）。 */
  struct PendingHistory { std::function<void(Result<CallHistoryPage>)> fn; };

  static void cbHistory(void* u, std::int32_t code, const char* name,
                        const imrtc_v1_call_record* records, std::uint32_t count,
                        std::int64_t nextCursor) {
    std::unique_ptr<PendingHistory> pending(static_cast<PendingHistory*>(u));
    if (!pending || !pending->fn) return;
    Result<CallHistoryPage> result;
    result.code = code;
    result.name = text(name);
    result.value.nextCursor = nextCursor;
    for (std::uint32_t i = 0; records != nullptr && i < count; ++i) {
      const imrtc_v1_call_record& in = records[i];
      CallHistoryRecord out;
      out.callId = text(in.call_id);
      out.roomId = text(in.room_id);
      out.caller = text(in.caller);
      out.mediaType = text(in.media_type);
      out.isGroup = in.is_group != 0;
      out.reason = text(in.reason);
      out.endedBy = text(in.ended_by);
      out.durationSec = in.duration_sec;
      out.startedAtMs = in.started_at_ms;
      out.connectedAtMs = in.connected_at_ms;
      out.endedAtMs = in.ended_at_ms;
      out.userData = text(in.user_data);
      out.chatGroupId = text(in.chat_group_id);
      for (std::uint32_t m = 0; in.members != nullptr && m < in.member_count; ++m) {
        out.members.push_back(CallHistoryMember{text(in.members[m].uid), text(in.members[m].state)});
      }
      result.value.records.push_back(std::move(out));
    }
    pending->fn(result);
  }

  static void fillValue(Result<>&, const char*) {}
  static void fillValue(Result<std::string>& result, const char* value) { result.value = text(value); }

  /**
   * submit 把 `done` 接到 C 结果回调上。`done` 为空时传 NULL（失败退回 onError）；
   * 没受理（返回非 0）时 C 那层不会回调，这里就地把同一个码交给 `done`，免得它悬着。
   */
  template <typename T, typename Invoke>
  Error submit(std::function<void(Result<T>)> done, Invoke&& invoke) {
    if (!done) return call(invoke(nullptr, nullptr));
    std::unique_ptr<PendingResult> pending(new PendingResult{
        [done](std::int32_t code, const char* name, const char* value) {
          Result<T> result;
          result.code = code;
          result.name = text(name);
          fillValue(result, value);
          done(result);
        }});
    const std::int32_t code = invoke(&Engine::cbResult, pending.get());
    if (code == IMRTC_V1_OK) {
      static_cast<void>(pending.release());  // 所有权交给 C 那层，cbResult 里删
    } else {
      pending->fn(code, imrtc_v1_error_name(code), "");
    }
    return call(code);
  }

  static std::vector<const char*> raw(const std::vector<std::string>& values) {
    std::vector<const char*> out;
    out.reserve(values.size());
    for (const std::string& value : values) out.push_back(value.c_str());
    return out;
  }

  /** self 把 user_data 还原回 Engine；observer 为空时返回 nullptr。 */
  static Observer* self(void* userData) {
    Engine* engine = static_cast<Engine*>(userData);
    return engine == nullptr ? nullptr : engine->observer_;
  }

  static std::string text(const char* value) {
    return value == nullptr ? std::string() : std::string(value);
  }

  static void cbConnected(void* u, const char* sessionId, imrtc_v1_bool resumed) {
    if (Observer* o = self(u)) o->onConnected(text(sessionId), resumed != 0);
  }
  static void cbDisconnected(void* u) {
    // 只在链到旧引擎（没有 on_disconnected_ex）时才会被调到，拿不到关闭码与
    // willReconnect，如实报成 (0, false)。
    if (Observer* o = self(u)) o->onDisconnected(0, false);
  }
  static void cbDisconnectedEx(void* u, std::int32_t code, imrtc_v1_bool willReconnect) {
    if (Observer* o = self(u)) o->onDisconnected(code, willReconnect != 0);
  }
  static void cbKickedOut(void* u, imrtc_v1_kicked_reason reason) {
    if (Observer* o = self(u)) o->onKickedOut(reason);
  }
  static void cbError(void* u, std::int32_t code, const char* name, const char* forType) {
    if (Observer* o = self(u)) o->onError(code, text(name), text(forType));
  }
  static void cbCallReceived(void* u, const imrtc_v1_call_invite* invite) {
    Observer* o = self(u);
    if (o == nullptr || invite == nullptr) return;
    std::vector<std::string> ids;
    for (std::uint32_t i = 0; i < invite->callee_count; ++i) ids.push_back(text(invite->callee_ids[i]));
    // 2026-09-20 追加的字段：旧引擎给的结构体不含它们，先看 struct_size 够不够再读。
    std::vector<std::string> joined;
    if (invite->struct_size >= offsetof(imrtc_v1_call_invite, joined_count) + sizeof(invite->joined_count)) {
      for (std::uint32_t i = 0; i < invite->joined_count; ++i) joined.push_back(text(invite->joined_ids[i]));
    }
    o->onCallReceived(text(invite->call_id), text(invite->caller), ids, text(invite->media_type),
                      invite->is_group != 0, text(invite->chat_group_id), text(invite->user_data),
                      text(invite->inviter), joined);
  }
  static void cbCallBegin(void* u, const imrtc_v1_call_begin* begin) {
    Observer* o = self(u);
    if (o == nullptr || begin == nullptr) return;
    o->onCallBegin(text(begin->call_id), text(begin->room_id), text(begin->role),
                  text(begin->caller), text(begin->chat_group_id), text(begin->user_data));
  }
  static void cbCallEnd(void* u, const imrtc_v1_call_end* end) {
    Observer* o = self(u);
    if (o == nullptr || end == nullptr) return;
    o->onCallEnd(text(end->call_id), text(end->reason), end->duration_sec, text(end->ended_by),
                end->reason_code);
  }
  static void cbCallMissed(void* u, const imrtc_v1_call_missed* missed) {
    Observer* o = self(u);
    if (o == nullptr || missed == nullptr) return;
    o->onCallMissed(text(missed->call_id), text(missed->caller), text(missed->reason));
  }
  static void cbCallCancelled(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onCallCancelled(text(uid));
  }
  static void cbCallRejected(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onCallRejected(text(uid));
  }
  static void cbCallBusy(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onCallBusy(text(uid));
  }
  static void cbCallNoAnswer(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onCallNoAnswer(text(uid));
  }
  static void cbHandledOnOtherDevice(void* u, const char* callId, const char* action) {
    if (Observer* o = self(u)) o->onHandledOnOtherDevice(text(callId), text(action));
  }
  static void cbUserEnter(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onUserEnter(text(uid));
  }
  static void cbUserLeave(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onUserLeave(text(uid));
  }
  static void cbUserRinging(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onUserRinging(text(uid));
  }
  static void cbUserAccept(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onUserAccept(text(uid));
  }
  static void cbUserReject(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onUserReject(text(uid));
  }
  static void cbUserNoResponse(void* u, const char* uid) {
    if (Observer* o = self(u)) o->onUserNoResponse(text(uid));
  }
  static void cbAudioAvailable(void* u, const char* uid, imrtc_v1_bool available) {
    if (Observer* o = self(u)) o->onUserAudioAvailable(text(uid), available != 0);
  }
  static void cbVideoAvailable(void* u, const char* uid, imrtc_v1_bool available) {
    if (Observer* o = self(u)) o->onUserVideoAvailable(text(uid), available != 0);
  }
  static void cbActiveSpeakers(void* u, const imrtc_v1_speaker* items, std::uint32_t count) {
    Observer* o = self(u);
    if (o == nullptr) return;
    std::vector<Speaker> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      out.push_back(Speaker{text(items[i].uid), text(items[i].participant_id), items[i].volume});
    }
    o->onActiveSpeakers(out);
  }
  static void cbNetworkQuality(void* u, const imrtc_v1_quality* items, std::uint32_t count) {
    Observer* o = self(u);
    if (o == nullptr) return;
    std::vector<Quality> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      out.push_back(Quality{text(items[i].uid), text(items[i].participant_id), items[i].level});
    }
    o->onNetworkQuality(out);
  }
  static void cbRoomJoined(void* u, const char* roomId) {
    if (Observer* o = self(u)) o->onRoomJoined(text(roomId));
  }
  static void cbRoomLeft(void* u, const char* roomId) {
    if (Observer* o = self(u)) o->onRoomLeft(text(roomId));
  }
  static void cbRoomClosed(void* u, const char* roomId, const char* reason) {
    if (Observer* o = self(u)) o->onRoomClosed(text(roomId), text(reason));
  }

  imrtc_v1_engine* handle_ = nullptr;
  Observer* observer_ = nullptr;
  Error lastError_;
};

/**
 * log 把一条日志打进**引擎那条流**（CONVENTIONS §8：demo 与宿主共用同一个入口）。
 *
 * 分两套的话「界面调了没有」与「引擎发了没有」永远对不上时间——
 * 而那正是排查「按了没反应」时唯一要问的问题。
 */
inline void log(imrtc_v1_log_level level, const std::string& message,
                const std::vector<std::pair<std::string, std::string>>& fields = {}) {
  std::vector<imrtc_v1_log_field> items;
  items.reserve(fields.size());
  for (const auto& field : fields) {
    imrtc_v1_log_field item{};
    item.struct_size = static_cast<std::uint32_t>(sizeof(imrtc_v1_log_field));
    item.key = field.first.c_str();
    item.value = field.second.c_str();
    items.push_back(item);
  }
  imrtc_v1_log(level, message.c_str(), items.empty() ? nullptr : items.data(),
               static_cast<std::uint32_t>(items.size()));
}

/** LogSink 是 C++ 侧的 sink 形状。装法见 setLogSink。 */
using LogSink = std::function<void(imrtc_v1_log_level level, const std::string& message,
                                   const std::vector<std::pair<std::string, std::string>>& fields)>;

/**
 * setLogSink 装一个 C++ sink。**进程级**，传空则卸掉。
 *
 * sink 存在一个函数内静态里：C ABI 那层收的是裸函数指针 + `void*`，
 * 而 `std::function` 过不去。宿主只会装一个，所以不需要更复杂的东西。
 */
inline void setLogSink(LogSink sink) {
  static LogSink installed;
  installed = std::move(sink);
  if (!installed) {
    imrtc_v1_set_log_sink(nullptr, nullptr);
    return;
  }
  imrtc_v1_set_log_sink(
      [](void* user_data, imrtc_v1_log_level level, const char* message,
         const imrtc_v1_log_field* fields, std::uint32_t count) {
        auto* target = static_cast<LogSink*>(user_data);
        if (target == nullptr || !*target) return;
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
          out.emplace_back(fields[i].key == nullptr ? "" : fields[i].key,
                           fields[i].value == nullptr ? "" : fields[i].value);
        }
        (*target)(level, message == nullptr ? "" : message, out);
      },
      &installed);
}

}  // namespace capi
}  // namespace imrtc
