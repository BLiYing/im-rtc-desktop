#include "imrtc/MachineTypes.h"

#include <utility>

namespace imrtc {

MachineInput MachineInput::act(std::string op, Json args) {
  MachineInput input;
  input.kind = Kind::Act;
  input.name = std::move(op);
  input.payload = std::move(args);
  return input;
}

MachineInput MachineInput::recv(std::string type, Json data) {
  MachineInput input;
  input.kind = Kind::Recv;
  input.name = std::move(type);
  input.payload = std::move(data);
  return input;
}

MachineInput MachineInput::internal(std::string name) {
  MachineInput input;
  input.kind = Kind::Internal;
  input.name = std::move(name);
  input.payload = Json::makeObject();
  return input;
}

std::string str(const Json& data, const std::string& key) {
  const Json* value = data.find(key);
  return value != nullptr && value->isString() ? value->asString() : std::string();
}

std::int64_t num(const Json& data, const std::string& key) {
  const Json* value = data.find(key);
  return value != nullptr && value->isInt() ? value->asInt() : 0;
}

bool boolean(const Json& data, const std::string& key) {
  const Json* value = data.find(key);
  return value != nullptr && value->isBool() && value->asBool();
}

std::vector<std::string> strArray(const Json& data, const std::string& key) {
  std::vector<std::string> out;
  const Json* value = data.find(key);
  if (value == nullptr || !value->isArray()) return out;
  for (const Json& item : value->items()) {
    if (item.isString()) out.push_back(item.asString());
  }
  return out;
}

Json obj(std::vector<Json::Member> members) { return Json::makeObject(std::move(members)); }

OutgoingFrame frameOf(const std::string& type, Json data) { return OutgoingFrame{type, std::move(data)}; }

EmittedEvent eventOf(const std::string& cb, Json args) { return EmittedEvent{cb, std::move(args)}; }

}  // namespace imrtc
