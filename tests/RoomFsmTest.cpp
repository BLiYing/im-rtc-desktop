#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "Vectors.h"
#include "imrtc/EngineMachine.h"
#include "imrtc/Registry.h"

using imrtc::EngineContext;
using imrtc::EngineOutput;
using imrtc::Json;
using imrtc::MachineInput;
using imrtc::RoomState;

/**
 * 房间状态机跑 `room_fsm.json` —— **五端同一份向量**。
 *
 * 这份向量跨了两台状态机（有个用例的初始态同时带 room 与 call），
 * 所以驱动的是 Engine 总状态而不是单独的房间机。
 */
namespace {

/** kNowMs 是固定的「现在」。状态机不读时钟，时间由调用方喂——否则向量没法复现。 */
constexpr std::int64_t kNowMs = 1756876900000;

std::string text(const Json& value, const std::string& key) {
  const Json* found = value.find(key);
  return found != nullptr && found->isString() ? found->asString() : std::string();
}

MachineInput toInput(const Json& step) {
  if (const Json* act = step.find("act")) {
    const Json* args = act->find("args");
    return MachineInput::act(text(*act, "op"), args == nullptr ? Json::makeObject() : *args);
  }
  if (const Json* recv = step.find("recv")) {
    const Json* data = recv->find("data");
    return MachineInput::recv(text(*recv, "type"), data == nullptr ? Json::makeObject() : *data);
  }
  if (const Json* internal = step.find("internal")) {
    return MachineInput::internal(internal->asString());
  }
  imtest::fail("room_fsm.json", "一步里必须有 act / recv / internal 之一");
}

std::map<std::string, std::string> toStringMap(const Json* value) {
  std::map<std::string, std::string> out;
  if (value == nullptr) return out;
  for (const Json::Member& member : value->members()) {
    out[member.first] = member.second.asString();
  }
  return out;
}

EngineContext seed(const Json& testCase) {
  const Json* init = testCase.find("initial_state");
  EngineContext ctx;

  const std::string roomState = init == nullptr ? "idle" : text(*init, "room");
  CHECK_TRUE(imrtc::parseRoomState(roomState.empty() ? "idle" : roomState, ctx.room.state),
             "未知的 room 初始状态：" + roomState);
  ctx.room.roomId = "r-1";

  if (init != nullptr) {
    ctx.room.publish = toStringMap(init->find("publish"));
    // 向量里的初始 publish 用 cid 作键，这里补上 cid → track_id 的映射，
    // 否则 unpublish 找不到该把哪条标成 unpublishing。
    for (const auto& entry : ctx.room.publish) {
      ctx.room.publishTrackIds[entry.first] = "t-7";
    }
    ctx.room.subscribe = toStringMap(init->find("subscribe"));
    for (const auto& entry : ctx.room.subscribe) {
      ctx.room.remoteTracks[entry.first] = imrtc::RemoteTrack{"bob", "video", "p-1"};
    }

    const std::string callState = text(*init, "call");
    if (!callState.empty()) {
      CHECK_TRUE(imrtc::parseCallState(callState, ctx.call.state),
                 "未知的 call 初始状态：" + callState);
      ctx.call.callId = "call-1";
      ctx.call.roomId = "r-1";
      // 只有「已接通」才有时长起点；未接通的通话时长恒为 0（不变量 I1）。
      if (ctx.call.state == imrtc::CallState::Connected) ctx.call.connectedAtMs = kNowMs - 5000;
    }
  }
  return ctx;
}

Json actualSend(const std::vector<imrtc::OutgoingFrame>& frames) {
  Json out = Json::makeArray();
  for (const imrtc::OutgoingFrame& frame : frames) {
    Json item = Json::makeObject();
    item.set("type", Json::make(frame.type));
    item.set("data", frame.data);
    out.push(std::move(item));
  }
  return out;
}

Json actualEmit(const std::vector<imrtc::EmittedEvent>& events) {
  Json out = Json::makeArray();
  for (const imrtc::EmittedEvent& event : events) {
    Json item = Json::makeObject();
    item.set("cb", Json::make(event.cb));
    item.set("args", event.args);
    out.push(std::move(item));
  }
  return out;
}

Json wanted(const Json& step, const std::string& key) {
  const Json* found = step.find(key);
  return found == nullptr ? Json::makeArray() : *found;
}

/** describeMap 把记账渲染成可读文本，断言失败时一眼看出多了/少了哪一条。 */
std::string describeMap(const std::map<std::string, std::string>& map) {
  std::string text = "{";
  bool first = true;
  for (const auto& entry : map) {
    if (!first) text += ", ";
    first = false;
    text += entry.first + ":" + entry.second;
  }
  return text + "}";
}

/**
 * assertState 比对状态。
 *
 * `room` / `call` 按字符串比；**`publish` / `subscribe` 按全等比**——
 * 它们在向量里是完整写出来的，用子集比会让「多了一条没清掉的订阅」溜过去，
 * 而那正是最容易出的错。
 */
void assertState(const EngineContext& ctx, const Json& want, const std::string& label) {
  if (want.contains("room")) {
    CHECK_EQ(std::string(imrtc::roomStateName(ctx.room.state)), text(want, "room"),
             label + " 的 room");
  }
  if (want.contains("call")) {
    CHECK_EQ(std::string(imrtc::callStateName(ctx.call.state)), text(want, "call"),
             label + " 的 call");
  }
  if (const Json* publish = want.find("publish")) {
    const std::map<std::string, std::string> expected = toStringMap(publish);
    CHECK_EQ(describeMap(ctx.room.publish), describeMap(expected), label + " 的 publish");
  }
  if (const Json* subscribe = want.find("subscribe")) {
    const std::map<std::string, std::string> expected = toStringMap(subscribe);
    CHECK_EQ(describeMap(ctx.room.subscribe), describeMap(expected), label + " 的 subscribe");
  }
}

void runCase(const Json& testCase) {
  const std::string name = text(testCase, "name");
  EngineContext ctx = seed(testCase);

  std::size_t index = 0;
  for (const Json& step : testCase.find("steps")->items()) {
    ++index;
    const std::string label = name + " 第 " + std::to_string(index) + " 步";
    const EngineOutput result = imrtc::reduceEngine(ctx, toInput(step), kNowMs);
    ctx = result.state;

    imtest::expectSubset(actualSend(result.send), wanted(step, "send"), label + " 的 send");
    imtest::expectSubset(actualEmit(result.emit), wanted(step, "emit"), label + " 的 emit");

    if (const Json* wantState = step.find("state")) assertState(ctx, *wantState, label);
  }
}

}  // namespace

