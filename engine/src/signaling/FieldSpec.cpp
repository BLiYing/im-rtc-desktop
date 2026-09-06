#include "imrtc/FieldSpec.h"

#include <algorithm>
#include <utility>

#include "imrtc/Errors.h"

namespace imrtc {
namespace {

[[noreturn]] void badParams(const std::string& reason) {
  throw RtcError(ErrorCode::BadParams, reason);
}

const std::string& expectString(const Json& value, const std::string& path) {
  if (!value.isString()) {
    badParams(path + " 必须是字符串，得到 " + value.typeName());
  }
  return value.asString();
}

std::int64_t expectInt(const Json& value, const std::string& path) {
  if (!value.isInt()) {
    badParams(path + " 必须是整数，得到 " + value.typeName());
  }
  return value.asInt();
}

const Json::Array& expectArray(const Json& value, const std::string& path) {
  if (!value.isArray()) {
    badParams(path + " 必须是数组，得到 " + value.typeName());
  }
  return value.items();
}

const Json& expectObject(const Json& value, const std::string& path) {
  if (!value.isObject()) {
    badParams(path + " 必须是对象，得到 " + value.typeName());
  }
  return value;
}

std::string normalizeEnum(const std::string& value, const EnumValues& values,
                          const std::string& fallback) {
  return std::find(values.begin(), values.end(), value) == values.end() ? fallback : value;
}

std::int64_t coerceInt(std::int64_t value, const FieldSpec& spec) {
  const bool belowMin = spec.hasMin && value < spec.minValue;
  const bool aboveMax = spec.hasMax && value > spec.maxValue;
  if (!belowMin && !aboveMax) return value;
  if (spec.hasOutOfRange) return spec.outOfRange;
  return belowMin ? spec.minValue : spec.maxValue;
}

/**
 * defaultOf 给出字段的协议默认值。
 *
 * **数组的默认值恒为空数组，绝不是缺席**——协议里没有 null，而「本该有的空数组
 * 不见了」在接收端表现为「这个人没有 simulcast 层」，很难往回查。
 */
Json defaultOf(const FieldSpec& spec) {
  switch (spec.kind) {
    case FieldKind::String: return Json::make(spec.defaultString);
    case FieldKind::Int: return Json::make(spec.defaultInt);
    case FieldKind::Bool: return Json::make(spec.defaultBool);
    case FieldKind::Enum:
      return Json::make(spec.defaultString.empty() ? spec.fallback : spec.defaultString);
    case FieldKind::StringArray:
    case FieldKind::EnumArray:
    case FieldKind::ObjectArray: return Json::makeArray();
    case FieldKind::Object: return decodeFields(*spec.fields, Json::makeObject());
  }
  return Json::makeNull();
}

Json decodeField(const FieldSpec& spec, const Json& value, const std::string& path) {
  switch (spec.kind) {
    case FieldKind::String: return Json::make(expectString(value, path));
    case FieldKind::Bool:
      // 布尔必须是真布尔：0/1/"true" 一律拒（§2.4「禁止用 0/1 代替」）。
      if (!value.isBool()) {
        badParams(path + " 必须是布尔，得到 " + value.typeName());
      }
      return value;
    case FieldKind::Int: return Json::make(coerceInt(expectInt(value, path), spec));
    case FieldKind::Enum:
      return Json::make(normalizeEnum(expectString(value, path), *spec.values, spec.fallback));
    case FieldKind::StringArray: {
      Json out = Json::makeArray();
      std::size_t index = 0;
      for (const Json& item : expectArray(value, path)) {
        out.push(Json::make(expectString(item, path + "[" + std::to_string(index) + "]")));
        ++index;
      }
      return out;
    }
    case FieldKind::EnumArray: {
      Json out = Json::makeArray();
      std::size_t index = 0;
      for (const Json& item : expectArray(value, path)) {
        const std::string& raw = expectString(item, path + "[" + std::to_string(index) + "]");
        out.push(Json::make(normalizeEnum(raw, *spec.values, spec.fallback)));
        ++index;
      }
      return out;
    }
    case FieldKind::ObjectArray: {
      Json out = Json::makeArray();
      std::size_t index = 0;
      for (const Json& item : expectArray(value, path)) {
        const std::string itemPath = path + "[" + std::to_string(index) + "]";
        out.push(decodeFields(*spec.fields, expectObject(item, itemPath), itemPath));
        ++index;
      }
      return out;
    }
    case FieldKind::Object: return decodeFields(*spec.fields, expectObject(value, path), path);
  }
  return Json::makeNull();
}

}  // namespace

FieldSpec stringField(std::string wire, std::string defaultValue) {
  FieldSpec spec;
  spec.kind = FieldKind::String;
  spec.wire = std::move(wire);
  spec.defaultString = std::move(defaultValue);
  return spec;
}

FieldSpec intField(std::string wire, std::int64_t defaultValue) {
  FieldSpec spec;
  spec.kind = FieldKind::Int;
  spec.wire = std::move(wire);
  spec.defaultInt = defaultValue;
  return spec;
}

FieldSpec intRangeField(std::string wire, std::int64_t defaultValue, std::int64_t minValue,
                        std::int64_t maxValue) {
  FieldSpec spec = intField(std::move(wire), defaultValue);
  spec.hasMin = true;
  spec.minValue = minValue;
  spec.hasMax = true;
  spec.maxValue = maxValue;
  return spec;
}

FieldSpec intFoldField(std::string wire, std::int64_t minValue, std::int64_t maxValue,
                       std::int64_t outOfRange) {
  FieldSpec spec = intRangeField(std::move(wire), 0, minValue, maxValue);
  spec.hasOutOfRange = true;
  spec.outOfRange = outOfRange;
  return spec;
}

FieldSpec boolField(std::string wire, bool defaultValue) {
  FieldSpec spec;
  spec.kind = FieldKind::Bool;
  spec.wire = std::move(wire);
  spec.defaultBool = defaultValue;
  return spec;
}

FieldSpec enumField(std::string wire, const EnumValues& values, std::string fallback) {
  FieldSpec spec;
  spec.kind = FieldKind::Enum;
  spec.wire = std::move(wire);
  spec.values = &values;
  spec.fallback = std::move(fallback);
  return spec;
}

FieldSpec enumField(std::string wire, const EnumValues& values, std::string fallback,
                    std::string defaultValue) {
  FieldSpec spec = enumField(std::move(wire), values, std::move(fallback));
  spec.defaultString = std::move(defaultValue);
  return spec;
}

FieldSpec stringArrayField(std::string wire) {
  FieldSpec spec;
  spec.kind = FieldKind::StringArray;
  spec.wire = std::move(wire);
  return spec;
}

FieldSpec enumArrayField(std::string wire, const EnumValues& values, std::string fallback) {
  FieldSpec spec;
  spec.kind = FieldKind::EnumArray;
  spec.wire = std::move(wire);
  spec.values = &values;
  spec.fallback = std::move(fallback);
  return spec;
}

FieldSpec objectArrayField(std::string wire, const FrameFields& fields) {
  FieldSpec spec;
  spec.kind = FieldKind::ObjectArray;
  spec.wire = std::move(wire);
  spec.fields = &fields;
  return spec;
}

FieldSpec objectField(std::string wire, const FrameFields& fields) {
  FieldSpec spec;
  spec.kind = FieldKind::Object;
  spec.wire = std::move(wire);
  spec.fields = &fields;
  return spec;
}

Json decodeFields(const FrameFields& fields, const Json& raw, const std::string& path) {
  Json out = Json::makeObject();
  for (const FieldSpec& spec : fields) {
    const Json* value = raw.find(spec.wire);
    out.set(spec.wire, value == nullptr ? defaultOf(spec)
                                        : decodeField(spec, *value, path + "." + spec.wire));
  }
  return out;
}

Json newFrameData(const FrameFields& fields) { return decodeFields(fields, Json::makeObject()); }

}  // namespace imrtc
