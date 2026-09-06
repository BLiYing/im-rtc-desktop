#pragma once

#include <map>
#include <string>
#include <vector>

#include "imrtc/MachineTypes.h"

namespace imrtc {

/**
 * 房间状态机：RTC_PROTOCOL.md §5.3 的 C++ 实现。
 *
 * 一致性向量：`im-rtc-server/docs/conformance/room_fsm.json`，五端跑同一份。
 *
 * # 三条不变量（协议 §5.3 的 R1~R3）
 *
 * - **R1** 只有 `joined` 才允许 publish / subscribe / mute；其余状态**本地拒绝**，
 *   不发上去让服务端报错。
 * - **R2** `joining` 与 `reconnecting` 期间**禁止发任何房间帧**，但要把用户意图
 *   缓存下来，进房/恢复后一次性重放。这两个状态的共同点是**宿主观察不到**——
 *   它拿到 onCallBegin 就推流是最自然的写法，不该因为一个内部中间态而失败。
 * - **R3** 订阅与换层是**幂等**的：重复 subscribe 同一条 track 等价于换层。
 */

/** RoomState 是房间连接状态。 */
enum class RoomState { Idle, Joining, Joined, Leaving, Reconnecting };

const char* roomStateName(RoomState state);
bool parseRoomState(const std::string& text, RoomState& out);

/** RemoteTrack 是远端 Track 的本地记账。 */
struct RemoteTrack {
  std::string uid;
  /** "audio" / "video"。`track_unpublished` 帧不带 kind，只能靠它。 */
  std::string kind;
  std::string participantId;
};

/**
 * BufferedIntent 是攒下来的一次调用，**存的是意图不是帧**。
 *
 * 存帧的话重放时只能原样发出去，状态（比如 publish[cid]="publishing"）就漏掉了；
 * 存意图则可以在 joined 态重新走一遍正常路径，跟没缓存过一模一样。
 */
struct BufferedIntent {
  std::string op;
  Json args;
};

/** RoomContext 是房间状态机持有的全部数据。 */
struct RoomContext {
  RoomState state = RoomState::Idle;
  std::string roomId;
  std::string roomToken;
  std::string participantId;
  bool autoSubscribe = true;
  /** cid → 发布状态（publishing / published / unpublishing）。发请求时还没有 track_id。 */
  std::map<std::string, std::string> publish;
  /** cid → 服务端分配的 track_id。 */
  std::map<std::string, std::string> publishTrackIds;
  /** track_id → 订阅状态（subscribing / subscribed / unsubscribing）。 */
  std::map<std::string, std::string> subscribe;
  /** track_id → 远端 Track 记账。 */
  std::map<std::string, RemoteTrack> remoteTracks;
  /** 期望的最高层。track_id → layer。 */
  std::map<std::string, std::string> layers;
  /** joining / reconnecting 期间缓存的用户意图（不变量 R2）。 */
  std::vector<BufferedIntent> buffered;
};

using RoomOutput = MachineOutput<RoomContext>;

/** roomOut 构造一次状态转移的产物。RoomRecv.cpp 也用它。 */
RoomOutput roomOut(RoomContext state, std::vector<OutgoingFrame> send = {},
                   std::vector<EmittedEvent> emit = {});

/** clearedRoom 把房间相关的记账全部清空，state 由调用方决定。 */
RoomContext clearedRoom(RoomState state);

/** reduceRoom 是房间状态机的唯一入口。 */
RoomOutput reduceRoom(const RoomContext& ctx, const MachineInput& input);

/** reduceRoomAct 处理宿主调用（供 replayBuffered 复用）。 */
RoomOutput reduceRoomAct(const RoomContext& ctx, const std::string& op, const Json& args);

/** reduceRoomRecv 处理一条下行房间帧，在 RoomRecv.cpp 里。 */
RoomOutput reduceRoomRecv(const RoomContext& ctx, const std::string& type, const Json& data);

/**
 * resumeRoom 在重连成功后恢复房间：重放缓存的用户意图。
 *
 * `resumed=false` 时**必须回到 idle 并重新 join**（协议 §1.4）——
 * 服务端那边的成员关系已经过期了，装作还在只会让 UI 撒谎。
 */
RoomOutput resumeRoom(const RoomContext& ctx, bool resumed);

/**
 * replayBuffered 在 joined 态把攒下的意图重新走一遍。
 *
 * **重放走的是正常路径**（reduceRoomAct），不是把缓存的帧直接吐出去——
 * 这样状态更新与帧生成永远一致，不会出现「帧发了但本地记账没跟上」。
 */
RoomOutput replayBuffered(const RoomContext& ctx);

}  // namespace imrtc
