#include "imrtc/MediaAdapter.h"

namespace imrtc {

const char* pcRoleName(PcRole role) { return role == PcRole::Sub ? "sub" : "pub"; }

bool parsePcRole(const std::string& text, PcRole& out) {
  if (text == "pub") {
    out = PcRole::Pub;
    return true;
  }
  if (text == "sub") {
    out = PcRole::Sub;
    return true;
  }
  return false;
}

const char* mediaKindName(MediaKind kind) { return kind == MediaKind::Video ? "video" : "audio"; }

const char* pcStateName(PcState state) {
  switch (state) {
    case PcState::New: return "new";
    case PcState::Connecting: return "connecting";
    case PcState::Connected: return "connected";
    case PcState::Disconnected: return "disconnected";
    case PcState::Failed: return "failed";
    case PcState::Closed: return "closed";
  }
  return "new";
}

}  // namespace imrtc
