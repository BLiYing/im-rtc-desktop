/*
 * imrtc_c.h —— im-rtc 桌面端的**对外唯一边界**。
 *
 * 这是一份纯 C 头文件。C++ 没有跨编译器 ABI（MSVC↔MinGW、/MD↔/MT、
 * Debug↔Release CRT、libstdc++↔libc++，任何一处不一致就链不上或运行时崩），
 * 导出 C++ 类等于「只服务与我们同工具链的宿主」；导出 C 等于「所有语言都能接」——
 * Qt / MFC / WPF+C# / Delphi / Java / Python / Flutter / Swift。
 *
 * 规矩见 im-rtc-desktop/CONVENTIONS.md §2。三条最容易踩的：
 *
 *   1. 回调发生在 **Engine 的线程**上，**切回 UI 线程是宿主的责任**
 *      （Qt 用 QueuedConnection，C# 用 Dispatcher.Invoke）。
 *   2. 回调里给出的所有指针**只在该次回调期间有效**。要留就自己拷。
 *   3. `imrtc_v1_engine_destroy` **会阻塞到所有回调静默为止**，返回之后
 *      绝不会再有回调打进来。宿主是 C# / Java 这类 GC 语言时，
 *      这一点尤其重要——它没法替你保活。
 *
 * 兼容性：结构体第一个字段一律是 `struct_size`，由**宿主**填 `sizeof(...)`。
 * 字段**只许追加**，永不重排、永不删除；枚举显式赋值，绝不依赖声明顺序。
 */
#ifndef IMRTC_V1_C_H
#define IMRTC_V1_C_H

#include <stdint.h>

/*
 * 静态链接本库的构建（比如本仓的测试与 Demo）会在命令行上把 IMRTC_API 定义成空——
 * 那时 dllimport / visibility 都不该出现，所以这里先让路。
 */
#ifndef IMRTC_API
#  if defined(_WIN32)
#    if defined(IMRTC_BUILDING_SHARED)
#      define IMRTC_API __declspec(dllexport)
#    else
#      define IMRTC_API __declspec(dllimport)
#    endif
#  else
#    define IMRTC_API __attribute__((visibility("default")))
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 错误码：与 RTC_PROTOCOL.md §7 同一份表。0 = 成功。 ---- */
enum {
  IMRTC_V1_OK = 0,
  /** 宿主在错误状态下调方法（比如没登录就拨号、第二次 accept）。 */
  IMRTC_V1_ERR_INVALID_STATE = 2005,
  /** 传进来的参数不合法（空指针、struct_size 对不上）。 */
  IMRTC_V1_ERR_BAD_PARAMS = 1004,
  /** 内部错误兜底。**异常绝不跨越 C ABI**，都在边界上被转成它。 */
  IMRTC_V1_ERR_INTERNAL = 1501
};

/** 通话状态，与 RTC_PROTOCOL.md §5.1 一一对应。 */
typedef enum imrtc_v1_call_state {
  IMRTC_V1_CALL_IDLE = 0,
  IMRTC_V1_CALL_INVITING = 1,
  IMRTC_V1_CALL_RINGING = 2,
  IMRTC_V1_CALL_ACCEPTING = 3,
  IMRTC_V1_CALL_CONNECTING = 4,
  IMRTC_V1_CALL_CONNECTED = 5
} imrtc_v1_call_state;

/** 房间状态，与 §5.3 一一对应。 */
typedef enum imrtc_v1_room_state {
  IMRTC_V1_ROOM_IDLE = 0,
  IMRTC_V1_ROOM_JOINING = 1,
  IMRTC_V1_ROOM_JOINED = 2,
  IMRTC_V1_ROOM_LEAVING = 3,
  IMRTC_V1_ROOM_RECONNECTING = 4
} imrtc_v1_room_state;

/** 不透明句柄。**宿主永远不该知道它里面是什么**。 */
typedef struct imrtc_v1_engine imrtc_v1_engine;

