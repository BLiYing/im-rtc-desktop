#include "imrtc/imrtc_c.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "imrtc/CallEngine.h"
#include "imrtc/Errors.h"
#include "imrtc/Log.h"
#include "imrtc/IxTransport.h"

#include "CApiConvert.h"
#include "CObserver.h"

/**
 * C++ → C 的转换层。**异常在这里被吃掉转成错误码**（CONVENTIONS §2 红线 5）。
 *
 * 这一层刻意很薄：它不做任何决策，只做三件事——查参数、转类型、接异常。
 * 一旦这里出现「如果…就…」的业务判断，那条判断就只对 C 宿主生效，
 * 而 Qt Demo 也走这条路，两边行为就会漂。
 *
 * `CObserver`（回调表转发）与两套枚举之间的转换函数拆到了 CObserver.{h,cpp} /
 * CApiConvert.{h,cpp}（CONVENTIONS §3 体量红线），这里只留 `imrtc_v1_engine_*`
 * 这一族入口点自己的控制流。
 */
namespace {

using imrtc::CallEngine;
using imrtc::CallEngineObserver;
using imrtc::CallEngineOptions;
using imrtc::capi_detail::CObserver;
using imrtc::capi_detail::cstr;
using imrtc::capi_detail::fromLogLevel;
using imrtc::capi_detail::isValidLayer;
using imrtc::capi_detail::toBool;
using imrtc::capi_detail::toCompletion;
using imrtc::capi_detail::toLogLevel;
using imrtc::capi_detail::toStrings;

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
  /*
    `on_disconnected_ex` 是 2026-09-15 追加的尾部字段。旧宿主的头里没有它，
    它的 `struct_size` 天然只到 `on_room_closed` 那里——**最低线要用 offsetof 记，
    不能用 sizeof(imrtc_v1_observer)**（那是含新字段的现在值）。用现在值的话，
    旧宿主会被这条新字段直接拒之门外，「只追加不删除」就白追加了。
  */
  constexpr std::size_t kMinObserverSize = offsetof(imrtc_v1_observer, on_disconnected_ex);
  if (observer->struct_size < kMinObserverSize) return IMRTC_V1_ERR_BAD_PARAMS;
  try {
    /*
      **只拷宿主实际给出的那么多字节，其余（含新字段）保持零初始化的 nullptr**。
      `CObserver(*observer)` 那种整份结构体拷贝会把 `on_disconnected_ex` 那几个字节
      读到宿主根本没分配的内存上——旧宿主的 struct 更小，这是未定义行为。
    */
    imrtc_v1_observer table{};
    const std::size_t copyBytes = std::min<std::size_t>(observer->struct_size, sizeof(table));
    std::memcpy(&table, observer, copyBytes);
    engine->observer = std::make_shared<CObserver>(table);
    engine->engine->setObserver(engine->observer);
    return IMRTC_V1_OK;
  } catch (...) {
    return IMRTC_V1_ERR_INTERNAL;
  }
}

std::int32_t imrtc_v1_engine_tick(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.tick(); });
}

std::int32_t imrtc_v1_login(imrtc_v1_engine* engine, const char* token, imrtc_v1_result_cb cb,
                            void* user_data) {
  if (token == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) { target.login(cstr(token), toCompletion(cb, user_data)); });
}

std::int32_t imrtc_v1_logout(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.logout(); });
}

std::int32_t imrtc_v1_update_token(imrtc_v1_engine* engine, const char* token) {
  if (token == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [token](CallEngine& target) { target.updateToken(cstr(token)); });
}

std::int32_t imrtc_v1_set_app_foreground(imrtc_v1_engine* engine, imrtc_v1_bool foreground) {
  return guard(engine, [foreground](CallEngine& target) { target.setAppForeground(foreground != 0); });
}

std::int32_t imrtc_v1_notify_network_changed(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.notifyNetworkChanged(); });
}

std::int32_t imrtc_v1_call(imrtc_v1_engine* engine, const char* const* callee_ids,
                           std::uint32_t callee_count, const char* media_type,
                           imrtc_v1_bool is_group, imrtc_v1_result_cb cb, void* user_data) {
  if (callee_ids == nullptr || callee_count == 0) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) {
    target.call(toStrings(callee_ids, callee_count), cstr(media_type), toBool(is_group),
                toCompletion(cb, user_data));
  });
}

std::int32_t imrtc_v1_call_ex(imrtc_v1_engine* engine, const char* const* callee_ids,
                              std::uint32_t callee_count, const char* media_type,
                              const imrtc_v1_call_options* options, imrtc_v1_result_cb cb,
                              void* user_data) {
  if (callee_ids == nullptr || callee_count == 0) return IMRTC_V1_ERR_BAD_PARAMS;
  imrtc::CallOptions callOptions;
  if (options != nullptr) {
    // struct_size 是版本闸：宿主的头比我们新也没关系，我们只读认识的那些；
    // 比我们旧则连必填字段都不全，拒掉（规矩同 imrtc_v1_engine_create）。
    if (options->struct_size < sizeof(imrtc_v1_call_options)) return IMRTC_V1_ERR_BAD_PARAMS;
    callOptions.chatGroupId = cstr(options->chat_group_id);
    callOptions.userData = cstr(options->user_data);
    callOptions.timeoutSec = options->timeout_sec;
  }
  const bool isGroup = options != nullptr && toBool(options->is_group);
  return guard(engine, [=](CallEngine& target) {
    target.call(toStrings(callee_ids, callee_count), cstr(media_type), isGroup, callOptions,
                toCompletion(cb, user_data));
  });
}

