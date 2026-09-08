#include "imrtc/imrtc_c.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "imrtc/CallEngine.h"
#include "imrtc/Enums.h"
#include "imrtc/Errors.h"
#include "imrtc/IxTransport.h"

/**
 * C++ → C 的转换层。**异常在这里被吃掉转成错误码**（CONVENTIONS §2 红线 5）。
 *
 * 这一层刻意很薄：它不做任何决策，只做三件事——查参数、转类型、接异常。
 * 一旦这里出现「如果…就…」的业务判断，那条判断就只对 C 宿主生效，
 * 而 Qt Demo 也走这条路，两边行为就会漂。
 */
namespace {

using imrtc::CallEngine;
using imrtc::CallEngineObserver;
using imrtc::CallEngineOptions;

/** cstr 把可能为空的 C 字符串收成 std::string。**NULL 当空串**，不是错误。 */
std::string cstr(const char* text) { return text == nullptr ? std::string() : std::string(text); }

/** toBool / fromBool 在 C 的 int32 布尔与 C++ bool 之间转（见头文件里为什么不用 _Bool）。 */
bool toBool(imrtc_v1_bool value) { return value != 0; }
imrtc_v1_bool fromBool(bool value) { return value ? 1 : 0; }

/**
 * isValidLayer 认协议 §3.5 的那四个值。
 *
 * **名单从 `imrtc::layers()` 读，不在这里再抄一份**——协议加一个层的时候，
 * 抄本会静默地把新值挡在门外，而症状是「宿主报了 h，画面还是 m」这种没人查得动的事。
 */
bool isValidLayer(const std::string& layer) {
  const imrtc::EnumValues& allowed = imrtc::layers();
  return std::find(allowed.begin(), allowed.end(), layer) != allowed.end();
}

std::vector<std::string> toStrings(const char* const* items, std::uint32_t count) {
  std::vector<std::string> out;
  if (items == nullptr) return out;
  out.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) out.push_back(cstr(items[i]));
  return out;
}

/** toKickedReason 把引擎枚举摊成 C 枚举。**显式列全**，加了新值编译器会提醒。 */
imrtc_v1_kicked_reason toKickedReason(imrtc::KickedReason reason) {
  switch (reason) {
    case imrtc::KickedReason::AuthExpired: return IMRTC_V1_KICKED_AUTH_EXPIRED;
    case imrtc::KickedReason::ConfigRejected: return IMRTC_V1_KICKED_CONFIG_REJECTED;
    case imrtc::KickedReason::TakenOver: break;
  }
  return IMRTC_V1_KICKED_TAKEN_OVER;
}

/**
 * CObserver 把 §7.5 的回调表转发到 C 的函数指针。
 *
 * **每个回调都先判空**：宿主只关心几个事件是常态，留 NULL 不该崩。
 * 字符串直接用 std::string::c_str()——它们活到回调返回为止，正是头文件承诺的
 * 「指针只在该次回调期间有效」。
 */
class CObserver : public CallEngineObserver {
public:
  explicit CObserver(const imrtc_v1_observer& table) : table_(table) {}

  void onConnected(const std::string& sessionId, bool resumed) override {
    if (table_.on_connected) table_.on_connected(table_.user_data, sessionId.c_str(), fromBool(resumed));
  }
  void onDisconnected() override {
    if (table_.on_disconnected) table_.on_disconnected(table_.user_data);
  }
  void onKickedOut(imrtc::KickedReason reason) override {
    if (table_.on_kicked_out) table_.on_kicked_out(table_.user_data, toKickedReason(reason));
  }
  void onError(std::int32_t code, const std::string& name, const std::string& forType) override {
    if (table_.on_error) table_.on_error(table_.user_data, code, name.c_str(), forType.c_str());
  }

