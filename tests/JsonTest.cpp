#include <string>

#include "TestHarness.h"
#include "imrtc/Errors.h"
#include "imrtc/Json.h"

using imrtc::Json;

/**
 * JSON 层的单测。
 *
 * 这一层没有一致性向量兜底（向量测的是协议行为，不是解析器），而它又是整条链路的
 * 最底下一环——**数字判定错一个分支，五端就会漂**。所以这里逐条钉死。
 */
namespace {

/** parseFails 断言一段文本解析失败，并给出错误码的机读名。 */
void parseFails(const std::string& text, const std::string& label) {
  try {
    Json::parse(text);
  } catch (const imrtc::RtcError& error) {
    CHECK_EQ(error.name(), std::string("bad_envelope"), label + " 的错误码");
    return;
  }
  imtest::fail(label, "本该解析失败");
}

}  // namespace

IMRTC_TEST(jsonNumbers, "Json —— 数字按值判定（1e3 是整数，15e-1 不是）") {
  CHECK_TRUE(Json::parse("1e3").isInt(), "1e3 应当收成整数");
  CHECK_EQ(Json::parse("1e3").asInt(), std::int64_t{1000}, "1e3 的值");
  CHECK_TRUE(Json::parse("15e-1").isDouble(), "15e-1 不是整数");
  CHECK_TRUE(Json::parse("73.5").isDouble(), "73.5 不是整数");
  CHECK_EQ(Json::parse("-42").asInt(), std::int64_t{-42}, "负整数");
  CHECK_EQ(Json::parse("9007199254740991").asInt(), std::int64_t{9007199254740991},
           "2^53-1 仍是整数");
  // 超出 2^53-1 的照样解析成整数——**拒绝它是 checkDiscipline 的事**，
  // 解析器要能表达非法值，否则没法报告「哪里越界了」。
  CHECK_TRUE(Json::parse("9007199254740992").isInt(), "2^53 仍解析为整数");
}

IMRTC_TEST(jsonSyntax, "Json —— 语法错误一律 bad_envelope") {
  parseFails("{\"type\":\"sys.ping\",", "截断的对象");
  parseFails("", "空文本");
  parseFails("{\"a\":}", "缺少值");
  parseFails("{\"a\":1}}", "尾部多余内容");
  parseFails("[1,2", "未闭合的数组");
  parseFails("\"未闭合", "未闭合的字符串");
}

IMRTC_TEST(jsonStrings, "Json —— 转义与 UTF-8 往返") {
  CHECK_EQ(Json::parse("\"a\\nb\"").asString(), std::string("a\nb"), "换行转义");
  CHECK_EQ(Json::parse("\"\\u4e2d\\u6587\"").asString(), std::string("中文"), "\\u 转义");
  // 代理对：U+1F600。
  CHECK_EQ(Json::parse("\"\\ud83d\\ude00\"").asString().size(), std::size_t{4}, "代理对长度");

  const Json value = Json::make(std::string("引号\"与\\反斜杠\n换行"));
  CHECK_EQ(Json::parse(value.dump()).asString(), value.asString(), "转义往返");
}

IMRTC_TEST(jsonObjects, "Json —— 对象保持插入序、重复键取最后一个、比较与顺序无关") {
  Json object = Json::makeObject();
  object.set("b", Json::make(std::int64_t{2}));
  object.set("a", Json::make(std::int64_t{1}));
  CHECK_EQ(object.dump(), std::string("{\"b\":2,\"a\":1}"), "插入序");

  object.set("b", Json::make(std::int64_t{9}));
  CHECK_EQ(object.dump(), std::string("{\"b\":9,\"a\":1}"), "覆盖不改顺序");

  CHECK_EQ(Json::parse("{\"a\":1,\"a\":2}").find("a")->asInt(), std::int64_t{2}, "重复键取最后");

  // 键顺序在 JSON 里无意义，比较必须与它无关（协议 §2.4 补充）。
  CHECK_TRUE(Json::parse("{\"a\":1,\"b\":2}") == Json::parse("{\"b\":2,\"a\":1}"), "顺序无关的相等");
  CHECK_TRUE(Json::parse("{\"a\":1}") != Json::parse("{\"a\":1,\"b\":2}"), "少一个键就不相等");
}

IMRTC_TEST(jsonAccessors, "Json —— 取错类型返回零值而不是崩") {
  const Json text = Json::make(std::string("hello"));
  CHECK_EQ(text.asInt(), std::int64_t{0}, "字符串取整数");
  CHECK_EQ(text.size(), std::size_t{0}, "字符串的 size");
  CHECK_TRUE(text.find("any") == nullptr, "非对象的 find");
  CHECK_TRUE(Json::makeNull().items().empty(), "非数组的 items");
}