/**
 * 布尔一律用 int32_t（0 / 1），**不用 C 的 _Bool**。
 *
 * 各语言对「一个字节的布尔」与「四个字节的 Win32 BOOL」的编组默认值不一样——
 * C# 的 P/Invoke 默认按 4 字节 BOOL 编组 bool，直接对上 _Bool 会读到隔壁的字节。
 */
typedef int32_t imrtc_v1_bool;

/** 一个正在说话的人。volume 0~100。 */
typedef struct imrtc_v1_speaker {
  const char* uid;
  const char* participant_id;
  int64_t volume;
} imrtc_v1_speaker;

/** 一个人的网络质量，level 0~6（0 = unknown）。 */
typedef struct imrtc_v1_quality {
  const char* uid;
  const char* participant_id;
  int64_t level;
} imrtc_v1_quality;

/** 收到的通话邀请，对应 on_call_received。 */
typedef struct imrtc_v1_call_invite {
  uint32_t struct_size;
  const char* call_id;
  const char* caller;
  /** 被叫列表。群通话里被叫要靠它摆占位格。 */
  const char* const* callee_ids;
  uint32_t callee_count;
  const char* media_type;
  imrtc_v1_bool is_group;
} imrtc_v1_call_invite;

/** 通话接通，对应 on_call_begin。 */
typedef struct imrtc_v1_call_begin {
  uint32_t struct_size;
  const char* call_id;
  const char* room_id;
  const char* media_type;
  imrtc_v1_bool is_group;
  /** "caller" / "callee"。 */
  const char* role;
} imrtc_v1_call_begin;

/** 通话结束，对应 on_call_end。**所有结束分支的唯一出口**。 */
typedef struct imrtc_v1_call_end {
  uint32_t struct_size;
  const char* call_id;
  /** §6 的封闭枚举，陌生值已折成 "error"。 */
  const char* reason;
  /** 未接通恒为 0。**别自己算时长**，用这个值。 */
  int64_t duration_sec;
  const char* ended_by;
} imrtc_v1_call_end;

/** 通话中被第三个人呼叫、已被自动回忙线，对应 on_call_missed。 */
typedef struct imrtc_v1_call_missed {
  uint32_t struct_size;
  const char* call_id;
  const char* caller;
  const char* reason;
} imrtc_v1_call_missed;

/**
 * 回调表。**宿主必须把 struct_size 填成 sizeof(imrtc_v1_observer)**，
 * 不关心的回调留 NULL。`user_data` 原样回传，不被解释、不被持有。
 */
typedef struct imrtc_v1_observer {
  uint32_t struct_size;
  void* user_data;

  /* 连接 */
  void (*on_connected)(void* user_data, const char* session_id, imrtc_v1_bool resumed);
  void (*on_disconnected)(void* user_data);
  void (*on_kicked_out)(void* user_data);
  void (*on_error)(void* user_data, int32_t code, const char* name, const char* for_type);

  /* 来电与拨出 */
  void (*on_call_received)(void* user_data, const imrtc_v1_call_invite* invite);
  void (*on_call_begin)(void* user_data, const imrtc_v1_call_begin* begin);
  void (*on_call_end)(void* user_data, const imrtc_v1_call_end* end);
  void (*on_call_missed)(void* user_data, const imrtc_v1_call_missed* missed);
  void (*on_call_cancelled)(void* user_data, const char* by);
  void (*on_call_rejected)(void* user_data, const char* uid);
  void (*on_call_busy)(void* user_data, const char* uid);
  void (*on_call_no_answer)(void* user_data, const char* uid);
  void (*on_handled_on_other_device)(void* user_data, const char* call_id, const char* action);

  /* 成员 */
  void (*on_user_enter)(void* user_data, const char* uid);
  void (*on_user_leave)(void* user_data, const char* uid);
  void (*on_user_accept)(void* user_data, const char* uid);
  void (*on_user_reject)(void* user_data, const char* uid);
  void (*on_user_no_response)(void* user_data, const char* uid);
  void (*on_user_audio_available)(void* user_data, const char* uid, imrtc_v1_bool available);
  void (*on_user_video_available)(void* user_data, const char* uid, imrtc_v1_bool available);

  /* 媒体与质量。数组指针只在回调期间有效。 */
  void (*on_active_speakers)(void* user_data, const imrtc_v1_speaker* speakers, uint32_t count);
  void (*on_network_quality)(void* user_data, const imrtc_v1_quality* entries, uint32_t count);

  /* 房间 */
  void (*on_room_joined)(void* user_data, const char* room_id);
  void (*on_room_left)(void* user_data, const char* room_id);
  void (*on_room_closed)(void* user_data, const char* room_id, const char* reason);
} imrtc_v1_observer;