  void onCallReceived(const imrtc::CallInvite& invite) override {
    if (!table_.on_call_received) return;
    // 字符串数组要摊成 const char* 的连续数组：C 那边收的是指针 + 长度。
    std::vector<const char*> ids;
    ids.reserve(invite.calleeIds.size());
    for (const std::string& id : invite.calleeIds) ids.push_back(id.c_str());

    imrtc_v1_call_invite out{};
    out.struct_size = sizeof(out);
    out.call_id = invite.callId.c_str();
    out.caller = invite.caller.c_str();
    out.callee_ids = ids.empty() ? nullptr : ids.data();
    out.callee_count = static_cast<std::uint32_t>(ids.size());
    out.media_type = invite.mediaType.c_str();
    out.is_group = fromBool(invite.isGroup);
    table_.on_call_received(table_.user_data, &out);
  }

  void onCallBegin(const imrtc::CallBegin& begin) override {
    if (!table_.on_call_begin) return;
    imrtc_v1_call_begin out{};
    out.struct_size = sizeof(out);
    out.call_id = begin.callId.c_str();
    out.room_id = begin.roomId.c_str();
    out.media_type = begin.mediaType.c_str();
    out.is_group = fromBool(begin.isGroup);
    out.role = begin.role.c_str();
    table_.on_call_begin(table_.user_data, &out);
  }

  void onCallEnd(const imrtc::CallEnd& end) override {
    if (!table_.on_call_end) return;
    imrtc_v1_call_end out{};
    out.struct_size = sizeof(out);
    out.call_id = end.callId.c_str();
    out.reason = end.reason.c_str();
    out.duration_sec = end.durationSec;
    out.ended_by = end.endedBy.c_str();
    table_.on_call_end(table_.user_data, &out);
  }

  void onCallMissed(const imrtc::CallMissed& missed) override {
    if (!table_.on_call_missed) return;
    imrtc_v1_call_missed out{};
    out.struct_size = sizeof(out);
    out.call_id = missed.callId.c_str();
    out.caller = missed.caller.c_str();
    out.reason = missed.reason.c_str();
    table_.on_call_missed(table_.user_data, &out);
  }

  void onCallCancelled(const std::string& by) override { one(table_.on_call_cancelled, by); }
  void onCallRejected(const std::string& uid) override { one(table_.on_call_rejected, uid); }
  void onCallBusy(const std::string& uid) override { one(table_.on_call_busy, uid); }
  void onCallNoAnswer(const std::string& uid) override { one(table_.on_call_no_answer, uid); }
  void onUserEnter(const std::string& uid) override { one(table_.on_user_enter, uid); }
  void onUserLeave(const std::string& uid) override { one(table_.on_user_leave, uid); }
  void onUserAccept(const std::string& uid) override { one(table_.on_user_accept, uid); }
  void onUserReject(const std::string& uid) override { one(table_.on_user_reject, uid); }
  void onUserNoResponse(const std::string& uid) override { one(table_.on_user_no_response, uid); }
  void onRoomJoined(const std::string& roomId) override { one(table_.on_room_joined, roomId); }
  void onRoomLeft(const std::string& roomId) override { one(table_.on_room_left, roomId); }

  void onHandledOnOtherDevice(const std::string& callId, const std::string& action) override {
    if (table_.on_handled_on_other_device) {
      table_.on_handled_on_other_device(table_.user_data, callId.c_str(), action.c_str());
    }
  }
  void onRoomClosed(const std::string& roomId, const std::string& reason) override {
    if (table_.on_room_closed) {
      table_.on_room_closed(table_.user_data, roomId.c_str(), reason.c_str());
    }
  }
  void onUserAudioAvailable(const std::string& uid, bool available) override {
    if (table_.on_user_audio_available) {
      table_.on_user_audio_available(table_.user_data, uid.c_str(), fromBool(available));
    }
  }
  void onUserVideoAvailable(const std::string& uid, bool available) override {
    if (table_.on_user_video_available) {
      table_.on_user_video_available(table_.user_data, uid.c_str(), fromBool(available));
    }
  }

