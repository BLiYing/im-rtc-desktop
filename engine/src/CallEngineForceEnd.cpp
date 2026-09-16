#include <string>
#include <utility>

#include "imrtc/CallEngine.h"
#include "imrtc/EngineMachine.h"
#include "imrtc/Log.h"

namespace imrtc {

/**
 * 强制收场的门面（状态怎么收见 `imrtc::forceEnd`）。
 *
 * 与 CallEngine.cpp 拆开是体量红线（CONVENTIONS §3，那边已经 500 多行）。
 *
 * **结束帧的应答不回灌状态机**：本地已经收场，`room.leave.ok` 喂回去会再抛一次 onRoomLeft，
 * 被拒（通话已经结束回 1401 之类）也改变不了什么——只记一条日志。
 */
void CallEngine::forceEnd() {
  const std::string callId = context_.call.callId;
  const std::string roomId = context_.room.roomId;
  EngineOutput output = imrtc::forceEnd(context_, options_.clock(), callStartedAtMs_);
  if (output.emit.empty()) {
    log(LogLevel::Info, "强制收场：没有进行中的通话或房间", {});
    return;
  }
  std::string types;
  for (const OutgoingFrame& frame : output.send) types += (types.empty() ? "" : ",") + frame.type;
  log(LogLevel::Warn, "强制收场",
      {{logfield::kCallId, callId}, {logfield::kRoomId, roomId}, {"frames", types}});

  const std::int64_t now = options_.clock();
  for (const OutgoingFrame& frame : output.send) {
    const bool sent = connection_ && connection_->request(
        frame.type, frame.data, now, [type = frame.type](const RequestResult& result) {
          if (!result.ok) log(LogLevel::Info, "强制收场的结束帧没成（本地已收场，无害）", {{logfield::kType, type}});
        });
    if (!sent) log(LogLevel::Warn, "强制收场：没有信令连接，结束帧发不出去，只做本地收场", {{logfield::kType, frame.type}});
  }
  output.send.clear();
  dispatchOutput(std::move(output), "");
}

}  // namespace imrtc