/** 构造参数。**填 struct_size**。 */
typedef struct imrtc_v1_options {
  uint32_t struct_size;
  /** 信令端点，生产必须 wss://。 */
  const char* url;
  /** ≤64，同一 uid 下唯一且跨重启稳定。 */
  const char* device_id;
  /** 仅用于日志与灰度，可为 NULL。 */
  const char* sdk;
  /** 请求超时毫秒；0 或负数按默认的 10000 处理。 */
  int64_t request_timeout_ms;
} imrtc_v1_options;

/* ---- 生命周期 ---- */

/**
 * 造一个 Engine。成功时 `*out_engine` 非空。
 *
 * 每个 Engine **只能在一个线程上使用**：全部方法（含 tick）与回调都在那个线程。
 */
IMRTC_API int32_t imrtc_v1_engine_create(const imrtc_v1_options* options,
                                         imrtc_v1_engine** out_engine);

/**
 * 销毁 Engine。**阻塞到所有回调静默**，返回之后绝不会再有回调打进来。
 * 传 NULL 是空操作。
 */
IMRTC_API void imrtc_v1_engine_destroy(imrtc_v1_engine* engine);

/** 注册回调表。传 NULL 表示注销。内部只拷贝这张表，不持有 `user_data` 的所有权。 */
IMRTC_API int32_t imrtc_v1_engine_set_observer(imrtc_v1_engine* engine,
                                               const imrtc_v1_observer* observer);

/**
 * 推进时间：心跳、请求超时、退避重连全靠它。宿主按 ~200ms~1s 的粒度调。
 * **Engine 不自己起线程**——宿主的事件循环长什么样我们不知道。
 */
IMRTC_API int32_t imrtc_v1_engine_tick(imrtc_v1_engine* engine);

/* ---- 连接 ---- */

IMRTC_API int32_t imrtc_v1_login(imrtc_v1_engine* engine, const char* token);
IMRTC_API int32_t imrtc_v1_logout(imrtc_v1_engine* engine);
/** 换票。**下次重连生效，不打断当前连接**。 */
IMRTC_API int32_t imrtc_v1_update_token(imrtc_v1_engine* engine, const char* token);

/* ---- 通话 ---- */

/** 发起通话。1v1 恰好 1 个被叫；群 ≤8。 */
IMRTC_API int32_t imrtc_v1_call(imrtc_v1_engine* engine, const char* const* callee_ids,
                                uint32_t callee_count, const char* media_type,
                                imrtc_v1_bool is_group);
IMRTC_API int32_t imrtc_v1_accept(imrtc_v1_engine* engine);
IMRTC_API int32_t imrtc_v1_reject(imrtc_v1_engine* engine);
IMRTC_API int32_t imrtc_v1_cancel(imrtc_v1_engine* engine);
IMRTC_API int32_t imrtc_v1_hangup(imrtc_v1_engine* engine);
/** 群通话中途加人，仅主叫可发。 */
IMRTC_API int32_t imrtc_v1_invite_more(imrtc_v1_engine* engine, const char* const* callee_ids,
                                       uint32_t callee_count);
/** 主动加入一通进行中的群通话。 */
IMRTC_API int32_t imrtc_v1_join_call(imrtc_v1_engine* engine, const char* call_id);

/* ---- 房间 ---- */

IMRTC_API int32_t imrtc_v1_join_room(imrtc_v1_engine* engine, const char* room_id,
                                     const char* room_token);
