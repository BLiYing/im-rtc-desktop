#include <cstdlib>
#include <string>

#include "imrtc/Enums.h"
#include "imrtc/Envelope.h"
#include "imrtc/Errors.h"
#include "imrtc/Json.h"

namespace imrtc {
namespace {

/**
 * RTC_PROTOCOL.md §2.4「七条编码硬规则」里能**脱离帧定义**判定的那几条。
 *
 * 它们是「五端都能实现」这句话的落点：不放浮点、不放 null、数组同构、
 * 嵌套不超过两层，于是 C++ 不需要 optional<optional<T>>、
 * TS 不需要区分 undefined 与 null、Swift 不需要 Optional<Optional>。
 *
 * 剩下两条（字段类型恒定、枚举封闭带兜底）没法脱离帧定义判，由 FieldSpec 负责。
 */
void walk(const Json& value, int depth, const std::string& path);

[[noreturn]] void badParams(const std::string& reason) {
  throw RtcError(ErrorCode::BadParams, reason);
}

/**
 * checkNumber 落实规则 1（没有浮点数）与规则 7（整数不超过 2^53-1）。
 *
 * 判定按**值**而不是按字面量：`1e3` 是整数 1000，合法；`4.5` 不是，非法。
 * 五端必须用同一套判定，否则一端发得出去、另一端收不下来
 * （这条曾经真的漂过，见 conformance/README.md）。
 */
void checkNumber(const Json& value, const std::string& path) {
  if (value.isDouble()) {
    badParams(path + ": 协议里没有浮点数，得到 " + value.dump() +
              "；音量/质量/时长/码率一律用整数");
  }
  const std::int64_t number = value.asInt();
  // **不要先取绝对值再比**：`-number` 在 number == INT64_MIN 时是有符号溢出（UB），
  // 而且在常见的补码实现上会折回 INT64_MIN——那是个负数，于是 `> 上限` 恒为假，
  // 一帧 `-9223372036854775808` 反而**绕过**了这道 2^53 的闸。直接两头比就没有这个洞。
  if (number > kMaxSafeProtocolInt || number < -kMaxSafeProtocolInt) {
    badParams(path + ": 整数 " + std::to_string(number) + " 超出 ±(2^53-1)，会静默丢精度");
  }
}

void walkObject(const Json& value, int depth, const std::string& path) {
  if (depth > kMaxObjectDepth) {
    badParams(path + ": 对象嵌套 " + std::to_string(depth) + " 层 > 上限 " +
              std::to_string(kMaxObjectDepth) + "；要塞任意结构请用 user_data（opaque 字符串）");
  }
  for (const Json::Member& member : value.members()) {
    walk(member.second, depth + 1, path + "." + member.first);
  }
}

/**
 * walkArray 除了递归检查元素，还要确认数组是**同构**的（规则 4）。
 * 异构数组在 TS/Swift 里勉强能表达，在 C++ 里就得上 variant——所以协议直接禁掉。
 */
void walkArray(const Json& value, int depth, const std::string& path) {
  const char* firstKind = nullptr;
  std::size_t index = 0;
  for (const Json& element : value.items()) {
    const char* kind = element.typeName();
    if (index == 0) {
      firstKind = kind;
    } else if (std::string(kind) != firstKind) {
      badParams(path + ": 数组必须同构，第 0 个是 " + firstKind + "、第 " +
                std::to_string(index) + " 个是 " + kind + "；要成对请用对象数组");
    }
    walk(element, depth, path + "[" + std::to_string(index) + "]");
    ++index;
  }
}

void walk(const Json& value, int depth, const std::string& path) {
  switch (value.type()) {
    case Json::Type::Null:
      // 规则 2：协议里任何位置都不许出现 null。可选字段的表达方式是**省略**。
      throw RtcError(ErrorCode::BadEnvelope, path + ": 出现 null；可选字段请省略，不要写 null");
    case Json::Type::Int:
    case Json::Type::Double:
      checkNumber(value, path);
      return;
    case Json::Type::String:
    case Json::Type::Bool:
      return;
    case Json::Type::Array:
      walkArray(value, depth, path);
      return;
    case Json::Type::Object:
      walkObject(value, depth, path);
      return;
  }
}

}  // namespace

void checkDiscipline(const Json& data) { walk(data, 1, "data"); }

}  // namespace imrtc
