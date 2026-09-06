#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "Vectors.h"
#include "imrtc/Envelope.h"
#include "imrtc/Errors.h"
#include "imrtc/Reasons.h"
#include "imrtc/Registry.h"

using imrtc::Json;

namespace {

/** ParsedFrame 是两步解析的结果：先信封（含编码硬规则），再帧级 data。 */
struct ParsedFrame {
  std::string type;
  std::string reqId;
  std::int64_t ts = 0;
  bool hasFields = false;
  Json data;
};

ParsedFrame parseFrame(const std::string& raw) {
  const imrtc::Envelope envelope = imrtc::decodeEnvelope(raw);
  ParsedFrame parsed;
  parsed.type = envelope.type;
  parsed.reqId = envelope.reqId;
  parsed.ts = envelope.ts;

  const imrtc::FrameFields* fields = imrtc::lookupFrame(envelope.type);
  if (fields == nullptr) return parsed;

  parsed.hasFields = true;
  // 解出来就是线路形状（snake_case + 默认值），向量比的正是这个。
  parsed.data = imrtc::decodeFields(*fields, envelope.data);
  return parsed;
}

std::string text(const Json& value, const std::string& key) {
  const Json* found = value.find(key);
  return found != nullptr && found->isString() ? found->asString() : std::string();
}

bool flag(const Json& value, const std::string& key) {
  const Json* found = value.find(key);
  return found != nullptr && found->isBool() && found->asBool();
}

/** runNegativeCase 断言解析必定失败，且错误码的机读名与向量一致。 */
void runNegativeCase(const Json& testCase, const std::string& label) {
  const std::string wantError = text(*testCase.find("expect"), "error");
  try {
    parseFrame(text(testCase, "input"));
  } catch (const imrtc::RtcError& error) {
    CHECK_EQ(error.name(), wantError, label + " 的错误码");
    return;
  }
  imtest::fail(label, "本该失败，却解析成功了");
}

void runEnvelopeCase(const Json& testCase) {
  const std::string name = text(testCase, "name");
  const std::string label = "envelope.json/" + name;
  const Json& expect = *testCase.find("expect");

  if (!flag(expect, "ok")) {
    runNegativeCase(testCase, label);
    return;
  }

  // 未知 type：客户端静默忽略，服务端回 1002。两侧都不该在信封阶段失败。
  if (text(expect, "client_action") == "ignore" || text(expect, "server_action") == "error") {
    const imrtc::Envelope envelope = imrtc::decodeEnvelope(text(testCase, "input"));
    CHECK_EQ(envelope.type, text(expect, "type"), label + " 的 type");
    CHECK_TRUE(imrtc::lookupFrame(envelope.type) == nullptr, label + " 本该是未注册的帧");
    return;
  }

  const ParsedFrame parsed = parseFrame(text(testCase, "input"));
  if (expect.contains("type")) CHECK_EQ(parsed.type, text(expect, "type"), label + " 的 type");
  if (expect.contains("req_id")) CHECK_EQ(parsed.reqId, text(expect, "req_id"), label + " 的 req_id");
  if (expect.contains("ts")) {
    CHECK_EQ(parsed.ts, expect.find("ts")->asInt(), label + " 的 ts");
  }
  if (expect.contains("data")) {
    CHECK_TRUE(parsed.hasFields, label + " 本该有已注册的字段声明");
    imtest::expectSubset(parsed.data, *expect.find("data"), "data");
  }
}

void runDefaultCase(const Json& testCase) {
  const std::string label = "envelope.json/default:" + text(testCase, "name");
  Json frame = Json::makeObject();
  frame.set("type", Json::make(text(testCase, "type")));
  frame.set("req_id", Json::make("c-1"));
  frame.set("ts", Json::make(std::int64_t{1756876800123}));
  frame.set("data", Json::parse(text(testCase, "input_data")));

  const ParsedFrame parsed = parseFrame(frame.dump());
  CHECK_TRUE(parsed.hasFields, label + " 本该有已注册的字段声明");
  imtest::expectSubset(parsed.data, *testCase.find("expect_data"), "data");
}

}  // namespace

IMRTC_TEST(envelopeVector, "envelope.json —— 信封解析与默认值填充") {
  const Json vector = imtest::loadVector("envelope.json");
  CHECK_EQ(text(vector, "kind"), std::string("envelope"), "向量类型");
  CHECK_EQ(vector.find("version")->asInt(), std::int64_t{1}, "向量版本");

  const Json* cases = vector.find("cases");
  CHECK_TRUE(cases != nullptr && !cases->items().empty(), "cases 不能为空");
  for (const Json& testCase : cases->items()) runEnvelopeCase(testCase);

  const Json* defaults = vector.find("default_cases");
  CHECK_TRUE(defaults != nullptr && !defaults->items().empty(), "default_cases 不能为空");
  for (const Json& testCase : defaults->items()) runDefaultCase(testCase);
}

