#include "CApiConvert.h"

#include <algorithm>

#include "imrtc/Enums.h"

namespace imrtc {
namespace capi_detail {

std::string cstr(const char* text) { return text == nullptr ? std::string() : std::string(text); }

bool toBool(imrtc_v1_bool value) { return value != 0; }
imrtc_v1_bool fromBool(bool value) { return value ? 1 : 0; }

bool isValidLayer(const std::string& layer) {
  const imrtc::EnumValues& allowed = imrtc::layers();
  return std::find(allowed.begin(), allowed.end(), layer) != allowed.end();
}

imrtc::ActionCompletion toCompletion(imrtc_v1_result_cb cb, void* userData) {
  if (cb == nullptr) return {};
  return [cb, userData](const imrtc::ActionResult& result) {
    cb(userData, result.code, result.name.c_str(), result.value.c_str());
  };
}

std::vector<std::string> toStrings(const char* const* items, std::uint32_t count) {
  std::vector<std::string> out;
  if (items == nullptr) return out;
  out.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) out.push_back(cstr(items[i]));
  return out;
}

imrtc_v1_log_level toLogLevel(imrtc::LogLevel level) {
  switch (level) {
    case imrtc::LogLevel::Debug: return IMRTC_V1_LOG_DEBUG;
    case imrtc::LogLevel::Warn: return IMRTC_V1_LOG_WARN;
    case imrtc::LogLevel::Error: return IMRTC_V1_LOG_ERROR;
    case imrtc::LogLevel::Info: break;
  }
  return IMRTC_V1_LOG_INFO;
}

imrtc::LogLevel fromLogLevel(imrtc_v1_log_level level) {
  switch (level) {
    case IMRTC_V1_LOG_DEBUG: return imrtc::LogLevel::Debug;
    case IMRTC_V1_LOG_WARN: return imrtc::LogLevel::Warn;
    case IMRTC_V1_LOG_ERROR: return imrtc::LogLevel::Error;
    case IMRTC_V1_LOG_INFO: break;
  }
  return imrtc::LogLevel::Info;
}

imrtc_v1_kicked_reason toKickedReason(imrtc::KickedReason reason) {
  switch (reason) {
    case imrtc::KickedReason::AuthExpired: return IMRTC_V1_KICKED_AUTH_EXPIRED;
    case imrtc::KickedReason::ConfigRejected: return IMRTC_V1_KICKED_CONFIG_REJECTED;
    case imrtc::KickedReason::TakenOver: break;
  }
  return IMRTC_V1_KICKED_TAKEN_OVER;
}

imrtc_v1_end_reason toEndReason(imrtc::EndReason reason) {
  switch (reason) {
    case imrtc::EndReason::Hangup: return IMRTC_V1_END_HANGUP;
    case imrtc::EndReason::Cancel: return IMRTC_V1_END_CANCEL;
    case imrtc::EndReason::Reject: return IMRTC_V1_END_REJECT;
    case imrtc::EndReason::NoAnswer: return IMRTC_V1_END_NO_ANSWER;
    case imrtc::EndReason::Busy: return IMRTC_V1_END_BUSY;
    case imrtc::EndReason::Offline: return IMRTC_V1_END_OFFLINE;
    case imrtc::EndReason::AnsweredElsewhere: return IMRTC_V1_END_ANSWERED_ELSEWHERE;
    case imrtc::EndReason::RejectedElsewhere: return IMRTC_V1_END_REJECTED_ELSEWHERE;
    case imrtc::EndReason::Kicked: return IMRTC_V1_END_KICKED;
    case imrtc::EndReason::RoomClosed: return IMRTC_V1_END_ROOM_CLOSED;
    case imrtc::EndReason::Network: return IMRTC_V1_END_NETWORK;
    case imrtc::EndReason::Error: break;
  }
  return IMRTC_V1_END_ERROR;
}

/*
  **枚举值必须与 C 头一一对应**。这里直接把 C++ 的枚举强转成 int32 交出去，
  所以一旦有人重排了 CallState / RoomState / EndReason 的声明顺序，C 宿主收到的
  就是**错的状态**——而且不报错、不崩，只是界面开始胡说八道。下面这组断言让那种
  改动在编译期就挂掉。CallState / RoomState 没有专门的转换函数（imrtc_c.cpp 里
  是直接 static_cast 的），断言放在这里是因为它们和上面几个转换函数同属
  「两套枚举的漂移哨兵」，比分散到各个用它的地方更容易一次看全。
*/
static_assert(static_cast<int>(imrtc::CallState::Idle) == IMRTC_V1_CALL_IDLE, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Inviting) == IMRTC_V1_CALL_INVITING, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Ringing) == IMRTC_V1_CALL_RINGING, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Accepting) == IMRTC_V1_CALL_ACCEPTING, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Connecting) == IMRTC_V1_CALL_CONNECTING, "枚举漂了");
static_assert(static_cast<int>(imrtc::CallState::Connected) == IMRTC_V1_CALL_CONNECTED, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Idle) == IMRTC_V1_ROOM_IDLE, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Joining) == IMRTC_V1_ROOM_JOINING, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Joined) == IMRTC_V1_ROOM_JOINED, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Leaving) == IMRTC_V1_ROOM_LEAVING, "枚举漂了");
static_assert(static_cast<int>(imrtc::RoomState::Reconnecting) == IMRTC_V1_ROOM_RECONNECTING,
              "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::Hangup) == IMRTC_V1_END_HANGUP, "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::Cancel) == IMRTC_V1_END_CANCEL, "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::Reject) == IMRTC_V1_END_REJECT, "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::NoAnswer) == IMRTC_V1_END_NO_ANSWER, "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::Busy) == IMRTC_V1_END_BUSY, "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::Offline) == IMRTC_V1_END_OFFLINE, "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::AnsweredElsewhere) ==
                  IMRTC_V1_END_ANSWERED_ELSEWHERE,
              "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::RejectedElsewhere) ==
                  IMRTC_V1_END_REJECTED_ELSEWHERE,
              "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::Kicked) == IMRTC_V1_END_KICKED, "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::RoomClosed) == IMRTC_V1_END_ROOM_CLOSED,
              "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::Network) == IMRTC_V1_END_NETWORK, "枚举漂了");
static_assert(static_cast<int>(imrtc::EndReason::Error) == IMRTC_V1_END_ERROR, "枚举漂了");

}  // namespace capi_detail
}  // namespace imrtc
