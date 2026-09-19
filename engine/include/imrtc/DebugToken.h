#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace imrtc {

/**
 * 调试密钥本地签票（设计稿 `im-rtc-server/docs/design/DEBUG_KEY_DESIGN.md` §4）。
 *
 * **仅联调用**：没有宿主后台时，拿控制台发的 `dbg-` 调试密钥在本机直接签一枚登录票。
 * 密钥放进客户端就等于公开，所以**上线必须换成宿主后端 `POST /v1/tokens` 换票**。
 * 每次调用都会打一条 warn 日志提醒这件事。
 *
 * 一致性向量：`im-rtc-server/docs/conformance/debug_token.json`（tests/DebugTokenTest.cpp 读它）。
 */

/** DebugTokenParams 是签票入参。`ttlSec` 缺省 / 0 = 12h，钳到 [60, 2592000]（30 天）。 */
struct DebugTokenParams {
  std::string appId;
  /** 调试密钥 id，必须以 `dbg-` 开头（把生产密钥误填进来会被挡在客户端之外）。 */
  std::string keyId;
  std::string secret;
  std::string uid;
  /** 可选；非空时写进 `did` 声明。 */
  std::string deviceId;
  std::int64_t ttlSec = 0;
  /** 可注入的「现在」（Unix 秒）；0 = 取系统时钟。测试用。 */
  std::int64_t nowUnix = 0;
};

constexpr std::int64_t kDebugTokenDefaultTtlSec = 12 * 3600;
constexpr std::int64_t kDebugTokenMinTtlSec = 60;
constexpr std::int64_t kDebugTokenMaxTtlSec = 30 * 24 * 3600;

/** clampDebugTokenTtl：0 / 负数 = 12h，其余钳到 [60, 2592000]（30 天）。 */
std::int64_t clampDebugTokenTtl(std::int64_t ttlSec);

/**
 * signDebugToken 生成 HS256 JWT。入参不合法抛 `RtcError(BadParams, 详情)`。
 * 仅调试用，见上。
 */
std::string signDebugToken(const DebugTokenParams& params);

/** hmacSha256 是标准 HMAC-SHA256（RFC 2104 / FIPS 180-4）。 */
std::array<std::uint8_t, 32> hmacSha256(const std::string& key, const std::string& message);

/** base64UrlEncode 是 RFC 4648 §5 的无填充 base64url。 */
std::string base64UrlEncode(const std::uint8_t* data, std::size_t size);
std::string base64UrlEncode(const std::string& data);

}  // namespace imrtc
