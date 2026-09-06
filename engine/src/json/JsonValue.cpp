#include "imrtc/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace imrtc {
namespace {

const std::string kEmptyString;
const Json::Array kEmptyArray;
const Json::Object kEmptyObject;

/** appendEscaped 按 JSON 规则转义一个字符串。协议里字符串一律 UTF-8。 */
void appendEscaped(const std::string& value, std::string& out) {
  out.push_back('"');
  for (const char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
          out += buffer;
        } else {
          // UTF-8 的后续字节原样输出——协议不要求把非 ASCII 转成 \u 转义。
          out.push_back(raw);
        }
        break;
    }
  }
  out.push_back('"');
}

/** appendDouble 只在「解析到了非法浮点、要把它打进错误信息」时用得上。 */
void appendDouble(double value, std::string& out) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  out += buffer;
}

void dumpInto(const Json& value, std::string& out) {
  switch (value.type()) {
    case Json::Type::Null: out += "null"; break;
    case Json::Type::Bool: out += value.asBool() ? "true" : "false"; break;
    case Json::Type::Int: out += std::to_string(value.asInt()); break;
    case Json::Type::Double: appendDouble(value.asDouble(), out); break;
    case Json::Type::String: appendEscaped(value.asString(), out); break;
    case Json::Type::Array: {
      out.push_back('[');
      bool first = true;
      for (const Json& item : value.items()) {
        if (!first) out.push_back(',');
        first = false;
        dumpInto(item, out);
      }
      out.push_back(']');
      break;
    }
    case Json::Type::Object: {
      out.push_back('{');
      bool first = true;
      for (const Json::Member& member : value.members()) {
        if (!first) out.push_back(',');
        first = false;
        appendEscaped(member.first, out);
        out.push_back(':');
        dumpInto(member.second, out);
      }
      out.push_back('}');
      break;
    }
  }
}

}  // namespace

Json Json::makeNull() { return Json(); }

Json Json::make(bool value) {
  Json json;
  json.type_ = Type::Bool;
  json.bool_ = value;
  return json;
}

Json Json::make(std::int64_t value) {
  Json json;
  json.type_ = Type::Int;
  json.int_ = value;
  return json;
}

Json Json::makeDouble(double value) {
  Json json;
  json.type_ = Type::Double;
  json.double_ = value;
  return json;
}

Json Json::make(std::string value) {
  Json json;
  json.type_ = Type::String;
  json.string_ = std::move(value);
  return json;
}

Json Json::make(const char* value) { return make(std::string(value == nullptr ? "" : value)); }

Json Json::makeArray(Array items) {
  Json json;
  json.type_ = Type::Array;
  json.array_ = std::move(items);
  return json;
}

Json Json::makeObject(Object members) {
  Json json;
  json.type_ = Type::Object;
  json.object_ = std::move(members);
  return json;
}

bool Json::asBool() const { return type_ == Type::Bool && bool_; }

std::int64_t Json::asInt() const { return type_ == Type::Int ? int_ : 0; }

double Json::asDouble() const {
  if (type_ == Type::Double) return double_;
  if (type_ == Type::Int) return static_cast<double>(int_);
  return 0.0;
}

const std::string& Json::asString() const {
  return type_ == Type::String ? string_ : kEmptyString;
}

const Json::Array& Json::items() const { return type_ == Type::Array ? array_ : kEmptyArray; }

const Json::Object& Json::members() const {
  return type_ == Type::Object ? object_ : kEmptyObject;
}

Json::Array& Json::items() {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    array_.clear();
  }
  return array_;
}

Json::Object& Json::members() {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    object_.clear();
  }
  return object_;
}

const Json* Json::find(const std::string& key) const {
  if (type_ != Type::Object) return nullptr;
  for (const Member& member : object_) {
    if (member.first == key) return &member.second;
  }
  return nullptr;
}

void Json::set(std::string key, Json value) {
  Object& target = members();
  for (Member& member : target) {
    if (member.first == key) {
      member.second = std::move(value);
      return;
    }
  }
  target.emplace_back(std::move(key), std::move(value));
}

void Json::push(Json value) { items().push_back(std::move(value)); }

std::size_t Json::size() const {
  if (type_ == Type::Array) return array_.size();
  if (type_ == Type::Object) return object_.size();
  return 0;
}

bool Json::operator==(const Json& other) const {
  if (type_ != other.type_) return false;
  switch (type_) {
    case Type::Null: return true;
    case Type::Bool: return bool_ == other.bool_;
    case Type::Int: return int_ == other.int_;
    // 浮点在协议里是非法值，能走到这里只有测试与错误信息，按位相等足够。
    case Type::Double: return double_ == other.double_;
    case Type::String: return string_ == other.string_;
    case Type::Array: return array_ == other.array_;
    case Type::Object: {
      // 对象比较**与键顺序无关**：JSON 里键顺序无意义（协议 §2.4 补充）。
      if (object_.size() != other.object_.size()) return false;
      for (const Member& member : object_) {
        const Json* rhs = other.find(member.first);
        if (rhs == nullptr || !(member.second == *rhs)) return false;
      }
      return true;
    }
  }
  return false;
}

std::string Json::dump() const {
  std::string out;
  dumpInto(*this, out);
  return out;
}

const char* Json::typeName(Type type) {
  switch (type) {
    case Type::Null: return "null";
    case Type::Bool: return "bool";
    case Type::Int: return "int";
    case Type::Double: return "float";
    case Type::String: return "string";
    case Type::Array: return "array";
    case Type::Object: return "object";
  }
  return "unknown";
}

}  // namespace imrtc
