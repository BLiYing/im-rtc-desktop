#include <string>
#include <vector>

#include "TestHarness.h"
#include "Vectors.h"
#include "imrtc/CallMachine.h"

using imrtc::CallContext;
using imrtc::CallOutput;
using imrtc::CallState;
using imrtc::Json;
using imrtc::MachineInput;

/**
 * 通话状态机跑 `call_fsm.json` —— **五端同一份向量**。
 *
 * 向量的铁律：**某步没写 send / emit 就是断言为空**。多抛一次 onCallEnd 会被抓到。
 */
namespace {

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
  imtest::fail("call_fsm.json", "一步里必须有 act / recv / internal 之一");
}

CallContext seedContext(const Json& testCase) {
  CallContext ctx;
  const std::string initial = text(testCase, "initial_state");
  CHECK_TRUE(imrtc::parseCallState(initial, ctx.state), "未知的 initial_state：" + initial);

  const std::string role = text(testCase, "role");
  ctx.role = (role == "caller" || role == "callee") ? role : "";

  if (const Json* context = testCase.find("context")) {
    ctx.callId = text(*context, "call_id");
    ctx.roomId = text(*context, "room_id");
    const Json* isGroup = context->find("is_group");
    ctx.isGroup = isGroup != nullptr && isGroup->isBool() && isGroup->asBool();
  }
  return ctx;
}

/** actualSend / actualEmit 把状态机的产物摊成向量那种 {type,data} / {cb,args} 形状。 */
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

/** wanted 取出向量里的期望；**省略即断言为空**——这条是向量的价值所在。 */
Json wanted(const Json& step, const std::string& key) {
  const Json* found = step.find(key);
  return found == nullptr ? Json::makeArray() : *found;
}

void runCase(const Json& testCase) {
  const std::string name = text(testCase, "name");
  CallContext ctx = seedContext(testCase);

  std::size_t index = 0;
  for (const Json& step : testCase.find("steps")->items()) {
    ++index;
    const std::string label = name + " 第 " + std::to_string(index) + " 步";
    const CallOutput result = imrtc::reduceCall(ctx, toInput(step));
    ctx = result.state;

    imtest::expectSubset(actualSend(result.send), wanted(step, "send"), label + " 的 send");
    imtest::expectSubset(actualEmit(result.emit), wanted(step, "emit"), label + " 的 emit");

    if (const Json* wantState = step.find("state")) {
      CHECK_EQ(std::string(imrtc::callStateName(ctx.state)), wantState->asString(),
               label + " 之后的状态");
    }
  }
}

}  // namespace

IMRTC_TEST(callFsmVector, "call_fsm.json —— 通话状态机（五端同一份向量）") {
  const Json vector = imtest::loadVector("call_fsm.json");
  CHECK_EQ(text(vector, "kind"), std::string("call_fsm"), "向量类型");
  CHECK_EQ(vector.find("version")->asInt(), std::int64_t{1}, "向量版本");

  const Json* cases = vector.find("cases");
  CHECK_TRUE(cases != nullptr && !cases->items().empty(), "cases 不能为空");
  for (const Json& testCase : cases->items()) runCase(testCase);
}

IMRTC_TEST(callFsmStates, "call_fsm.json —— 用例里出现的状态都在声明的集合内") {
  const Json vector = imtest::loadVector("call_fsm.json");
  std::vector<std::string> allowed;
  for (const Json& state : vector.find("states")->items()) allowed.push_back(state.asString());

  const auto isAllowed = [&allowed](const std::string& state) {
    return std::find(allowed.begin(), allowed.end(), state) != allowed.end();
  };

  for (const Json& testCase : vector.find("cases")->items()) {
    CHECK_TRUE(isAllowed(text(testCase, "initial_state")), "initial_state 越界");
    for (const Json& step : testCase.find("steps")->items()) {
      if (const Json* state = step.find("state")) {
        CHECK_TRUE(isAllowed(state->asString()), "state 越界：" + state->asString());
      }
    }
  }

  // 反过来也钉一次：C++ 侧的状态集合不许比向量多也不许少。
  CHECK_EQ(allowed.size(), std::size_t{6}, "向量声明的状态数");
  for (const std::string& state : allowed) {
    CallState parsed = CallState::Idle;
    CHECK_TRUE(imrtc::parseCallState(state, parsed), "C++ 侧不认识状态 " + state);
  }
}