  void onActiveSpeakers(const std::vector<imrtc::Speaker>& speakers) override {
    if (!table_.on_active_speakers) return;
    std::vector<imrtc_v1_speaker> out;
    out.reserve(speakers.size());
    for (const imrtc::Speaker& speaker : speakers) {
      imrtc_v1_speaker item{};
      item.struct_size = sizeof(item);
      item.uid = speaker.uid.c_str();
      item.participant_id = speaker.participantId.c_str();
      item.volume = speaker.volume;
      out.push_back(item);
    }
    table_.on_active_speakers(table_.user_data, out.empty() ? nullptr : out.data(),
                              static_cast<std::uint32_t>(out.size()));
  }

  void onNetworkQuality(const std::vector<imrtc::QualityEntry>& entries) override {
    if (!table_.on_network_quality) return;
    std::vector<imrtc_v1_quality> out;
    out.reserve(entries.size());
    for (const imrtc::QualityEntry& entry : entries) {
      imrtc_v1_quality item{};
      item.struct_size = sizeof(item);
      item.uid = entry.uid.c_str();
      item.participant_id = entry.participantId.c_str();
      item.level = entry.level;
      out.push_back(item);
    }
    table_.on_network_quality(table_.user_data, out.empty() ? nullptr : out.data(),
                              static_cast<std::uint32_t>(out.size()));
  }

private:
  using OneArg = void (*)(void*, const char*);
  void one(OneArg fn, const std::string& value) const {
    if (fn) fn(table_.user_data, value.c_str());
  }

  imrtc_v1_observer table_;
};

/*
  **枚举值必须与 C 头一一对应**。这里直接把 C++ 的枚举强转成 int32 交出去，
  所以一旦有人重排了 CallState / RoomState 的声明顺序，C 宿主收到的就是**错的状态**
  ——而且不报错、不崩，只是界面开始胡说八道。下面这组断言让那种改动在编译期就挂掉。
*/
static_assert(static_cast<int>(imrtc::CallState::Idle) == IMRTC_V1_CALL_IDLE, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Inviting) == IMRTC_V1_CALL_INVITING, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Ringing) == IMRTC_V1_CALL_RINGING, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Accepting) == IMRTC_V1_CALL_ACCEPTING, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Connecting) == IMRTC_V1_CALL_CONNECTING, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Connected) == IMRTC_V1_CALL_CONNECTED, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Idle) == IMRTC_V1_ROOM_IDLE, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Joining) == IMRTC_V1_ROOM_JOINING, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Joined) == IMRTC_V1_ROOM_JOINED, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Leaving) == IMRTC_V1_ROOM_LEAVING, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Reconnecting) == IMRTC_V1_ROOM_RECONNECTING,
              "枚举漂了");

}  // namespace

/**
 * imrtc_v1_engine 是不透明句柄的真身。
 *
 * 观察者用 shared_ptr 持有、Engine 那边是 weak_ptr：句柄一销毁，
 * 观察者随之释放，飞在半路的回调自然落空而不是打到野指针上。
 */
struct imrtc_v1_engine {
  std::unique_ptr<CallEngine> engine;
  std::shared_ptr<CObserver> observer;
};

namespace {

/**
 * guard 把每个入口点包起来：判句柄、接异常。
 *
 * **异常绝不跨越 C ABI**——那是未定义行为，在 MSVC 上直接 terminate。
 * 内部用异常是允许的（CONVENTIONS §7），但必须在这条边界上转成错误码。
 */
template <typename Fn>
std::int32_t guard(imrtc_v1_engine* handle, Fn&& body) {
  if (handle == nullptr || !handle->engine) return IMRTC_V1_ERR_BAD_PARAMS;
  try {
    body(*handle->engine);
    return IMRTC_V1_OK;
  } catch (const imrtc::RtcError& error) {
    return error.code();
  } catch (...) {
    return IMRTC_V1_ERR_INTERNAL;
  }
}

}  // namespace

