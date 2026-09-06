#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "imrtc/Json.h"

/**
 * 一个 120 行的测试框架。
 *
 * 为什么不引 Catch2 / GoogleTest：本期整个工程**零第三方依赖**，
 * 一份 CMake + 一个编译器就能跑。断言只需要「相等」「为真」「该抛异常」三种，
 * 为它们拉一个几万行的框架进来不划算（CONVENTIONS §12）。
 * 等媒体层进来、真需要 fixture 与参数化时再换，那时换掉的成本也只有这一个文件。
 */
namespace imtest {

/** AssertionFailure 是断言失败。用异常是为了让一条用例失败后**继续跑其余用例**。 */
class AssertionFailure : public std::runtime_error {
public:
  explicit AssertionFailure(const std::string& message) : std::runtime_error(message) {}
};

struct TestCase {
  std::string name;
  std::function<void()> body;
};

std::vector<TestCase>& registry();

/** Registrar 在静态初始化期把用例挂进注册表。 */
struct Registrar {
  Registrar(std::string name, std::function<void()> body);
};

/** runAll 跑完全部用例，返回进程退出码（0 = 全绿）。 */
int runAll();

/** describe 把值渲染成人类可读的文本，给失败信息用。 */
std::string describe(const std::string& value);
std::string describe(const char* value);
std::string describe(bool value);
std::string describe(std::int64_t value);
std::string describe(int value);
std::string describe(std::size_t value);
std::string describe(const imrtc::Json& value);

template <typename T>
std::string describe(const std::vector<T>& values) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) out << ", ";
    out << describe(values[i]);
  }
  out << "]";
  return out.str();
}

[[noreturn]] void fail(const std::string& label, const std::string& detail);

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, const std::string& label) {
  if (!(actual == expected)) {
    fail(label, "期望 " + describe(expected) + "，实得 " + describe(actual));
  }
}

void checkTrue(bool condition, const std::string& label);

}  // namespace imtest

/** IMRTC_TEST 声明一个用例。名字里带上向量文件名，失败时一眼看出是哪一份。 */
#define IMRTC_TEST(unique_name, display_name)                        \
  static void unique_name();                                         \
  static const ::imtest::Registrar unique_name##_registrar(display_name, unique_name); \
  static void unique_name()

/** CHECK_EQ / CHECK_TRUE 是仅有的两个断言。 */
#define CHECK_EQ(actual, expected, label) ::imtest::checkEqual((actual), (expected), (label))
#define CHECK_TRUE(condition, label) ::imtest::checkTrue((condition), (label))
