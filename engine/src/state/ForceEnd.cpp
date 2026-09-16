#include <string>
#include <utility>
#include <vector>

#include "imrtc/EngineMachine.h"
#include "imrtc/Envelope.h"
#include "imrtc/Reasons.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/**
 * endFrames 按通话此刻的状态挑结束帧，以及本地收场写哪个结束原因。
 *
 * - `accepting` 发 **reject + hangup 两帧**：accept 有没有在服务端落地，本端不知道。
 *   还在响铃就是 reject 生效（随后那条 hangup 被拒，无害）；已经接起来就是 hangup 生效。
 * - `inviting` 还没拿到 call_id（`call.invite.ok` 没回来）时**此刻发不了 cancel**，
 *   由那条 invite.ok 迟到时补发（CallRecv.cpp 的 handleLateFrame）。
 *
 * 与 Web `state/forceEnd.ts` 的 `endFrames()` 同一张表。
 */
std::pair<std::vector<OutgoingFrame>, const char*> endFrames(const CallContext& call) {
  const char* reason = reason::kHangup;
  std::vector<const char*> types;
  switch (call.state) {
    case CallState::Idle: return {{}, reason};
    case CallState::Ringing:
      reason = reason::kReject;
      types = {frame::kCallReject};
      break;
    case CallState::Inviting:
      reason = reason::kCancel;
      types = {frame::kCallCancel};
      break;
    case CallState::Accepting: types = {frame::kCallReject, frame::kCallHangup}; break;
    case CallState::Connecting:
    case CallState::Connected: types = {frame::kCallHangup}; break;
  }
  std::vector<OutgoingFrame> frames;
  if (call.callId.empty()) return {std::move(frames), reason};
  for (const char* type : types) {
    frames.push_back(frameOf(type, obj({{"call_id", Json::make(call.callId)}})));
  }
  return {std::move(frames), reason};
}

}  // namespace

EngineOutput forceEnd(const EngineContext& ctx, std::int64_t nowMs, std::int64_t startedAtMs) {
  if (ctx.call.state != CallState::Idle) {
    auto [frames, reason] = endFrames(ctx.call);
    // 时长与 I8 那条例外同一个算法：本地已经收场，服务端那条带真值的 call.ended 随后会因为 idle 被丢掉。
    const std::int64_t since = startedAtMs > 0 ? startedAtMs : ctx.call.connectedAtMs;
    EngineContext next;
    next.room = clearedRoom(RoomState::Idle);
    return EngineOutput{
        std::move(next), std::move(frames),
        {eventOf("onCallEnd",
                 obj({{"call_id", Json::make(ctx.call.callId)},
                      {"reason", Json::make(reason)},
                      {"duration_sec", Json::make(callDurationSec(since, nowMs))},
                      {"ended_by", Json::make("")}}))}};
  }
  if (ctx.room.state == RoomState::Idle) return EngineOutput{ctx, {}, {}};

  // 没有通话却在房里：会议。结束动作是离房。
  std::vector<OutgoingFrame> send;
  if (!ctx.room.roomId.empty()) {
    send.push_back(frameOf(frame::kRoomLeave, obj({{"room_id", Json::make(ctx.room.roomId)}})));
  }
  EngineContext next = ctx;
  next.room = clearedRoom(RoomState::Idle);
  return EngineOutput{std::move(next),
                      std::move(send),
                      {eventOf("onRoomLeft", obj({{"room_id", Json::make(ctx.room.roomId)}}))}};
}

}  // namespace imrtc
