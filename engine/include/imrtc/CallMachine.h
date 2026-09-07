#pragma once

#include <cstdint>
#include <string>

#include "imrtc/MachineTypes.h"

namespace imrtc {

/**
 * 通话状态机：RTC_PROTOCOL.md §5.1 的 C++ 实现。
 *
 * 一致性向量：`im-rtc-server/docs/conformance/call_fsm.json`，五端跑同一份。
 *
 * # 三条容易写错的地方
 *
 * 1. **没有 `ended` 状态**——`ended` 是事件不是状态。草图 §09 里那个「停 1.5s」的
 *    方框是 UI 层的展示状态，由 UI 自己持有（不变量 I5）。
 * 2. **按成员裁决的便利事件只在 1v1 抛**（`onCallRejected` / `onCallBusy` /
 *    `onCallNoAnswer`）。群通话里某人拒接，通话还在继续，后面并不会紧跟一条
 *    `onCallEnd`，抛便利事件就违反了「便利事件后必定跟 onCallEnd」（I7）。
 *
 *    `onCallCancelled` **不在这一组里**，它群里也照抛：`call.cancelled` 说的是
 *    主叫把整通电话取消了，服务端随后给每个人都发 `call.ended`——I7 成立。
 *    （早先这行把它一起列进了「只在 1v1」，与 CallRecv.cpp 的实现对不上；
 *    对不上的是这行字，不是代码。）
 * 3. **状态只由信令帧与宿主调用驱动，禁止由定时器改状态**（I4）。
 *    本地振铃倒计时只改 UI，超时由服务端裁决。
 */

/** CallState 是通话状态。**没有 ended**，见文件头。 */
enum class CallState { Idle, Inviting, Ringing, Accepting, Connecting, Connected };

/** callStateName / parseCallState 在状态与协议里的字符串之间转换。 */
const char* callStateName(CallState state);
bool parseCallState(const std::string& text, CallState& out);

/** CallRole 是本端在这通电话里的角色。空串表示还没有角色。 */
struct CallContext {
  CallState state = CallState::Idle;
  std::string callId;
  std::string roomId;
  std::string roomToken;
  std::string mediaType = "audio";
  bool isGroup = false;
  /** "caller" / "callee" / ""。 */
  std::string role;
  /** 通话时长的起点，来自服务端。**客户端不自己算时长**（I8）。 */
  std::int64_t connectedAtMs = 0;
};

using CallOutput = MachineOutput<CallContext>;

/** callOut 构造一次状态转移的产物。CallRecv.cpp 也用它。 */
CallOutput callOut(CallContext state, std::vector<OutgoingFrame> send = {},
                   std::vector<EmittedEvent> emit = {});

/** invalidCallState 是「宿主在错误状态下调方法」的统一落点：本地拒绝，不发上去。 */
CallOutput invalidCallState(const CallContext& ctx);

/**
 * reduceCall 是通话状态机的唯一入口。
 *
 * `nowMs` 只有一个用处：本地合成 `onCallEnd` 时算时长（不变量 I8 的例外）。
 * **状态机不自己读时钟**（I4），否则一致性向量没法复现。
 */
CallOutput reduceCall(const CallContext& ctx, const MachineInput& input,
                      std::int64_t nowMs = 0);

/** reduceCallRecv 处理一条下行帧（转移表的右半边），在 CallRecv.cpp 里。 */
CallOutput reduceCallRecv(const CallContext& ctx, const std::string& type, const Json& data);

/**
 * synthesizeNetworkEnd 是不变量 I8 的那个**唯一例外**：重连恢复失败时服务端的
 * `call.ended` 送不到客户端，Engine 本地合成一条 `onCallEnd(network)`，时长用本地计时。
 */
CallOutput synthesizeNetworkEnd(const CallContext& ctx, std::int64_t nowMs);

}  // namespace imrtc
