#include "imrtc/DebugToken.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "imrtc/Errors.h"
#include "imrtc/Json.h"
#include "imrtc/Log.h"

namespace imrtc {
namespace {

constexpr std::size_t kMaxUidBytes = 64;
constexpr char kDebugKeyPrefix[] = "dbg-";
/** 服务端配了 issuer 就会核对，漏了整张票被拒（设计稿 §4）。 */
constexpr char kIssuer[] = "im-rtc-server";

[[noreturn]] void reject(const std::string& why) { throw RtcError(ErrorCode::BadParams, "debug token: " + why); }

bool hasWhitespace(const std::string& text) {
  return std::any_of(text.begin(), text.end(),
                     [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
}

void validate(const DebugTokenParams& params) {
  if (params.uid.empty()) reject("uid is empty");
  if (hasWhitespace(params.uid)) reject("uid contains whitespace");
  if (params.uid.size() > kMaxUidBytes) reject("uid longer than 64 bytes");
  if (params.appId.empty()) reject("app_id is empty");
  if (params.secret.empty()) reject("secret is empty");
  if (params.keyId.rfind(kDebugKeyPrefix, 0) != 0) reject("key_id must start with \"dbg-\"");
}

std::int64_t systemNowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

std::int64_t clampDebugTokenTtl(std::int64_t ttlSec) {
  if (ttlSec <= 0) return kDebugTokenDefaultTtlSec;
  return std::min(std::max(ttlSec, kDebugTokenMinTtlSec), kDebugTokenMaxTtlSec);
}

std::string signDebugToken(const DebugTokenParams& params) {
  // 先警告再校验：即使调用被拒，也要让人在日志里看见有人在用本地签票。secret 绝不进日志。
  log(LogLevel::Warn, "DEBUG ONLY: signing a login token locally with a debug key; switch to backend POST /v1/tokens before release",
      {{logfield::kUid, params.uid}, {"key_id", params.keyId}});
  validate(params);

  const std::int64_t now = params.nowUnix != 0 ? params.nowUnix : systemNowUnix();
  Json header = Json::makeObject();
  header.set("alg", Json::make("HS256"));
  header.set("typ", Json::make("JWT"));
  header.set("kid", Json::make(params.keyId));

  Json claims = Json::makeObject();
  claims.set("iss", Json::make(kIssuer));
  claims.set("sub", Json::make(params.uid));
  claims.set("aud", Json::make(params.appId));
  claims.set("exp", Json::make(now + clampDebugTokenTtl(params.ttlSec)));
  claims.set("iat", Json::make(now));
  claims.set("scope", Json::make("access"));
  if (!params.deviceId.empty()) claims.set("did", Json::make(params.deviceId));

  const std::string signingInput = base64UrlEncode(header.dump()) + "." + base64UrlEncode(claims.dump());
  const std::array<std::uint8_t, 32> mac = hmacSha256(params.secret, signingInput);
  return signingInput + "." + base64UrlEncode(mac.data(), mac.size());
}

}  // namespace imrtc