IMRTC_API int32_t imrtc_v1_leave_room(imrtc_v1_engine* engine);

/**
 * 报某个 uid 的画面**层上界**（协议 §3.5，线路上是 `room.update_layer`）。
 *
 * `layer` 取 `"none"` / `"l"` / `"m"` / `"h"`，其余值直接回
 * `IMRTC_V1_ERR_BAD_PARAMS`，**不会发到线路上**。`"none"` = 暂停下发该
 * Track 的媒体，订阅关系保留。
 *
 * **这道校验必须在这里做，指望不上服务端**（2026-09-07 实测）：协议 §3.5 写着
 * 非法 `max_layer` 回 1306，但当前服务端**不校验**——它照收，SFU 的 `layerRank()`
 * 又把不认识的值兜底成 `0`（等同 `"l"`）。于是写错一个字母的后果是
 * **那条流被永久锁在最低层，而且没有报错、没有日志**。
 *
 * 典型用法：九宫格缩略图报 `"l"`，双击放大那一格报 `"h"`。
 *
 * 三件事必须知道：
 *   1. **是上界不是命令**。SFU 按 `min(你报的, 带宽估计允许的, 实际存在的)` 选层，
 *      所以调完不保证立刻变，它还要等目标层的关键帧。
 *   2. **不触发重协商**，是一条普通的信令请求。
 *   3. **那个人的视频轨还没发布时这次调用会被丢掉**，返回值仍是 0。
 *      宿主通常在 `on_user_enter` 就建好格子，而轨道晚几百毫秒才到——
 *      所以**要在 `on_user_video_available` 里再报一次**。
 *
 * 这条**不需要媒体实现**：纯信令，`WebRTCAdapter` 没落地时也照发。
 */
IMRTC_API int32_t imrtc_v1_set_remote_layer(imrtc_v1_engine* engine, const char* uid,
                                            const char* layer);

/* ---- 媒体 ---- */

IMRTC_API int32_t imrtc_v1_open_mic(imrtc_v1_engine* engine);
IMRTC_API int32_t imrtc_v1_close_mic(imrtc_v1_engine* engine);
IMRTC_API int32_t imrtc_v1_open_camera(imrtc_v1_engine* engine);
IMRTC_API int32_t imrtc_v1_close_camera(imrtc_v1_engine* engine);

/**
 * 把某个 uid 的远端画面挂到宿主的原生窗口上（渲染路径 A，设计 §8.3）。
 *
 * Windows 传 `HWND`、macOS 传 `NSView*`；传 NULL 卸载。
 * 自绘宿主走路径 B（原始帧回调）——那条口子等媒体实现落地后加。
 */
IMRTC_API int32_t imrtc_v1_attach_view(imrtc_v1_engine* engine, const char* uid,
                                       void* native_handle);

/**
 * 把**本端摄像头预览**挂到宿主的原生窗口上（1v1 那一屏右下角的小窗）。
 *
 * 单独一个函数，**不是** `attach_view(自己的 uid, ...)`：引擎不知道你的 uid
 * （那是你与服务端之间的事），而且本端画面来自采集侧，根本没有远端轨道 id。
 * 硬要复用会逼出一个魔法 uid 约定，那是给将来埋雷。
 *
 * 传 NULL 卸载。摄像头还没开时可以先挂，开了自然有画面。
 */
IMRTC_API int32_t imrtc_v1_attach_local_view(imrtc_v1_engine* engine, void* native_handle);

/* ---- 查询（都不分配内存） ---- */

IMRTC_API int32_t imrtc_v1_get_call_state(imrtc_v1_engine* engine, int32_t* out_state);
IMRTC_API int32_t imrtc_v1_get_room_state(imrtc_v1_engine* engine, int32_t* out_state);
/** 错误码的机读名。返回**静态字符串**，不需要释放，未知码返回 "unknown"。 */
IMRTC_API const char* imrtc_v1_error_name(int32_t code);
/** SDK 版本串。返回**静态字符串**，不需要释放。 */
IMRTC_API const char* imrtc_v1_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* IMRTC_V1_C_H */
