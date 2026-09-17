#include <string>
#include <vector>

#include "TestHarness.h"
#include "imrtc/MachineTypes.h"
#include "imrtc/Registry.h"
#include "imrtc/RoomMachine.h"
#include "imrtc/RoomPaging.h"

using imrtc::Json;

/**
 * 会议房按页订阅（MEETING_ROOM_DESIGN §4.3）。
 *
 * 一致性向量（`room_fsm.json` 的 `meeting_audio_auto_video_by_page` 与
 * `call_room_auto_subscribe_all_layer_only`）钉的是**正常翻页那一条线**，
 * 这里补的是向量表达不了的两件事：**16 路上限**与**翻回来撤掉迟滞**。
 *
 * 桌面端本期没有会议界面，但这套翻译在引擎里必须有——少了它，
 * `auto_subscribe:"audio"` 的房间里 `setRemoteLayer` 会对一条根本没订阅的流发换层帧。
 */
namespace {

imrtc::RoomContext meeting(int videoCount) {
  imrtc::RoomContext ctx;
  ctx.state = imrtc::RoomState::Joined;
  ctx.roomId = "r-m";
  ctx.autoSubscribe = "audio";
  for (int i = 1; i <= videoCount; ++i) {
    const std::string trackId = "t-" + std::to_string(i);
    ctx.remoteTracks[trackId] = imrtc::RemoteTrack{"u" + std::to_string(i), "video",
                                                   "p-" + std::to_string(i)};
  }
  return ctx;
}

imrtc::RoomOutput layer(const imrtc::RoomContext& ctx, const std::string& trackId,
                        const std::string& maxLayer) {
  return imrtc::reduceRoomAct(
      ctx, "update_layer",
      imrtc::obj({{"track_id", Json::make(trackId)}, {"max_layer", Json::make(maxLayer)}}));
}

/** subscribeAll 把前 count 条视频都订上并坐实（模拟一页一页翻过来）。 */
imrtc::RoomContext subscribeAll(const imrtc::RoomContext& start, int count) {
  imrtc::RoomContext ctx = start;
  for (int i = 1; i <= count; ++i) ctx = layer(ctx, "t-" + std::to_string(i), "l").state;
  for (auto& entry : ctx.subscribe) entry.second = "subscribed";
  return ctx;
}

}  // namespace

IMRTC_TEST(pagingHysteresis, "按页订阅 —— 翻走先停包，五秒到点才退订") {
  const imrtc::RoomContext ctx = subscribeAll(meeting(3), 1);

  const imrtc::RoomOutput out = layer(ctx, "t-1", "none");
  CHECK_EQ(out.send.size(), std::size_t{1}, "只发一帧");
  CHECK_EQ(out.send[0].type, std::string(imrtc::frame::kRoomUpdateLayer), "这一步只停包，不退订");
  CHECK_EQ(out.state.pendingUnsubscribe.size(), std::size_t{1}, "排进待退订");
  CHECK_EQ(out.state.subscribe.at("t-1"), std::string("subscribed"), "订阅关系还在");

  const imrtc::RoomOutput elapsed = imrtc::reduceRoom(
      out.state, imrtc::MachineInput::internal("unsubscribe_hysteresis_elapsed",
                                               imrtc::obj({{"track_id", Json::make("t-1")}})));
  CHECK_EQ(elapsed.send.size(), std::size_t{1}, "到点要退订");
  CHECK_EQ(elapsed.send[0].type, std::string(imrtc::frame::kRoomUnsubscribe), "发的是退订");
  CHECK_EQ(elapsed.state.subscribe.at("t-1"), std::string("unsubscribing"), "记账跟上");
}

IMRTC_TEST(pagingPageBack, "按页订阅 —— 五秒内翻回来只换层，计时撤掉") {
  const imrtc::RoomContext ctx = subscribeAll(meeting(3), 1);
  const imrtc::RoomOutput out = layer(layer(ctx, "t-1", "none").state, "t-1", "l");

  CHECK_EQ(out.send.size(), std::size_t{1}, "只发一帧");
  CHECK_EQ(out.send[0].type, std::string(imrtc::frame::kRoomUpdateLayer),
           "翻回来不该再订一次——那就是一次白白的重协商");
  CHECK_TRUE(out.state.pendingUnsubscribe.empty(), "计时要撤掉");

  // 计时撤掉之后，那条内部事件迟到了也不许退订。
  const imrtc::RoomOutput late = imrtc::reduceRoom(
      out.state, imrtc::MachineInput::internal("unsubscribe_hysteresis_elapsed", Json::makeObject()));
  CHECK_TRUE(late.send.empty(), "撤过的迟滞不许再退订");
  CHECK_EQ(late.state.subscribe.at("t-1"), std::string("subscribed"), "还订着");
}

