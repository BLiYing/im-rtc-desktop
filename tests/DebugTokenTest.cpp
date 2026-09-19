#include <array>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "Vectors.h"
#include "imrtc/DebugToken.h"
#include "imrtc/Errors.h"
#include "imrtc/Json.h"
#include "imrtc/Log.h"

using imrtc::Json;

namespace {

std::string text(const Json& value, const std::string& key) {
  const Json* found = value.find(key);
  return found != nullptr && found->isString() ? found->asString() : std::string();
}

std::int64_t number(const Json& value, const std::string& key) {
  const Json* found = value.find(key);
  return found != nullptr && found->isInt() ? found->asInt() : 0;
}

std::string toHex(const std::array<std::uint8_t, 32>& bytes) {
  static const char kDigits[] = "0123456789abcdef";
  std::string out;
  for (std::uint8_t byte : bytes) {
    out += kDigits[byte >> 4];
    out += kDigits[byte & 15];
  }
  return out;
}

std::string base64UrlDecode(const std::string& text) {
  std::string out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (char c : text) {
    int value = -1;
    if (c >= 'A' && c <= 'Z') value = c - 'A';
    else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
    else if (c >= '0' && c <= '9') value = c - '0' + 52;
    else if (c == '-') value = 62;
    else if (c == '_') value = 63;
    CHECK_TRUE(value >= 0, std::string("base64url 里出现非法字符: ") + c);
    buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buffer >> bits) & 0xff);
    }
  }
  return out;
}

std::vector<std::string> splitDots(const std::string& token) {
  std::vector<std::string> parts(1);
  for (char c : token) {
    if (c == '.') parts.emplace_back();
    else parts.back() += c;
  }
  return parts;
}

std::string macOf(const std::string& secret, const std::string& message) {
  const std::array<std::uint8_t, 32> mac = imrtc::hmacSha256(secret, message);
  return imrtc::base64UrlEncode(mac.data(), mac.size());
}

imrtc::DebugTokenParams paramsFrom(const Json& testCase) {
  const Json& input = *testCase.find("input");
  imrtc::DebugTokenParams params;
  params.appId = text(input, "app_id");
  params.uid = text(input, "uid");
  params.deviceId = text(input, "device_id");
  params.ttlSec = number(input, "ttl_sec");
  params.keyId = testCase.contains("key_id") ? text(testCase, "key_id") : "dbg-1";
  params.secret = testCase.contains("secret") ? text(testCase, "secret") : "0123456789abcdef0123456789abcdef";
  params.nowUnix = 1790000000;
  return params;
}

}  // namespace

