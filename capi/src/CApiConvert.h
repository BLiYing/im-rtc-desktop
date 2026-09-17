#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/CallEngine.h"
#include "imrtc/Log.h"
#include "imrtc/imrtc_c.h"

/**
 * CApiConvert —— C ABI 边界上反复要用的小转换函数 + 两套枚举的漂移哨兵。
 *
 * 从 imrtc_c.cpp 拆出来（CONVENTIONS §3 体量红线）：`CObserver` 与 imrtc_c.cpp 里
 * 直接处理 C 参数的那些入口点都要用到它们，各抄一份比抽出来更容易走漂
 * （比如 `isValidLayer` 的名单如果抄两份，协议加一层时只会改对一份）。
 *
 * **纯转换，不做业务判断**——业务判断留在 imrtc_c.cpp 或 CObserver 里。
 */
namespace imrtc {
namespace capi_detail {

/** cstr 把可能为空的 C 字符串收成 std::string。**NULL 当空串**，不是错误。 */
std::string cstr(const char* text);

/** toBool / fromBool 在 C 的 int32 布尔与 C++ bool 之间转（见 imrtc_c.h 里为什么不用 _Bool）。 */
bool toBool(imrtc_v1_bool value);
imrtc_v1_bool fromBool(bool value);

/**
 * isValidLayer 认协议 §3.5 的那四个值。
 *
 * **名单从 `imrtc::layers()` 读，不在这里再抄一份**——协议加一个层的时候，
 * 抄本会静默地把新值挡在门外，而症状是「宿主报了 h，画面还是 m」这种没人查得动的事。
 */
bool isValidLayer(const std::string& layer);

/** toStrings 把 C 的指针数组摊成 vector<string>。items 为 NULL 时返回空。 */
std::vector<std::string> toStrings(const char* const* items, std::uint32_t count);

/**
 * toCompletion 把 C 的结果回调包成引擎的 ActionCompletion。**cb 为 NULL 时返回空函数**——
 * 引擎据此把失败退回 on_error（ACTION_RESULT_DESIGN R7）。
 */
imrtc::ActionCompletion toCompletion(imrtc_v1_result_cb cb, void* userData);

/** toLogLevel / fromLogLevel 在两套枚举之间互转。数值同序，但不许靠这个偷懒。 */
imrtc_v1_log_level toLogLevel(imrtc::LogLevel level);
imrtc::LogLevel fromLogLevel(imrtc_v1_log_level level);

/** toKickedReason 把引擎枚举摊成 C 枚举。**显式列全**，加了新值编译器会提醒。 */
imrtc_v1_kicked_reason toKickedReason(imrtc::KickedReason reason);

/** toEndReason 把引擎的类型化结束原因摊成 C 枚举。**显式列全**，加新值编译器会提醒。 */
imrtc_v1_end_reason toEndReason(imrtc::EndReason reason);

}  // namespace capi_detail
}  // namespace imrtc
