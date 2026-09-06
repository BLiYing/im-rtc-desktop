#include "Vectors.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "TestHarness.h"

namespace imtest {
namespace {

/** kSiblingHint 是找不到向量时给出的两条出路。 */
const char* const kSiblingHint =
    "找不到一致性向量。两条出路：\n"
    "  1) 把 im-rtc-server 克隆到本仓同级目录（默认布局）；\n"
    "  2) 设 RTC_CONFORMANCE_DIR 指向 im-rtc-server/docs/conformance。\n"
    "向量是五仓共用的单一真相源，**不要拷贝一份到本仓**。";

bool fileExists(const std::string& path) {
  std::ifstream probe(path);
  return probe.good();
}

/** resolveDir 依次试环境变量、CMake 注入的默认路径。 */
std::string resolveDir() {
  const char* fromEnv = std::getenv("RTC_CONFORMANCE_DIR");
  if (fromEnv != nullptr && fromEnv[0] != '\0') return fromEnv;
  return IMRTC_CONFORMANCE_DIR;
}

}  // namespace

imrtc::Json loadVector(const std::string& name) {
  const std::string path = resolveDir() + "/" + name;
  if (!fileExists(path)) {
    throw AssertionFailure(std::string(kSiblingHint) + "\n（试过：" + path + "）");
  }
  std::ifstream file(path);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return imrtc::Json::parse(buffer.str());
}

void expectSubset(const imrtc::Json& actual, const imrtc::Json& want, const std::string& path) {
  if (want.isArray()) {
    CHECK_TRUE(actual.isArray(), path + " 应当是数组");
    CHECK_EQ(actual.size(), want.size(), path + " 长度（实得 " + actual.dump() + "）");
    for (std::size_t i = 0; i < want.items().size(); ++i) {
      expectSubset(actual.items()[i], want.items()[i], path + "[" + std::to_string(i) + "]");
    }
    return;
  }
  if (want.isObject()) {
    CHECK_TRUE(actual.isObject(), path + " 应当是对象");
    for (const imrtc::Json::Member& member : want.members()) {
      const imrtc::Json* got = actual.find(member.first);
      CHECK_TRUE(got != nullptr, path + "." + member.first + " 应当存在");
      expectSubset(*got, member.second, path + "." + member.first);
    }
    return;
  }
  CHECK_EQ(actual, want, path);
}

}  // namespace imtest