IMRTC_TEST(debugTokenHmacStandardVectors, "debug_token —— HMAC-SHA256 对 RFC 4231 标准向量（含长密钥、多块消息）") {
  CHECK_EQ(toHex(imrtc::hmacSha256(std::string(20, '\x0b'), "Hi There")),
           std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), "RFC4231 case 1");
  CHECK_EQ(toHex(imrtc::hmacSha256("Jefe", "what do ya want for nothing?")),
           std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"), "RFC4231 case 2");
  CHECK_EQ(toHex(imrtc::hmacSha256(std::string(131, '\xaa'),
                                   "Test Using Larger Than Block-Size Key - Hash Key First")),
           std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"), "RFC4231 case 6（长密钥）");
  CHECK_EQ(imrtc::base64UrlEncode(std::string("\xfb\xff\xfe")), std::string("-__-"), "url 字母表");
  CHECK_EQ(imrtc::base64UrlEncode(std::string("a")), std::string("YQ"), "无填充（余 1 字节）");
  CHECK_EQ(imrtc::base64UrlEncode(std::string("ab")), std::string("YWI"), "无填充（余 2 字节）");
}

IMRTC_TEST(debugTokenHmacVector, "debug_token.json hmac_cases —— HMAC-SHA256 + base64url 无填充") {
  const Json vector = imtest::loadVector("debug_token.json");
  const Json* cases = vector.find("hmac_cases");
  CHECK_TRUE(cases != nullptr && cases->size() > 0, "hmac_cases 应当非空");
  for (const Json& testCase : cases->items()) {
    CHECK_EQ(macOf(text(testCase, "secret"), text(testCase, "signing_input")), text(testCase, "expect_signature"),
             "hmac " + text(testCase, "name"));
  }
}

IMRTC_TEST(debugTokenSignVector, "debug_token.json sign_cases —— 解析后比对 header / claims，并用 secret 验签") {
  const Json vector = imtest::loadVector("debug_token.json");
  const Json* cases = vector.find("sign_cases");
  CHECK_TRUE(cases != nullptr && cases->size() > 0, "sign_cases 应当非空");
  for (const Json& testCase : cases->items()) {
    const std::string label = "sign " + text(testCase, "name");
    imrtc::DebugTokenParams params = paramsFrom(testCase);
    params.nowUnix = number(testCase, "now_unix");
    const std::string token = imrtc::signDebugToken(params);
    const std::vector<std::string> parts = splitDots(token);
    CHECK_EQ(parts.size(), std::size_t{3}, label + " 三段");
    CHECK_TRUE(token.find('=') == std::string::npos, label + " 无填充");

    const Json header = Json::parse(base64UrlDecode(parts[0]));
    const Json claims = Json::parse(base64UrlDecode(parts[1]));
    // 子集比对 + 键数相等 = 全等（不比键序）。
    imtest::expectSubset(header, *testCase.find("expect_header"), label + ".header");
    imtest::expectSubset(claims, *testCase.find("expect_claims"), label + ".claims");
    CHECK_EQ(header.size(), testCase.find("expect_header")->size(), label + " header 不许多键");
    CHECK_EQ(claims.size(), testCase.find("expect_claims")->size(), label + " claims 不许多键");
    CHECK_EQ(parts[2], macOf(params.secret, parts[0] + "." + parts[1]), label + " 签名可用 secret 验过");
  }
}

IMRTC_TEST(debugTokenRejectVector, "debug_token.json reject_cases —— 非法入参必须拒绝（BadParams）") {
  const Json vector = imtest::loadVector("debug_token.json");
  const Json* cases = vector.find("reject_cases");
  CHECK_TRUE(cases != nullptr && cases->size() > 0, "reject_cases 应当非空");
  for (const Json& testCase : cases->items()) {
    const std::string label = "reject " + text(testCase, "name");
    bool rejected = false;
    try {
      imrtc::signDebugToken(paramsFrom(testCase));
    } catch (const imrtc::RtcError& error) {
      rejected = true;
      CHECK_EQ(error.code(), imrtc::codeValue(imrtc::ErrorCode::BadParams), label + " 的错误码");
    }
    CHECK_TRUE(rejected, label + " 应当被拒");
  }
}

IMRTC_TEST(debugTokenEdges, "debug_token —— 系统时钟兜底、uid 恰好 64 字节可过、警告日志已打且不含 secret") {
  imrtc::DebugTokenParams params;
  params.appId = "10000001";
  params.keyId = "dbg-7";
  params.secret = "s3cret-value";
  params.uid = std::string(64, 'x');
  std::vector<std::string> warnings;
  const imrtc::LogLevel savedLevel = imrtc::logLevel();
  imrtc::setLogLevel(imrtc::LogLevel::Info);
  imrtc::setLogSink([&](imrtc::LogLevel level, const std::string& message, const imrtc::LogFields& fields) {
    if (level != imrtc::LogLevel::Warn) return;
    std::string line = message;
    for (const auto& field : fields) line += " " + field.first + "=" + field.second;
    warnings.push_back(line);
  });
  const std::string token = imrtc::signDebugToken(params);  // nowUnix = 0：走系统时钟
  imrtc::setLogSink(nullptr);
  imrtc::setLogLevel(savedLevel);
  const Json claims = Json::parse(base64UrlDecode(splitDots(token)[1]));
  CHECK_TRUE(number(claims, "iat") > 1700000000, "iat 取系统时钟");
  CHECK_EQ(number(claims, "exp") - number(claims, "iat"), std::int64_t{43200}, "缺省 12h");
  CHECK_EQ(warnings.size(), std::size_t{1}, "恰好一条警告");
  CHECK_TRUE(warnings[0].find("DEBUG ONLY") != std::string::npos, "警告醒目");
  CHECK_TRUE(warnings[0].find("s3cret-value") == std::string::npos, "日志不含 secret");
}
