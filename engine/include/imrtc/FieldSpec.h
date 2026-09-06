#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/Enums.h"
#include "imrtc/Json.h"

namespace imrtc {

/**
 * 帧字段的**声明式定义**：一处声明同时产出运行时校验与默认值填充。
 *
 * 这套声明负责 RTC_PROTOCOL.md §2.4 里**必须结合帧定义**才能判的两条：
 * 规则 3（字段类型恒定）与规则 6（枚举封闭且带兜底）。
 * 与帧无关的四条在 Discipline.cpp。
 *
 * # 与 TS 端的一个刻意差异
 *
 * TS 端解码成 camelCase 对象、比对时再编回 snake_case。C++ 这边**直接解成
 * 线路形状**（snake_case 的 Json 对象），少一层映射也少一处漂移点：
 * 状态机读的是 `data["call_id"]`，一致性向量比的也是 `call_id`。
 */
enum class FieldKind { String, Int, Bool, Enum, StringArray, EnumArray, ObjectArray, Object };

struct FieldSpec;

/** FrameFields 是一帧的全部字段声明，顺序即输出顺序。 */
using FrameFields = std::vector<FieldSpec>;

/** FieldSpec 是一个字段的契约。`wire` 是线路上的 snake_case 名。 */
struct FieldSpec {
  FieldKind kind = FieldKind::String;
  std::string wire;

  std::string defaultString;
  std::int64_t defaultInt = 0;
  bool defaultBool = false;

  bool hasMin = false;
  std::int64_t minValue = 0;
  bool hasMax = false;
  std::int64_t maxValue = 0;
  /**
   * 越界时的处理方式。协议对不同字段的规定不一样，不能一刀切：
   * `timeout_sec` 越界**钳到边界**（§2.6）；质量 `level` 越界**折成 0 = unknown**
   * （§2.4 规则 6 的兜底表）。给了 outOfRange 就折成它，没给就钳到边界。
   */
  bool hasOutOfRange = false;
  std::int64_t outOfRange = 0;

  /** 枚举的取值集合与兜底值。收到集合外的值折成 fallback，**禁止崩溃、禁止透传**。 */
  const EnumValues* values = nullptr;
  std::string fallback;

  /** 嵌套对象/对象数组的字段声明。 */
  const FrameFields* fields = nullptr;
};

/** 下面这组构造函数让帧定义读起来像协议表本身。 */
FieldSpec stringField(std::string wire, std::string defaultValue = "");
FieldSpec intField(std::string wire, std::int64_t defaultValue = 0);
FieldSpec intRangeField(std::string wire, std::int64_t defaultValue, std::int64_t minValue,
                        std::int64_t maxValue);
FieldSpec intFoldField(std::string wire, std::int64_t minValue, std::int64_t maxValue,
                       std::int64_t outOfRange);
FieldSpec boolField(std::string wire, bool defaultValue = false);
FieldSpec enumField(std::string wire, const EnumValues& values, std::string fallback);
FieldSpec enumField(std::string wire, const EnumValues& values, std::string fallback,
                    std::string defaultValue);
FieldSpec stringArrayField(std::string wire);
FieldSpec enumArrayField(std::string wire, const EnumValues& values, std::string fallback);
FieldSpec objectArrayField(std::string wire, const FrameFields& fields);
FieldSpec objectField(std::string wire, const FrameFields& fields);

/**
 * decodeFields 把线路上的 data 解成**带默认值的线路形状对象**。
 *
 * 「可选字段用省略表达，接收方按默认值填」（§2.4 规则 2）就落在这里：
 * 字段缺席 → 取声明的默认值；出现了 → 校验类型、归一化枚举、钳制数值。
 * 未声明的字段被丢弃（前向兼容：服务端可能比客户端新）。
 *
 * 类型不符抛 RtcError(bad_params)。
 */
Json decodeFields(const FrameFields& fields, const Json& raw, const std::string& path = "data");

/**
 * newFrameData 返回某帧**已填好协议默认值**的数据对象。
 *
 * # 发送方必须用它
 *
 * 「省略即取默认值」只对**真的省略**成立。而各端的序列化器都会把零值显式写上线路：
 * 直接发一个只填了 room_id 的 `room.join`，线路上是 `auto_subscribe:false`，
 * 于是自动订阅静默关掉，人进了房却收不到任何流（五端都踩过，§2.4 规则 2 的注）。
 * **发送侧一律从这里起手再改字段。**
 */
Json newFrameData(const FrameFields& fields);

}  // namespace imrtc