IMRTC_TEST(pagingQuota, "按页订阅 —— 订满 16 路时提前退掉最早翻走的那一条") {
  const int maxVideo = static_cast<int>(imrtc::kMaxSubscribedVideo);
  imrtc::RoomContext ctx = subscribeAll(meeting(maxVideo + 1), maxVideo);
  ctx = layer(ctx, "t-1", "none").state;
  ctx = layer(ctx, "t-2", "none").state;
  CHECK_EQ(ctx.pendingUnsubscribe.size(), std::size_t{2}, "两条排着");

  const imrtc::RoomOutput out = layer(ctx, "t-" + std::to_string(maxVideo + 1), "l");
  CHECK_EQ(out.send.size(), std::size_t{2}, "先退一条再订一条");
  CHECK_EQ(out.send[0].type, std::string(imrtc::frame::kRoomUnsubscribe), "先退");
  CHECK_EQ(out.send[0].data.find("track_id")->asString(), std::string("t-1"), "退最早翻走的");
  CHECK_EQ(out.send[1].type, std::string(imrtc::frame::kRoomSubscribe), "再订");
  CHECK_EQ(out.state.pendingUnsubscribe.size(), std::size_t{1}, "t-2 还在迟滞里，没被牵连");
}

IMRTC_TEST(pagingOverflow, "按页订阅 —— 一条都腾不出来时本地拒绝，不排队也不发帧") {
  const int maxVideo = static_cast<int>(imrtc::kMaxSubscribedVideo);
  const imrtc::RoomContext ctx = subscribeAll(meeting(maxVideo + 1), maxVideo);
  const imrtc::RoomOutput out = layer(ctx, "t-" + std::to_string(maxVideo + 1), "l");

  CHECK_TRUE(out.send.empty(), "本地拒绝不许带帧");
  CHECK_TRUE(out.reject.rejected(), "要有本地拒绝");
  CHECK_TRUE(out.state.subscribe.find("t-" + std::to_string(maxVideo + 1)) ==
                 out.state.subscribe.end(),
             "没订上就不该记账");
}

IMRTC_TEST(pagingCallRoomGuard, "按页订阅 —— 通话房报 none 只换层，迟滞事件到了也什么都不做") {
  imrtc::RoomContext ctx;
  ctx.state = imrtc::RoomState::Joined;
  ctx.roomId = "r-c";
  ctx.autoSubscribe = "all";
  ctx.remoteTracks["t-1"] = imrtc::RemoteTrack{"bob", "video", "p-1"};
  ctx.subscribe["t-1"] = "subscribed";

  const imrtc::RoomOutput out = layer(ctx, "t-1", "none");
  CHECK_EQ(out.send[0].type, std::string(imrtc::frame::kRoomUpdateLayer), "只换层");
  CHECK_TRUE(out.state.pendingUnsubscribe.empty(), "通话房不排退订");
  CHECK_EQ(out.state.subscribe.at("t-1"), std::string("subscribed"),
           "通话房退订会让那个人的画面再也回不来");

  const imrtc::RoomOutput elapsed = imrtc::reduceRoom(
      out.state, imrtc::MachineInput::internal("unsubscribe_hysteresis_elapsed", Json::makeObject()));
  CHECK_TRUE(elapsed.send.empty(), "通话房收到迟滞事件必须什么都不做");
}

IMRTC_TEST(pagingAutoSubscribeFallback, "按页订阅 —— 认不出的 auto_subscribe 兜底成 all 而不是 none") {
  const imrtc::RoomContext ctx;
  const imrtc::RoomOutput out = imrtc::reduceRoomAct(
      ctx, "join",
      imrtc::obj({{"room_id", Json::make("r-1")},
                  {"room_token", Json::make("tk")},
                  {"auto_subscribe", Json::make("video")}}));
  CHECK_EQ(out.state.autoSubscribe, std::string("all"), "兜底成 all");
  CHECK_EQ(out.send[0].data.find("auto_subscribe")->asString(), std::string("all"), "线路上也是 all");
}