IMRTC_TEST(roomFsmVector, "room_fsm.json —— 房间与 Track 状态机（五端同一份向量）") {
  const Json vector = imtest::loadVector("room_fsm.json");
  CHECK_EQ(text(vector, "kind"), std::string("room_fsm"), "向量类型");
  CHECK_EQ(vector.find("version")->asInt(), std::int64_t{1}, "向量版本");

  const Json* cases = vector.find("cases");
  CHECK_TRUE(cases != nullptr && !cases->items().empty(), "cases 不能为空");
  for (const Json& testCase : cases->items()) runCase(testCase);
}

IMRTC_TEST(roomResubscribeAfterUnsubscribe,
           "房间机 —— 退订之后再订阅，发的必须是 room.subscribe 而不是换层") {
  imrtc::RoomContext ctx;
  ctx.state = imrtc::RoomState::Joined;
  ctx.roomId = "r-1";
  ctx.participantId = "p-1";

  const imrtc::RoomOutput dropped = imrtc::reduceRoomAct(
      ctx, "unsubscribe", imrtc::obj({{"track_id", Json::make("t-91")}}));
  /*
    **回归**：早先 unsubscribe 会无条件往 subscribe 表里塞一条 "unsubscribing"，
    哪怕这条轨道压根没订阅过。而 subscribe 只看「表里有没有这个键」，
    于是下一次订阅退化成 room.update_layer —— 服务端根本没在给这条流，
    换层是个空操作，格子就一直黑着，两边都不报错。
  */
  CHECK_TRUE(dropped.state.subscribe.find("t-91") == dropped.state.subscribe.end(),
             "没订阅过就不该凭空记一笔");

  const imrtc::RoomOutput again = imrtc::reduceRoomAct(
      dropped.state, "subscribe", imrtc::obj({{"track_id", Json::make("t-91")}}));
  CHECK_EQ(again.send.size(), std::size_t{1}, "要发一帧");
  CHECK_EQ(again.send[0].type, std::string(imrtc::frame::kRoomSubscribe), "是订阅，不是换层");

  // 订阅中途再退订、再订阅：那条 "unsubscribing" 也不算「已订阅」。
  const imrtc::RoomOutput mid = imrtc::reduceRoomAct(
      again.state, "unsubscribe", imrtc::obj({{"track_id", Json::make("t-91")}}));
  const imrtc::RoomOutput retry = imrtc::reduceRoomAct(
      mid.state, "subscribe", imrtc::obj({{"track_id", Json::make("t-91")}}));
  CHECK_EQ(retry.send[0].type, std::string(imrtc::frame::kRoomSubscribe),
           "退订还没结算完就重订，也得重新订阅");
}

