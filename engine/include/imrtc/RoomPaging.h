#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/RoomMachine.h"

/*
 会议房的**按页订阅**：把「这个人现在看得见吗」翻译成订阅与退订。

 见 `im-rtc-server/docs/design/MEETING_ROOM_DESIGN.md` §4.3。

 # 为什么挂在 setRemoteLayer 上，而不是新开一个 API

 **Engine 的公开 API 一个都不新增。** 自画 UI 的宿主本来就得按可视尺寸调
 `setRemoteLayer(uid, layer)`（九宫格报 `l`、放大报 `h`、看不见报 `none`），
 这套调用已经**完整地表达了「谁在当前页」**。会议房要的只是把同一组调用翻译成另一套帧：
 `l/m/h` = 订阅或换层，`none` = 五秒后退订。

 # 桌面端做到哪一步

 本期桌面端**没有会议界面**（分页画廊跟随桌面 UI 排期），但这套翻译在**引擎**里必须有：
 少了它，`auto_subscribe:"audio"` 的房间里 `setRemoteLayer` 会对一条**根本没订阅**的流
 发 `room.update_layer`，服务端回 1301，画面永远不来。五端跑同一份 `room_fsm.json`，
 这条也是那份向量的一部分。

 # 只在 auto_subscribe == "audio" 的房间里生效

 通话房是 `all`：服务端全自动订好，`none` 的语义只是**暂停下发**（协议 §3.5），
 退订会让那个人永远消失。这条分支写错的后果就是通话房里有人的画面再也回不来。
*/

namespace imrtc {

/**
 * kUnsubscribeHysteresisMs 是翻页离开之后**等多久才真的退订**。
 *
 * 退订要重协商（sub PC 少一条 m-line），而翻页是来回的动作：左滑一页看一眼再滑回来
 * 是最常见的操作。立刻退订的话这一来一回要两次协商，回来那一下还得重新等关键帧，
 * 画面黑一下。等五秒，来回翻的那一种就一次协商都不用。
 *
 * 定时器不在状态机里（状态机是纯函数）：由 `CallEngine::tick()` 按
 * `RoomContext::pendingUnsubscribe` 记下截止时刻，到点喂一个内部事件回来。
 */
constexpr std::int64_t kUnsubscribeHysteresisMs = 5000;

/**
 * kMaxSubscribedVideo 是同时订阅的视频路数上限（手机：本页 8 + 迟滞 8）。
 *
 * **这个数是 SDP 墙定的，不是算力定的**：sub offer 每订一路多一条 m-line，
 * 整帧超过 64 KiB 就发不出去，而发不出去的后果是这个人的下行**永久冻结**（设计 §1.3）。
 */
constexpr std::size_t kMaxSubscribedVideo = 16;

/** usesPagedVideo 报告这个房间的视频是不是由客户端按页订阅的。 */
bool usesPagedVideo(const RoomContext& ctx);

/**
 * pagedUpdateLayer 把一次 `setRemoteLayer` 翻译成订阅动作。
 *
 * - `none`：**先发 `room.update_layer{none}` 立刻停包**，再排五秒的退订。
 * - `l/m/h`：撤掉还没到点的退订；订过就只换层（**不重协商**），没订过就订。
 */
RoomOutput pagedUpdateLayer(const RoomContext& ctx, const std::string& trackId,
                            const std::string& maxLayer);

/**
 * flushHysteresis 让迟滞到点：把排着的退订真的发出去。
 *
 * `trackId` 传空串时把**全部**排着的一次清掉（一致性向量用的就是这一种）。
 * **通话房什么都不做**：它的 `none` 只是暂停，退订会让那个人的画面再也回不来。
 */
RoomOutput flushHysteresis(const RoomContext& ctx, const std::string& trackId);

/**
 * dropPending 把已经不存在的 track 从待退订队列里摘掉。
 *
 * 人走了、对方 unpublish 了，那条订阅本来就没了。不摘的话到点会发一条打在空处的
 * `room.unsubscribe`。
 */
void dropPending(RoomContext& ctx, const std::vector<std::string>& gone);

}  // namespace imrtc
