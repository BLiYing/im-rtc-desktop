/*
 * imrtc_c_history.h —— 通话记录查询（`GET /v1/calls`）的 C ABI。
 *
 * 从 imrtc_c.h 拆出来只为守体量红线；**宿主只 include imrtc_c.h 就够了**，它末尾会带上本头。
 * 兼容性规则同 imrtc_c.h：字段只许追加，永不重排、永不删除。
 */
#ifndef IMRTC_V1_C_HISTORY_H
#define IMRTC_V1_C_HISTORY_H

#include "imrtc_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 通话记录 ---- */

/** 通话记录里的一位成员。`state` 是这位成员在这通电话里的结局，原样透传。 */
typedef struct imrtc_v1_call_member {
  const char* uid;
  const char* state;
} imrtc_v1_call_member;

/**
 * 一条通话记录，字段与服务端 `GET /v1/calls` 一一对应。所有字符串**只在该次回调期间有效**。
 *
 * `reason` 是通话的最终结局（`hangup` / `cancel` / `reject` / `no_answer`…），**不分角色**：
 * 要显示「已取消」还是「对方已取消」，用 `caller` 与自己的 uid 比出角色再定文案。
 * `media_type` 是 `"audio"` 或 `"video"`。
 */
typedef struct imrtc_v1_call_record {
  const char* call_id;
  const char* room_id;
  const char* caller;
  const char* media_type;
  imrtc_v1_bool is_group;
  const char* reason;
  const char* ended_by;
  int64_t duration_sec;
  int64_t started_at_ms;
  int64_t connected_at_ms;
  int64_t ended_at_ms;
  const char* user_data;
  const char* chat_group_id;
  const imrtc_v1_call_member* members;
  uint32_t member_count;
} imrtc_v1_call_record;

/**
 * 一次通话记录查询的结果。**恰好调一次**，在调 Engine 的那个线程上（`imrtc_v1_engine_tick` 里，
 * 或本地就地拒绝时在发起函数返回之前）；`imrtc_v1_engine_destroy` 时还没回来的在 destroy 返回之前回 2005。
 *
 * - `code == 0` 为成功：`records` / `record_count` 是这一页（按发起时间倒序），`next_cursor` 是下一页的游标，
 *   **0 表示已经到底**；
 * - 失败时 `code` 是错误码（2007 没登录、1101 票被拒、2003 网络不通、1501 其它失败、2005 没有 HTTP 实现或已销毁），
 *   `name` 是机读名，`records` 为 NULL、`record_count` 为 0。
 *
 * 指针只在该次回调期间有效，要留就自己拷。
 */
typedef void (*imrtc_v1_call_history_cb)(void* user_data, int32_t code, const char* name,
                                         const imrtc_v1_call_record* records,
                                         uint32_t record_count, int64_t next_cursor);

/**
 * 查自己的通话记录（`GET /v1/calls`），按发起时间倒序，**游标翻页**。
 *
 * `limit` 夹在 1..200（传 0 或负数按 1；想要默认值传 20）；`cursor` 首页传 0，
 * 下一页传上一页回调里的 `next_cursor`。**必须已登录**（用登录那枚票，含 `imrtc_v1_update_token` 换过的）。
 * 服务端只返回本人参与过的通话，所以没有 uid 参数。宿主也可以不用它，自己拿 `on_call_end` 存。
 *
 * **cb 必须非 NULL**（没有 on_error 兜底，静默丢结果没有意义）。
 * 返回值只表示「根本没受理」（NULL 句柄 / cb 为 NULL）——返回非 0 时不会再调 cb。
 */
IMRTC_API int32_t imrtc_v1_fetch_call_history(imrtc_v1_engine* engine, int32_t limit,
                                              int64_t cursor, imrtc_v1_call_history_cb cb,
                                              void* user_data);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* IMRTC_V1_C_HISTORY_H */