IMRTC_TEST(roomResumeAfterDropWhileJoining,
           "房间机 —— join 还在飞的时候掉线：恢复后要重新 join，不许直接宣布 joined") {
  // 向量里没有这一路（room_fsm.json 的两条 reconnect 用例都从 joined 出发），
  // 但它在真机上是常事：进房请求刚发出去，地铁进隧道。
  imrtc::RoomContext ctx;
  ctx.state = imrtc::RoomState::Joining;
  ctx.roomId = "r-1";
  ctx.roomToken = "tk";
  // participant_id 还是空的 —— room.join.ok 从没到过。

  const imrtc::RoomOutput dropped =
      imrtc::reduceRoom(ctx, imrtc::MachineInput::internal("disconnected"));
  CHECK_TRUE(dropped.state.state == imrtc::RoomState::Joining,
             "joining 不能塌进 reconnecting，否则恢复时分不出这两路");

  const imrtc::RoomOutput resumed = imrtc::resumeRoom(dropped.state, true);
  /*
    **回归**：早先任何非 idle 状态都进 reconnecting，而 resumeRoom 看到 reconnecting
    就一律置成 joined。于是一个服务端从没把我们加进去的房间被宣布成「已进房」：
    participant_id 是空的，之后每一帧都换回 1201，而重新 join 又被「必须在 idle」
    挡回来——这个房间再也出不去，只能 logout。

    resumed=true 说的是「会话还在」，不是「成员关系还在」。
  */
  CHECK_EQ(resumed.send.size(), std::size_t{1}, "要重新发一条 room.join");
  CHECK_EQ(resumed.send[0].type, std::string(imrtc::frame::kRoomJoin), "发的是 room.join");
  CHECK_EQ(imrtc::str(resumed.send[0].data, "room_id"), std::string("r-1"), "还是原来那个房间");
  CHECK_TRUE(resumed.state.state == imrtc::RoomState::Joining, "回到 joining 等 join.ok");
}

IMRTC_TEST(roomFsmStates, "room_fsm.json —— C++ 侧的房间状态集合与向量一致") {
  const Json vector = imtest::loadVector("room_fsm.json");
  std::vector<std::string> allowed;
  for (const Json& state : vector.find("room_states")->items()) allowed.push_back(state.asString());

  CHECK_EQ(allowed.size(), std::size_t{5}, "向量声明的房间状态数");
  for (const std::string& state : allowed) {
    RoomState parsed = RoomState::Idle;
    CHECK_TRUE(imrtc::parseRoomState(state, parsed), "C++ 侧不认识房间状态 " + state);
  }
}
