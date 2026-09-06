#include "imrtc/Enums.h"

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
