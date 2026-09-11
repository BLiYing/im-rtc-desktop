#pragma once

namespace imrtc {

/**
 * SDK 版本号。**改版本只改这一处**：C ABI 的 `imrtc_v1_version()` 与引擎各 Options 的
 * `sdk` 默认串都从这里取。五端共享大版本（协议不兼容才升大版本）。
 *
 * 是编译期常量、不是函数，所以不会多出任何导出符号（`scripts/check-abi.sh` 守着）。
 */
inline constexpr char kSdkVersion[] = "1.0.0";

}  // namespace imrtc
