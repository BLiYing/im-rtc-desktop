#include "imrtc/RoomPaging.h"

#include <algorithm>
#include <utility>

#include "imrtc/Errors.h"
#include "imrtc/MachineTypes.h"
#include "imrtc/Registry.h"
#include "imrtc/RoomMachine.h"

namespace imrtc {
namespace {

/** countLiveVideo 数此刻**占着 m-line** 的视频路数：订上的与正在订的都算，正在退的不算。 */
std::size_t countLiveVideo(const RoomContext& ctx) {
  std::size_t count = 0;
  for (const auto& entry : ctx.subscribe) {
    if (entry.second == "unsubscribing") continue;
    const auto track = ctx.remoteTracks.find(entry.first);
    if (track == ctx.remoteTracks.end() || track->second.kind != "video") continue;
    ++count;
  }
  return count;
}

/** unsubscribeNow 把一条真的退掉；已经没订或正在退的什么都不做。 */
void unsubscribeNow(RoomContext& ctx, const std::string& trackId,
                    std::vector<OutgoingFrame>& send) {
  const auto it = ctx.subscribe.find(trackId);
  if (it == ctx.subscribe.end() || it->second == "unsubscribing") return;
  it->second = "unsubscribing";
  ctx.layers.erase(trackId);
  send.push_back(frameOf(frame::kRoomUnsubscribe, obj({{"track_id", Json::make(trackId)}})));
}

/**
 * freeSlot 在订满 16 路时**提前**把排着的退订执行掉，腾出位置。
 *
 * 快速连翻几页就会踩到：第一页还在五秒迟滞里，第二页也翻走了，第三页要订新的。
 * 迟滞是一种便利，不是承诺——位置不够时先退最早翻走的那一页，正是想退的顺序。
 */
void freeSlot(RoomContext& ctx, std::vector<OutgoingFrame>& send) {
  while (countLiveVideo(ctx) >= kMaxSubscribedVideo && !ctx.pendingUnsubscribe.empty()) {
    const std::string oldest = ctx.pendingUnsubscribe.front();
    ctx.pendingUnsubscribe.erase(ctx.pendingUnsubscribe.begin());
    unsubscribeNow(ctx, oldest, send);
  }
}

RoomOutput pageOut(const RoomContext& ctx, const std::string& trackId) {
  // 没订过的不用退；已经排着退订的也不用再报一次 none——它早就不出包了，
  // 再报一次只会把五秒的计时重新拉长。
  if (ctx.subscribe.find(trackId) == ctx.subscribe.end()) return roomOut(ctx);
  const auto& pending = ctx.pendingUnsubscribe;
  if (std::find(pending.begin(), pending.end(), trackId) != pending.end()) return roomOut(ctx);

  RoomContext next = ctx;
  next.layers[trackId] = "none";
  next.pendingUnsubscribe.push_back(trackId);
  return roomOut(std::move(next),
                 {frameOf(frame::kRoomUpdateLayer,
                          obj({{"track_id", Json::make(trackId)},
                               {"max_layer", Json::make(std::string("none"))}}))});
}

RoomOutput pageIn(const RoomContext& ctx, const std::string& trackId,
                  const std::string& maxLayer) {
  RoomContext next = ctx;
  next.pendingUnsubscribe.erase(
      std::remove(next.pendingUnsubscribe.begin(), next.pendingUnsubscribe.end(), trackId),
      next.pendingUnsubscribe.end());

  const auto subscribed = next.subscribe.find(trackId);
  if (subscribed != next.subscribe.end() &&
      (subscribed->second == "subscribing" || subscribed->second == "subscribed")) {
    next.layers[trackId] = maxLayer;
    return roomOut(std::move(next),
                   {frameOf(frame::kRoomUpdateLayer,
                            obj({{"track_id", Json::make(trackId)},
                                 {"max_layer", Json::make(maxLayer)}}))});
  }

  /*
    **先问腾不腾得出位置，再动手**：本地拒绝要求不发帧、状态不变，
    所以不能先把强制退订发出去再反悔。

    排着迟滞的那些都还占着 m-line，它们是唯一能腾出来的位置。全退了还满，
    就**只可能是调用方一次要看超过 16 路视频**——翻页翻不出这种局面（一页 8 路），
    那是界面那边的 bug，不该由引擎悄悄吞掉。

    **不排队**：排队要有一个「什么时候轮到你」的触发点，而这里没有——
    订阅位是靠翻页腾出来的，队列只会安静地越积越长，
    表现成「第 17 个人的画面永远不出来，也没有任何报错」。
  */
  if (countLiveVideo(next) - next.pendingUnsubscribe.size() >= kMaxSubscribedVideo) {
    const std::int32_t code = codeValue(ErrorCode::InvalidState);
    RoomOutput out = roomOut(ctx);
    out.reject = LocalReject{code, errorName(code)};
    return out;
  }

  std::vector<OutgoingFrame> send;
  freeSlot(next, send);
  next.subscribe[trackId] = "subscribing";
  next.layers[trackId] = maxLayer;
  send.push_back(frameOf(frame::kRoomSubscribe, obj({{"track_id", Json::make(trackId)},
                                                     {"max_layer", Json::make(maxLayer)}})));
  return roomOut(std::move(next), std::move(send));
}

}  // namespace

bool usesPagedVideo(const RoomContext& ctx) { return ctx.autoSubscribe == "audio"; }

RoomOutput pagedUpdateLayer(const RoomContext& ctx, const std::string& trackId,
                            const std::string& maxLayer) {
  return maxLayer == "none" ? pageOut(ctx, trackId) : pageIn(ctx, trackId, maxLayer);
}

RoomOutput flushHysteresis(const RoomContext& ctx, const std::string& trackId) {
  if (!usesPagedVideo(ctx)) return roomOut(ctx);

  std::vector<std::string> targets;
  for (const std::string& id : ctx.pendingUnsubscribe) {
    if (trackId.empty() || id == trackId) targets.push_back(id);
  }
  if (targets.empty()) return roomOut(ctx);

  RoomContext next = ctx;
  next.pendingUnsubscribe.erase(
      std::remove_if(next.pendingUnsubscribe.begin(), next.pendingUnsubscribe.end(),
                     [&targets](const std::string& id) {
                       return std::find(targets.begin(), targets.end(), id) != targets.end();
                     }),
      next.pendingUnsubscribe.end());

  std::vector<OutgoingFrame> send;
  for (const std::string& id : targets) unsubscribeNow(next, id, send);
  return roomOut(std::move(next), std::move(send));
}

void dropPending(RoomContext& ctx, const std::vector<std::string>& gone) {
  ctx.pendingUnsubscribe.erase(
      std::remove_if(ctx.pendingUnsubscribe.begin(), ctx.pendingUnsubscribe.end(),
                     [&gone](const std::string& id) {
                       return std::find(gone.begin(), gone.end(), id) != gone.end();
                     }),
      ctx.pendingUnsubscribe.end());
}

}  // namespace imrtc
