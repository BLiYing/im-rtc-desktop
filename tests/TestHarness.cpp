#include "TestHarness.h"
#include "imrtc/Log.h"

#include <cstdio>
#include <exception>
#include <utility>

namespace imtest {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

Registrar::Registrar(std::string name, std::function<void()> body) {
  registry().push_back(TestCase{std::move(name), std::move(body)});
}

std::string describe(const std::string& value) { return "\"" + value + "\""; }
std::string describe(const char* value) { return describe(std::string(value == nullptr ? "" : value)); }
std::string describe(bool value) { return value ? "true" : "false"; }
std::string describe(std::int64_t value) { return std::to_string(value); }
std::string describe(int value) { return std::to_string(value); }
std::string describe(std::size_t value) { return std::to_string(value); }
std::string describe(const imrtc::Json& value) { return value.dump(); }

void fail(const std::string& label, const std::string& detail) {
  throw AssertionFailure(label + " —— " + detail);
}

void checkTrue(bool condition, const std::string& label) {
  if (!condition) fail(label, "期望为真");
}

int runAll() {
  int failed = 0;
  for (const TestCase& testCase : registry()) {
    try {
      testCase.body();
    } catch (const AssertionFailure& failure) {
      ++failed;
      std::printf("  ✗ %s\n      %s\n", testCase.name.c_str(), failure.what());
      continue;
    } catch (const std::exception& error) {
      ++failed;
      std::printf("  ✗ %s\n      抛出了未预期的异常：%s\n", testCase.name.c_str(), error.what());
      continue;
    }
    std::printf("  ✓ %s\n", testCase.name.c_str());
  }

  const std::size_t total = registry().size();
  if (failed == 0) {
    std::printf("\n结果：✓ %zu 个用例全绿。\n", total);
    return 0;
  }
  std::printf("\n结果：✗ %d / %zu 个用例失败。\n", failed, total);
  return 1;
}

}  // namespace imtest

int main() {
  std::printf("== im-rtc-desktop 测试 ==\n");
  /*
    把日志压到 error：内置那一路写 stderr，而测试报告也在终端上——
    默认的 info 会往里掺 80 多行，真正要看的失败信息就被冲走了。

    **不是关掉**：要验日志本身的用例自己把级别调回来（见 LogTest.cpp），
    关掉的话那些用例就只能验一个假的。
  */
  imrtc::setLogLevel(imrtc::LogLevel::Error);
  return imtest::runAll();
}
