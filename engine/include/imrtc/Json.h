#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace imrtc {

/**
 * Json 是信令帧的通用值类型。
 *
 * 为什么自己写而不引第三方：协议 §2.4 的七条编码硬规则里有三条要在**解析层**判
 * （没有浮点、整数不超过 2^53-1、null 一律拒），而「1e3 是整数、15e-1 不是」这条
 * 必须按**值**判定——四端已经因为按字面量判 vs 按值判踩过一次坑
 * （见 conformance/README.md）。自己写这一层比给第三方库打补丁便宜，
 * 也符合「不引重型第三方框架」（CONVENTIONS §12）。
 *
 * Double 这个类型在协议里是**非法**的，但解析器必须表达得出来——否则没法把
 * 「收到了浮点」这件事报告给 discipline 去拒绝。
 *
 * 对象成员用有序 vector 而不是 map：键顺序在 JSON 里无意义，但**输出稳定**
 * 对日志与测试断言很有价值，而帧只有个位数字段，线性查找比红黑树快。
 */
class Json {
public:
  enum class Type { Null, Bool, Int, Double, String, Array, Object };

  using Array = std::vector<Json>;
  using Member = std::pair<std::string, Json>;
  using Object = std::vector<Member>;

  Json() = default;

  static Json makeNull();
  static Json make(bool value);
  static Json make(std::int64_t value);
  static Json makeDouble(double value);
  static Json make(std::string value);
  static Json make(const char* value);
  static Json makeArray(Array items = {});
  static Json makeObject(Object members = {});

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isInt() const { return type_ == Type::Int; }
  bool isDouble() const { return type_ == Type::Double; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  /** 取值。类型不符时返回该类型的零值，**不抛异常**——调用方先用 isXxx() 判。 */
  bool asBool() const;
  std::int64_t asInt() const;
  double asDouble() const;
  const std::string& asString() const;

  const Array& items() const;
  const Object& members() const;
  Array& items();
  Object& members();

  /** find 返回对象成员的指针；不是对象或没这个键时返回 nullptr。 */
  const Json* find(const std::string& key) const;
  bool contains(const std::string& key) const { return find(key) != nullptr; }

  /** set 插入或覆盖一个对象成员，保持插入顺序。自动把自己变成对象。 */
  void set(std::string key, Json value);
  /** push 往数组尾部追加。自动把自己变成数组。 */
  void push(Json value);

  std::size_t size() const;

  bool operator==(const Json& other) const;
  bool operator!=(const Json& other) const { return !(*this == other); }

  /** dump 序列化成紧凑 JSON 文本。**给日志与错误信息用**，不做美化。 */
  std::string dump() const;

  /**
   * parse 解析一段 JSON 文本。
   *
   * 语法错误抛 `RtcError(bad_envelope)`。**它只管语法**——「协议里不许有浮点」
   * 那类语义规则在 checkDiscipline()，两者刻意分开：解析器要能表达非法值，
   * 才谈得上拒绝它。
   */
  static Json parse(const std::string& text);

  /** typeName 给出人类可读的类型名，用于错误信息。 */
  static const char* typeName(Type type);
  const char* typeName() const { return typeName(type_); }

private:
  Type type_ = Type::Null;
  bool bool_ = false;
  std::int64_t int_ = 0;
  double double_ = 0.0;
  std::string string_;
  Array array_;
  Object object_;
};

}  // namespace imrtc
