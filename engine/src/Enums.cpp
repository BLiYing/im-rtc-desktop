#include "imrtc/Enums.h"

#include <algorithm>

namespace imrtc {

const EnumValues& mediaTypes() {
  static const EnumValues kValues = {"audio", "video"};
  return kValues;
}

const EnumValues& reasonValues() {
  static const EnumValues kValues = {"hangup",  "cancel",  "reject",
                                     "no_answer", "busy",  "offline",
                                     "answered_elsewhere", "rejected_elsewhere",
                                     "kicked",  "room_closed", "network", "error"};
  return kValues;
}

const EnumValues& layers() {
  static const EnumValues kValues = {"none", "l", "m", "h"};
  return kValues;
}

const EnumValues& autoSubscribeModes() {
  static const EnumValues kValues = {"all", "audio", "none"};
  return kValues;
}

bool autoSubscribeCovers(const std::string& mode, const std::string& kind) {
  if (mode == "all") return true;
  return mode == "audio" && kind == "audio";
}

std::string coerceAutoSubscribe(const std::string& mode) {
  const EnumValues& modes = autoSubscribeModes();
  return std::find(modes.begin(), modes.end(), mode) == modes.end() ? "all" : mode;
}

const EnumValues& trackKinds() {
  static const EnumValues kValues = {"audio", "video"};
  return kValues;
}

const EnumValues& trackSources() {
  static const EnumValues kValues = {"microphone", "camera", "screen", "screen_audio"};
  return kValues;
}

const EnumValues& pcRoles() {
  static const EnumValues kValues = {"pub", "sub"};
  return kValues;
}

const EnumValues& roomKinds() {
  static const EnumValues kValues = {"call_1v1", "call_group", "meeting"};
  return kValues;
}

const EnumValues& handledActions() {
  static const EnumValues kValues = {"accept", "reject"};
  return kValues;
}

}  // namespace imrtc