extern "C" {

std::int32_t imrtc_v1_engine_create(const imrtc_v1_options* options,
                                    imrtc_v1_engine** out_engine) {
  if (out_engine == nullptr || options == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  *out_engine = nullptr;
  // struct_size 是版本闸：宿主的头比我们新（多了字段）也没关系，我们只读认识的那些；
  // 比我们旧则说明它连必填字段都不全，拒掉。
  if (options->struct_size < sizeof(imrtc_v1_options)) return IMRTC_V1_ERR_BAD_PARAMS;
  if (options->url == nullptr || options->device_id == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  /*
    `device_id` 在**边界上**同步拒掉（协议 §2.5：非空 / ≤64 字节 / [A-Za-z0-9_-]）。

    不拦的症状是「登录失败，没有下文」——服务端一律回 1004，而它那句说得很清楚的
    charset 说明到不了宿主手里。Android 真机上踩过：`Build.MODEL` 是 `Pixel 2 XL`，
    带空格，于是 1004 + 无限退避重连，界面只写「登录失败」。

    **只校验不改写**：`device_id` 要跨重启稳定，SDK 悄悄改掉，宿主自己那套设备管理
    就跟服务端对不上账；而「删掉非法字符」更糟——`MI 8` 与 `MI8` 删完撞成同一个 id，
    两台设备会互相顶号。清洗是宿主的事。
  */
  if (!imrtc::deviceIdValid(cstr(options->device_id))) return IMRTC_V1_ERR_BAD_PARAMS;

  try {
    CallEngineOptions engineOptions;
    engineOptions.url = cstr(options->url);
    engineOptions.deviceId = cstr(options->device_id);
    if (options->sdk != nullptr) engineOptions.sdk = cstr(options->sdk);
    if (options->request_timeout_ms > 0) engineOptions.requestTimeoutMs = options->request_timeout_ms;
    engineOptions.transportFactory = []() -> std::unique_ptr<imrtc::Transport> {
      return std::unique_ptr<imrtc::Transport>(new imrtc::IxTransport());
    };

    std::unique_ptr<imrtc_v1_engine> handle(new imrtc_v1_engine());
    handle->engine.reset(new CallEngine(engineOptions));
    *out_engine = handle.release();
    return IMRTC_V1_OK;
  } catch (...) {
    return IMRTC_V1_ERR_INTERNAL;
  }
}

void imrtc_v1_engine_destroy(imrtc_v1_engine* engine) {
  if (engine == nullptr) return;
  /*
    **阻塞到回调静默**：CallEngine 析构 → Connection 析构 → IxTransport 析构 →
    `ws_->stop()` join 掉后台线程。这条链上每一环都是同步的，所以 delete 返回之后
    绝不会再有回调打进来——这正是头文件对宿主的承诺，宿主是 GC 语言时尤其要紧。
  */
  delete engine;
}

std::int32_t imrtc_v1_engine_set_observer(imrtc_v1_engine* engine,
                                          const imrtc_v1_observer* observer) {
  if (engine == nullptr || !engine->engine) return IMRTC_V1_ERR_BAD_PARAMS;
  if (observer == nullptr) {
    engine->observer.reset();
    engine->engine->setObserver(std::weak_ptr<CallEngineObserver>());
    return IMRTC_V1_OK;
  }
  if (observer->struct_size < sizeof(imrtc_v1_observer)) return IMRTC_V1_ERR_BAD_PARAMS;
  try {
    engine->observer = std::make_shared<CObserver>(*observer);
    engine->engine->setObserver(engine->observer);
    return IMRTC_V1_OK;
  } catch (...) {
    return IMRTC_V1_ERR_INTERNAL;
  }
}

std::int32_t imrtc_v1_engine_tick(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.tick(); });
}

std::int32_t imrtc_v1_login(imrtc_v1_engine* engine, const char* token) {
  if (token == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [token](CallEngine& target) { target.login(cstr(token)); });
}

std::int32_t imrtc_v1_logout(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.logout(); });
}

std::int32_t imrtc_v1_update_token(imrtc_v1_engine* engine, const char* token) {
  if (token == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [token](CallEngine& target) { target.updateToken(cstr(token)); });
}

std::int32_t imrtc_v1_call(imrtc_v1_engine* engine, const char* const* callee_ids,
                           std::uint32_t callee_count, const char* media_type,
                           imrtc_v1_bool is_group) {
  if (callee_ids == nullptr || callee_count == 0) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) {
    target.call(toStrings(callee_ids, callee_count), cstr(media_type), toBool(is_group));
  });
}