std::int32_t imrtc_v1_accept(imrtc_v1_engine* engine, imrtc_v1_result_cb cb, void* user_data) {
  return guard(engine, [=](CallEngine& target) { target.accept(toCompletion(cb, user_data)); });
}
std::int32_t imrtc_v1_reject(imrtc_v1_engine* engine, imrtc_v1_result_cb cb, void* user_data) {
  return guard(engine, [=](CallEngine& target) { target.reject(toCompletion(cb, user_data)); });
}
std::int32_t imrtc_v1_cancel(imrtc_v1_engine* engine, imrtc_v1_result_cb cb, void* user_data) {
  return guard(engine, [=](CallEngine& target) { target.cancel(toCompletion(cb, user_data)); });
}
std::int32_t imrtc_v1_hangup(imrtc_v1_engine* engine, imrtc_v1_result_cb cb, void* user_data) {
  return guard(engine, [=](CallEngine& target) { target.hangup(toCompletion(cb, user_data)); });
}
std::int32_t imrtc_v1_force_end(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.forceEnd(); });
}

std::int32_t imrtc_v1_invite_more(imrtc_v1_engine* engine, const char* const* callee_ids,
                                  std::uint32_t callee_count, imrtc_v1_result_cb cb,
                                  void* user_data) {
  if (callee_ids == nullptr || callee_count == 0) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) {
    target.inviteMore(toStrings(callee_ids, callee_count), toCompletion(cb, user_data));
  });
}

std::int32_t imrtc_v1_join_call(imrtc_v1_engine* engine, const char* call_id, imrtc_v1_result_cb cb,
                                void* user_data) {
  if (call_id == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) {
    target.joinCall(cstr(call_id), toCompletion(cb, user_data));
  });
}

std::int32_t imrtc_v1_join_room(imrtc_v1_engine* engine, const char* room_id,
                                const char* room_token, imrtc_v1_result_cb cb, void* user_data) {
  if (room_id == nullptr || room_token == nullptr) return IMRTC_V1_ERR_BAD_PARAMS;
  return guard(engine, [=](CallEngine& target) {
    target.joinRoom(cstr(room_id), cstr(room_token), toCompletion(cb, user_data));
  });
}

std::int32_t imrtc_v1_leave_room(imrtc_v1_engine* engine, imrtc_v1_result_cb cb, void* user_data) {
  return guard(engine, [=](CallEngine& target) { target.leaveRoom(toCompletion(cb, user_data)); });
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

std::int32_t imrtc_v1_open_microphone(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.openMic(); });
}
std::int32_t imrtc_v1_close_microphone(imrtc_v1_engine* engine) {
  return guard(engine, [](CallEngine& target) { target.closeMic(); });
}

// 已弃用别名：语义与上面两个一字不差，直接转发，不许各写一份判断走漂。
std::int32_t imrtc_v1_open_mic(imrtc_v1_engine* engine) { return imrtc_v1_open_microphone(engine); }
std::int32_t imrtc_v1_close_mic(imrtc_v1_engine* engine) {
  return imrtc_v1_close_microphone(engine);
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

// 字符串字面量常量，静态存储，指针稳定——与 error_name 同一条要求。
const char* imrtc_v1_version(void) { return imrtc::kSdkVersion; }

void imrtc_v1_set_log_sink(imrtc_v1_log_sink sink, void* user_data) {
  if (sink == nullptr) {
    imrtc::setLogSink(nullptr);
    return;
  }
  imrtc::setLogSink([sink, user_data](imrtc::LogLevel level, const std::string& message,
                                      const imrtc::LogFields& fields) {
    /*
      摊成 C 数组再交出去。**每个元素都要填 struct_size**——宿主是按 sizeof 的
      步长走这个数组的，不填它将来追加字段就会让已发出去的宿主读错位置。

      指针指向的是本函数栈上的 vector 与 fields 里的 std::string，
      两者都活到回调返回为止，正是头文件承诺的「只在该次回调期间有效」。
    */
    std::vector<imrtc_v1_log_field> items;
    items.reserve(fields.size());
    for (const auto& field : fields) {
      imrtc_v1_log_field item{};
      item.struct_size = static_cast<std::uint32_t>(sizeof(imrtc_v1_log_field));
      item.key = field.first.c_str();
      item.value = field.second.c_str();
      items.push_back(item);
    }
    sink(user_data, toLogLevel(level), message.c_str(), items.empty() ? nullptr : items.data(),
         static_cast<std::uint32_t>(items.size()));
  });
}

void imrtc_v1_set_log_level(imrtc_v1_log_level level) {
  imrtc::setLogLevel(fromLogLevel(level));
}

void imrtc_v1_log(imrtc_v1_log_level level, const char* message, const imrtc_v1_log_field* fields,
                  std::uint32_t field_count) {
  if (message == nullptr) return;
  imrtc::LogFields out;
  if (fields != nullptr) {
    out.reserve(field_count);
    for (std::uint32_t i = 0; i < field_count; ++i) {
      // 宿主的头可能比我们旧（结构体更小），所以按它自报的 struct_size 走，
      // 不能按我们自己的 sizeof —— 那正是 struct_size 存在的意义。
      const auto* item = reinterpret_cast<const imrtc_v1_log_field*>(
          reinterpret_cast<const unsigned char*>(fields) +
          static_cast<std::size_t>(i) * fields[0].struct_size);
      if (item->key == nullptr || item->value == nullptr) continue;
      out.emplace_back(cstr(item->key), cstr(item->value));
    }
  }
  imrtc::log(fromLogLevel(level), cstr(message), std::move(out));
}

}  // extern "C"
