#pragma once

#include <cstdint>

#include "imrtc/CallMachine.h"
#include "imrtc/RoomMachine.h"

namespace imrtc {

/**
 * Engine 的总状态：把通话机与房间机合起来，并处理只有「合起来」才说得清的事。
 *
 * 四件只有这一层能做的事：
 * 1. **连接级事件**（onConnected / onDisconnected / onKickedOut）由这里抛——
 *    它们既不属于某次通话，也不属于某个房间。
 * 2. **重连恢复失败**时，房间回 idle **且**通话要本地合成 `onCallEnd(network)`
 *    （不变量 I8）——服务端那条 ended 帧送不到我们手里了。
 * 3. **通话机产出的 room.join** 要转成房间机的 join 动作，否则房间状态机不知道
 *    自己正在进房，之后的 join.ok 就没人接，UI 停在「接通中」不动。
 * 4. **通话结束时房间要回 idle**。这条是 3 的反向，漏了它的后果比漏 3 还隐蔽：
 *    `call.ended` 之后服务端就把房间销毁了，而房间机还停在 joined，
 *    于是**之后每一帧都发向一个已经不存在的房间**（服务端回 1201），
 *    下一次进房还会因为「不在 idle」被本地拒掉。
 */
struct EngineContext {
  RoomContext room;
  CallContext call;
};

using EngineOutput = MachineOutput<EngineContext>;

/**
 * reduceEngine 是 Engine 状态的唯一入口。
 *
 * `nowMs` 只在一个地方用得上：恢复失败时本地合成 onCallEnd 的时长（不变量 I8）。
 * **状态机不自己读时钟**——那样就没法用向量复现了。
 */
EngineOutput reduceEngine(const EngineContext& ctx, const MachineInput& input,
                          std::int64_t nowMs = 0);

/**
 * forceEnd 算出强制收场的结果（门面见 `CallEngine::forceEnd`）：通话机、房间机一起归零，
 * 抛唯一的结束出口（通话 `onCallEnd`、会议 `onRoomLeft`），并给出该发的结束帧。
 * 没有进行中的通话也不在房里时原样返回（`emit` 为空）。
 *
 * 时长起点优先用**本端**进这通电话的时刻 `startedAtMs`（门面记的），没有才退回整通的
 * `connectedAtMs`——中途被拉进来的人用后者会偏大（Web / iOS 2026-09-15 真机踩过）。
 *
 * 纯函数，与 Web `state/forceEnd.ts`、iOS `IMEngineMachine.forceEnd` 同形。
 */
EngineOutput forceEnd(const EngineContext& ctx, std::int64_t nowMs, std::int64_t startedAtMs = 0);

}  // namespace imrtc