IMRTC_TEST(errorCodesVector, "error_codes.json —— 错误码全表逐条相等") {
  const Json vector = imtest::loadVector("error_codes.json");

  std::map<std::int64_t, imrtc::ErrorDefinition> want;
  for (const char* group : {"wire", "local"}) {
    const Json* entries = vector.find(group);
    CHECK_TRUE(entries != nullptr, std::string("向量缺少 ") + group + " 段");
    for (const Json& entry : entries->items()) {
      const std::int64_t code = entry.find("code")->asInt();
      want[code] = imrtc::ErrorDefinition{static_cast<std::int32_t>(code), text(entry, "name"),
                                          text(entry, "msg"), flag(entry, "retryable"),
                                          text(entry, "group") == "local"};
    }
  }

  const std::vector<imrtc::ErrorDefinition>& got = imrtc::errorDefinitions();
  CHECK_EQ(got.size(), want.size(), "错误码条数");
  for (const imrtc::ErrorDefinition& def : got) {
    const std::string label = "错误码 " + std::to_string(def.code);
    const auto expected = want.find(def.code);
    CHECK_TRUE(expected != want.end(), label + " 不在向量里");
    CHECK_EQ(def.name, expected->second.name, label + " 的 name");
    CHECK_EQ(def.msg, expected->second.msg, label + " 的 msg");
    CHECK_EQ(def.retryable, expected->second.retryable, label + " 的 retryable");
    CHECK_EQ(def.local, expected->second.local, label + " 的 local");
  }
}

IMRTC_TEST(errorCodesLocalRange, "error_codes —— 2xxx 段与 local 标记一致（本地码永不上线路）") {
  for (const imrtc::ErrorDefinition& def : imrtc::errorDefinitions()) {
    const bool inLocalRange = def.code >= 2000 && def.code < 3000;
    CHECK_EQ(def.local, inLocalRange, std::to_string(def.code) + "(" + def.name + ")");
  }
}

IMRTC_TEST(reasonsVector, "reasons.json —— reason 枚举、兜底与群主导优先级") {
  const Json vector = imtest::loadVector("reasons.json");

  std::vector<std::string> wantReasons;
  for (const Json& entry : vector.find("reasons")->items()) {
    wantReasons.push_back(text(entry, "value"));
  }
  std::vector<std::string> gotReasons = imrtc::reasonValues();
  std::sort(wantReasons.begin(), wantReasons.end());
  std::sort(gotReasons.begin(), gotReasons.end());
  CHECK_EQ(gotReasons, wantReasons, "reason 枚举取值");

  CHECK_EQ(text(vector, "unknown_fallback"), std::string("error"), "未知值兜底");
  CHECK_EQ(imrtc::normalizeReason(Json::make("supernova")), std::string("error"), "陌生 reason");
  CHECK_EQ(imrtc::normalizeReason(Json::makeNull()), std::string("error"), "缺席的 reason");
  CHECK_EQ(imrtc::normalizeReason(Json::make(std::int64_t{42})), std::string("error"), "数字 reason");

  std::vector<std::string> wantPriority;
  for (const Json& entry : vector.find("group_dominant_priority")->items()) {
    wantPriority.push_back(entry.asString());
  }
  CHECK_EQ(imrtc::groupDominantPriority(), wantPriority, "群主导优先级");

  for (const Json& testCase : vector.find("group_dominant_cases")->items()) {
    std::vector<std::string> outcomes;
    for (const Json& entry : testCase.find("member_outcomes")->items()) {
      outcomes.push_back(entry.asString());
    }
    CHECK_EQ(imrtc::dominantReason(outcomes), text(testCase, "expect"),
             "dominant: " + text(testCase, "name"));
  }

  for (const Json& testCase : vector.find("duration_cases")->items()) {
    const std::int64_t got = imrtc::callDurationSec(testCase.find("connected_at_ms")->asInt(),
                                                    testCase.find("ended_at_ms")->asInt());
    CHECK_EQ(got, testCase.find("expect_duration_sec")->asInt(),
             "duration: " + text(testCase, "name"));
  }
}

IMRTC_TEST(registryShape, "registry —— 请求帧的 .ok 一律可查，留位帧不当作已实现") {
  for (const std::string& type : imrtc::requestTypes()) {
    CHECK_TRUE(imrtc::lookupFrame(type) != nullptr, type + " 应当已注册");
    CHECK_TRUE(imrtc::lookupFrame(imrtc::okType(type)) != nullptr, type + ".ok 应当可查");
  }
  for (const std::string& type : imrtc::reservedTypes()) {
    CHECK_TRUE(imrtc::lookupFrame(type) == nullptr, type + " 是留位帧，不该有字段声明");
    CHECK_TRUE(imrtc::isReservedType(type), type + " 应当被认作留位帧");
  }
}
