/*
 * imrtc_c_debug.h —— 调试密钥本地签票的 C ABI（设计稿 DEBUG_KEY_DESIGN.md §4）。
 *
 * **仅调试 / 联调用，不得进生产。** 把调试密钥放进客户端等于公开它；没有宿主后台时可以用它
 * 在本机签一枚登录票，**上线必须换成宿主后端 `POST /v1/tokens` 换票**。每次调用引擎都会打一条 warn 日志。
 * 从 imrtc_c.h 拆出来只为守体量红线；宿主只 include imrtc_c.h 就够了（它末尾会带上本头）。
 * 兼容性规则同 imrtc_c.h：字段只许追加，永不重排、永不删除。
 */
#ifndef IMRTC_V1_C_DEBUG_H
#define IMRTC_V1_C_DEBUG_H

#include "imrtc_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 调试签票入参。`struct_size` 填 `sizeof(imrtc_v1_debug_token_options)`。字符串一律 UTF-8。
 *
 * - `app_id` / `key_id` / `secret` / `uid` 必填；`key_id` 必须以 `dbg-` 开头；
 *   `uid` 不得含空白、不超过 64 字节；
 * - `device_id` 可为 NULL / 空（不写 `did` 声明）；
 * - `ttl_sec` 0 = 12 小时，钳到 [60, 2592000]（30 天）；
 * - `now_unix` 0 = 取系统时钟；非 0 时用它当「现在」（Unix 秒，给测试注入用）。
 */
typedef struct imrtc_v1_debug_token_options {
  uint32_t struct_size;
  const char* app_id;
  const char* key_id;
  const char* secret;
  const char* uid;
  const char* device_id;
  int64_t ttl_sec;
  int64_t now_unix;
} imrtc_v1_debug_token_options;

/**
 * **仅调试**：本地签一枚 HS256 登录票，写进宿主给的缓冲区（NUL 结尾，谁分配谁释放，引擎不分配）。
 *
 * 返回 `IMRTC_V1_OK` 成功，`*out_len`（可为 NULL）写入不含 NUL 的票长度；
 * 入参不合法（含 NULL、`key_id` 不是 `dbg-` 开头、`struct_size` 偏小）返回 `IMRTC_V1_ERR_BAD_PARAMS`；
 * 缓冲区放不下也返回 `IMRTC_V1_ERR_BAD_PARAMS`，此时 `*out_len` 是需要的长度（不含 NUL），`out` 不被改写。
 * 一枚票约 250 字节，给 512 字节足够。产物直接交给 `imrtc_v1_login`。
 */
IMRTC_API int32_t imrtc_v1_debug_sign_token(const imrtc_v1_debug_token_options* options, char* out,
                                            uint32_t out_capacity, uint32_t* out_len);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* IMRTC_V1_C_DEBUG_H */
