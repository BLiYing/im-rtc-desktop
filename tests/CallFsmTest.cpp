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
