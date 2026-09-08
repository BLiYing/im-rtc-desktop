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

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "imrtc/imrtc_c.h"

namespace imrtc {
namespace capi {

/** Error 是 C 错误码的一层薄封装，`ok()` 为真表示成功。 */
class Error {
public:
  Error() = default;
  explicit Error(std::int32_t code) : code_(code) {}

  bool ok() const { return code_ == IMRTC_V1_OK; }
  std::int32_t code() const { return code_; }
  /** name 返回机读名。指向库内的静态字符串，**不要释放**。 */
  const char* name() const { return imrtc_v1_error_name(code_); }

private:
  std::int32_t code_ = IMRTC_V1_OK;
};

/** 一个正在说话的人，对应 `imrtc_v1_speaker`。volume 0~100。 */
struct Speaker {
  std::string uid;
  std::string participantId;
  std::int64_t volume = 0;
};

/** 一个人的网络质量，对应 `imrtc_v1_quality`。level 0~6（0 = unknown）。 */
struct Quality {
  std::string uid;
  std::string participantId;
  std::int64_t level = 0;
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
  virtual void onDisconnected() {}
  /** 被踢 / 鉴权用尽 / 握手被拒。`reason` 决定宿主该做什么，见 imrtc_v1_kicked_reason。 */
  virtual void onKickedOut(imrtc_v1_kicked_reason reason) { (void)reason; }
  virtual void onError(std::int32_t code, const std::string& name, const std::string& forType) {
    (void)code; (void)name; (void)forType;
  }
  virtual void onCallReceived(const std::string& callId, const std::string& caller,
                              const std::vector<std::string>& calleeIds,
                              const std::string& mediaType, bool isGroup) {
    (void)callId; (void)caller; (void)calleeIds; (void)mediaType; (void)isGroup;
  }
  virtual void onCallBegin(const std::string& callId, const std::string& roomId,
                           const std::string& role) {
    (void)callId; (void)roomId; (void)role;
  }
  virtual void onCallEnd(const std::string& callId, const std::string& reason,
                         std::int64_t durationSec, const std::string& endedBy) {
    (void)callId; (void)reason; (void)durationSec; (void)endedBy;
  }
  /** 通话中被第三个人呼叫、服务端已替你回了忙线。**不是**一次需要你处理的来电。 */
  virtual void onCallMissed(const std::string& callId, const std::string& caller,
                            const std::string& reason) {
    (void)callId; (void)caller; (void)reason;
  }
  virtual void onCallCancelled(const std::string& by) { (void)by; }
  virtual void onCallRejected(const std::string& uid) { (void)uid; }
  virtual void onCallBusy(const std::string& uid) { (void)uid; }
  virtual void onCallNoAnswer(const std::string& uid) { (void)uid; }
  /** 同一账号的另一台设备接了或拒了。action 是 "accepted" / "rejected"。 */
  virtual void onHandledOnOtherDevice(const std::string& callId, const std::string& action) {
    (void)callId; (void)action;
  }

  virtual void onUserEnter(const std::string& uid) { (void)uid; }
  virtual void onUserLeave(const std::string& uid) { (void)uid; }
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
         const std::string& sdk = "desktop-cpp/0.1.0") {
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
    table.on_disconnected = &Engine::cbDisconnected;
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
    return call(imrtc_v1_engine_set_observer(handle_, &table));
  }

  Error login(const std::string& token) { return call(imrtc_v1_login(handle_, token.c_str())); }
  Error logout() { return call(imrtc_v1_logout(handle_)); }
  Error updateToken(const std::string& token) {
    return call(imrtc_v1_update_token(handle_, token.c_str()));
  }

  Error call(const std::vector<std::string>& calleeIds, const std::string& mediaType,
             bool isGroup) {
    std::vector<const char*> ids = raw(calleeIds);
    return call(imrtc_v1_call(handle_, ids.data(), static_cast<std::uint32_t>(ids.size()),
                              mediaType.c_str(), isGroup ? 1 : 0));
  }
  Error accept() { return call(imrtc_v1_accept(handle_)); }
  Error reject() { return call(imrtc_v1_reject(handle_)); }
  Error cancel() { return call(imrtc_v1_cancel(handle_)); }
  Error hangup() { return call(imrtc_v1_hangup(handle_)); }
  Error inviteMore(const std::vector<std::string>& calleeIds) {
    std::vector<const char*> ids = raw(calleeIds);
    return call(imrtc_v1_invite_more(handle_, ids.data(), static_cast<std::uint32_t>(ids.size())));
  }
  Error joinCall(const std::string& callId) {
    return call(imrtc_v1_join_call(handle_, callId.c_str()));
  }
  Error joinRoom(const std::string& roomId, const std::string& roomToken) {
    return call(imrtc_v1_join_room(handle_, roomId.c_str(), roomToken.c_str()));
  }
  Error leaveRoom() { return call(imrtc_v1_leave_room(handle_)); }
  /**
   * setRemoteLayer 报某人画面的层上界（`"none"|"l"|"m"|"h"`，协议 §3.5）。
   * 九宫格报 `"l"`、放大那一格报 `"h"`。轨道还没发布时这次会被丢掉且返回 0——
   * 要在 `onUserVideoAvailable` 里再报一次。详见 imrtc_c.h。
   */
  Error setRemoteLayer(const std::string& uid, const std::string& layer) {
    return call(imrtc_v1_set_remote_layer(handle_, uid.c_str(), layer.c_str()));
  }

  Error openMic() { return call(imrtc_v1_open_mic(handle_)); }
  Error closeMic() { return call(imrtc_v1_close_mic(handle_)); }
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
    if (Observer* o = self(u)) o->onDisconnected();
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
    o->onCallReceived(text(invite->call_id), text(invite->caller), ids, text(invite->media_type),
                      invite->is_group != 0);
  }
  static void cbCallBegin(void* u, const imrtc_v1_call_begin* begin) {
    Observer* o = self(u);
    if (o == nullptr || begin == nullptr) return;
    o->onCallBegin(text(begin->call_id), text(begin->room_id), text(begin->role));
  }
  static void cbCallEnd(void* u, const imrtc_v1_call_end* end) {
    Observer* o = self(u);
    if (o == nullptr || end == nullptr) return;
    o->onCallEnd(text(end->call_id), text(end->reason), end->duration_sec, text(end->ended_by));
  }
  static void cbCallMissed(void* u, const imrtc_v1_call_missed* missed) {
    Observer* o = self(u);
    if (o == nullptr || missed == nullptr) return;
    o->onCallMissed(text(missed->call_id), text(missed->caller), text(missed->reason));
  }
  static void cbCallCancelled(void* u, const char* by) {
    if (Observer* o = self(u)) o->onCallCancelled(text(by));
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