namespace {

/**
 * HOST_INTEGRATION_DESIGN §3.3 的本地校验：不是五仓共用的向量（只有桌面/客户端要
 * 在上线路之前自己挡），所以在这里单独钉，不进 call_fsm.json。
 */
Json callArgs(const std::string& chatGroupId, const std::string& userData) {
  Json args = Json::makeObject();
  Json calleeIds = Json::makeArray();
  calleeIds.push(Json::make(std::string("bob")));
  args.set("callee_ids", calleeIds);
  args.set("media_type", Json::make(std::string("audio")));
  args.set("is_group", Json::make(false));
  if (!chatGroupId.empty()) args.set("chat_group_id", Json::make(chatGroupId));
  if (!userData.empty()) args.set("user_data", Json::make(userData));
  return args;
}

void expectLocallyRejected(const Json& args, const std::string& label) {
  const CallContext ctx;
  const CallOutput result = imrtc::reduceCall(ctx, MachineInput::act("call", args));

  CHECK_TRUE(result.send.empty(), label + "：不许上线路");
  CHECK_EQ(result.emit.size(), std::size_t{2}, label + "：onError + onCallEnd 两条");
  if (result.emit.size() == 2) {
    CHECK_EQ(result.emit[0].cb, std::string("onError"), label + " 第一条回调名");
    CHECK_EQ(imrtc::num(result.emit[0].args, "code"), std::int64_t{1004}, label + " 错误码");
    CHECK_EQ(result.emit[1].cb, std::string("onCallEnd"), label + " 第二条回调名");
    CHECK_EQ(text(result.emit[1].args, "reason"), std::string("error"), label + " reason");
    CHECK_EQ(imrtc::num(result.emit[1].args, "duration_sec"), std::int64_t{0}, label + " 时长");
  }
  CHECK_TRUE(result.state.state == CallState::Idle, label + "：不该转移状态");
}

}  // namespace

IMRTC_TEST(callLocalRejectsOversizedChatGroupId,
           "call() —— chat_group_id 超 64 字节本地先拦：onError(1004)+onCallEnd(error)，"
           "不上线路（HOST_INTEGRATION_DESIGN §3.3）") {
  expectLocallyRejected(callArgs(std::string(65, 'g'), ""), "超长 chat_group_id");
}

IMRTC_TEST(callLocalRejectsWhitespaceChatGroupId,
           "call() —— chat_group_id 含空白/换行本地先拦") {
  expectLocallyRejected(callArgs("g 42", ""), "含空格");
  expectLocallyRejected(callArgs("g\n42", ""), "含换行");
}

IMRTC_TEST(callLocalRejectsOversizedUserData,
           "call() —— user_data 超 4096 字节本地先拦") {
  expectLocallyRejected(callArgs("", std::string(4097, 'x')), "超长 user_data");
}

IMRTC_TEST(callLocalAcceptsBoundaryValues,
           "call() —— 恰好卡在边界（64 字节 / 4096 字节）不该被拦") {
  const CallContext ctx;
  const Json args = callArgs(std::string(64, 'g'), std::string(4096, 'x'));
  const CallOutput result = imrtc::reduceCall(ctx, MachineInput::act("call", args));
  CHECK_EQ(result.send.size(), std::size_t{1}, "边界值应当正常发出 call.invite");
  CHECK_TRUE(result.state.state == CallState::Inviting, "边界值应当正常进入 inviting");
}

namespace {

/** incomingData 拼一条 call.incoming；inviter 为空表示「旧服务端压根不带这个字段」。 */
Json incomingData(const std::string& inviter) {
  Json data = Json::makeObject();
  data.set("call_id", Json::make(std::string("call-1")));
  data.set("room_id", Json::make(std::string("r-1")));
  data.set("caller", Json::make(std::string("alice")));
  data.set("media_type", Json::make(std::string("audio")));
  data.set("is_group", Json::make(true));
  if (!inviter.empty()) data.set("inviter", Json::make(inviter));
  return data;
}

/** invitedBy 收一条 call.incoming，返回抛给宿主的 inviter。 */
std::string invitedBy(const std::string& inviter, const std::string& label) {
  const CallContext ctx;
  const CallOutput result =
      imrtc::reduceCall(ctx, MachineInput::recv("call.incoming", incomingData(inviter)));
  CHECK_EQ(result.emit.size(), std::size_t{1}, label + "：该抛且只抛一条 onCallReceived");
  CHECK_EQ(result.emit[0].cb, std::string("onCallReceived"), label + "：回调名");
  CHECK_EQ(text(result.emit[0].args, "caller"), std::string("alice"),
           label + "：caller 恒为发起人，不许被 inviter 顶掉");
  return text(result.emit[0].args, "inviter");
}

}  // namespace

IMRTC_TEST(incomingCarriesInviter,
           "call.incoming —— inviter 是「谁把你拉进来的」，缺省回落 caller（协议 §4.1，2026-09-16）") {
  // invite_more 拉进来的人：caller 仍是最初的发起人，inviter 是按下「添加成员」的那个。
  CHECK_EQ(invitedBy("carol", "带 inviter"), std::string("carol"), "带 inviter 时原样抛给宿主");
  // 旧服务端不带这个字段：回落成 caller，宿主永远拿得到一个非空的人。
  CHECK_EQ(invitedBy("", "不带 inviter"), std::string("alice"), "缺省时回落 caller");
}