std::int32_t imrtc_v1_accept(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.accept(); });
}
std::int32_t imrtc_v1_reject(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.reject(); });
}
std::int32_t imrtc_v1_cancel(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.cancel(); });
}
std::int32_t imrtc_v1_hangup(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.hangup(); });
}

std::int32_t imrtc_v1_invite_more(imrtc_v1_engine* engine, const char* const* callee_ids,
                                  std::uint32_t callee_count) {
  if (callee_ids == nullptr || callee_count == 0) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) {
    target.inviteMore(toStrings(callee_ids, callee_count));
  });
}

std::int32_t imrtc_v1_join_call(imrtc_v1_engine* engine, const char* call_id) {
  if (call_id == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [call_id](CallEngine& target) { target.joinCall(cstr(call_id)); });
}

std::int32_t imrtc_v1_join_room(imrtc_v1_engine* engine, const char* room_id,
                                const char* room_token) {
  if (room_id == nullptr || room_token == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) { target.joinRoom(cstr(room_id), cstr(room_token)); });
}

std::int32_t imrtc_v1_leave_room(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.leaveRoom(); });
}

std::int32_t imrtc_v1_set_remote_layer(imrtc_v1_engine* engine, const char* uid,
                                       const char* layer) {
  if (uid == nullptr || layer == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  /*
    在**边界上**挡掉非法层名，不让它上线路。

    协议 §2.4 规则 6 规定枚举收到集合外的值**必须兜底**（max_layer 兜到 "l"）
    而不是报错，因为这条兜底正是 §10「新增枚举值不算破坏兼容」的前提。
    也就是说**服务端按设计不会拒绝我们**——2026-09-07 用一个 "zzz" 实测：
    照收、回 .ok（服务端同日补了一条 Warn 日志，但那是给运维看的，不回给客户端）。

    于是层名写错一个字母，宿主这边收不到任何反馈，那条流被降到最低层——
    画面只是糊。这道同步的 BAD_PARAMS 因此是**宿主唯一会收到的反馈**，
    不是「省一个来回」。

    枚举本可以从根上杜绝它，但本头里 media_type 等同类参数一律是字符串，
    为一个参数换风格不划算。
  */
  if (!isValidLayer(cstr(layer))) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) { target.setRemoteLayer(cstr(uid), cstr(layer)); });
}

std::int32_t imrtc_v1_open_mic(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.openMic(); });
}
std::int32_t imrtc_v1_close_mic(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.closeMic(); });
}
std::int32_t imrtc_v1_open_camera(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.openCamera(); });
}
std::int32_t imrtc_v1_close_camera(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.closeCamera(); });
}

std::int32_t imrtc_v1_attach_view(imrtc_v1_engine* engine, const char* uid, void* native_handle) {
  if (uid == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) { target.attachView(cstr(uid), native_handle); });
}

std::int32_t imrtc_v1_attach_local_view(imrtc_v1_engine* engine, void* native_handle) {
  return guard(engine, [=](CallEngine& target) { target.attachLocalView(native_handle); });
}

std::int32_t imrtc_v1_get_call_state(imrtc_v1_engine* engine, std::int32_t* out_state) {
  if (out_state == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [out_state](CallEngine& target) {
    *out_state = static_cast<std::int32_t>(target.callState());
  });
}

std::int32_t imrtc_v1_get_room_state(imrtc_v1_engine* engine, std::int32_t* out_state) {
  if (out_state == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [out_state](CallEngine& target) {
    *out_state = static_cast<std::int32_t>(target.roomState());
  });
}

const char* imrtc_v1_error_name(std::int32_t code) {
  // 返回**静态字符串**：不能返回 std::string 的 c_str()，那块内存出了函数就没了。
  const imrtc::ErrorDefinition* def = imrtc::lookupError(code);
  return def == nullptr ? "unknown" : def->name.c_str();
}

const char* imrtc_v1_version(void) { return "0.1.0"; }

}  // extern "C"
