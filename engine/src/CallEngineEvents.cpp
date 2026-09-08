#include <string>
#include <vector>

#include "imrtc/CallEngine.h"
#include "imrtc/MachineTypes.h"

namespace imrtc {
namespace {

/**
 * 状态机产出的 `EmittedEvent`（cb 名 + 线路形状 args）翻译成观察者的方法调用。
 *
 * 与 CallEngine.cpp 拆开是体量红线（CONVENTIONS §3），也因为这是一张**纯映射表**：
 * 加一个回调只动这里与 CallEngineObserver.h，不碰门面的控制流。
 *
 * args 的键一律是**协议的 snake_case 名**——它们与一致性向量、与另外四端同名，
 * 换成 C++ 惯用写法会让这张表和向量之间多一层没人测的翻译。
 */

std::vector<std::string> stringsOf(const Json& args, const std::string& key) {
  return strArray(args, key);
}

std::vector<Speaker> speakersOf(const Json& args) {
  std::vector<Speaker> speakers;
  const Json* list = args.find("speakers");
  if (list == nullptr) return speakers;
  for (const Json& item : list->items()) {
    speakers.push_back(Speaker{str(item, "uid"), str(item, "participant_id"),
                               num(item, "volume")});
  }
  return speakers;
}

std::vector<QualityEntry> entriesOf(const Json& args) {
  std::vector<QualityEntry> entries;
  const Json* list = args.find("entries");
  if (list == nullptr) return entries;
  for (const Json& item : list->items()) {
    entries.push_back(QualityEntry{str(item, "uid"), str(item, "participant_id"),
                                   num(item, "level")});
  }
  return entries;
}

}  // namespace

bool dispatchObserverEvent(CallEngineObserver& out, const EmittedEvent& event) {
  CallEngineObserver* const target = &out;
  const Json& args = event.args;
  const std::string& cb = event.cb;

  if (cb == "onConnected") {
    target->onConnected(str(args, "session_id"), boolean(args, "resumed"));
  } else if (cb == "onDisconnected") {
    target->onDisconnected();
  } else if (cb == "onKickedOut") {
    /*
      原因**不是状态机给的**，是门面在派发前塞进 args 的（见 CallEngine::emitEvent）。

      状态机这条 emit 的 args 在一致性向量里就是 `{}`（room_fsm.json 的
      `ws_closed_4403`）——它只关心状态怎么走，而「为什么被踢」它压根不知道：
      同一个内部事件既被 4403 复用，也被「鉴权失败到顶」复用。取不到就兜底成
      TakenOver，那是 4403 的含义。
    */
    target->onKickedOut(parseKickedReason(str(args, "reason")));
  } else if (cb == "onError") {
    // for_type 是「哪一帧没成」。状态机产出的 onError 不带它，本地补的那条
    // （CallEngine::failLocally）带——取不到就是空串，与原先的行为一致。
    target->onError(static_cast<std::int32_t>(num(args, "code")), str(args, "name"),
                    str(args, "for_type"));
  } else if (cb == "onCallReceived") {
    target->onCallReceived(CallInvite{str(args, "call_id"), str(args, "caller"),
                                      stringsOf(args, "callee_ids"), str(args, "media_type"),
                                      boolean(args, "is_group")});
  } else if (cb == "onCallBegin") {
    target->onCallBegin(CallBegin{str(args, "call_id"), str(args, "room_id"),
                                  str(args, "media_type"), boolean(args, "is_group"),
                                  str(args, "role")});
  } else if (cb == "onCallEnd") {
    target->onCallEnd(CallEnd{str(args, "call_id"), str(args, "reason"),
                              num(args, "duration_sec"), str(args, "ended_by")});
  } else if (cb == "onCallMissed") {
    target->onCallMissed(
        CallMissed{str(args, "call_id"), str(args, "caller"), str(args, "reason")});
  } else if (cb == "onCallCancelled") {
    target->onCallCancelled(str(args, "by"));
  } else if (cb == "onCallRejected") {
    target->onCallRejected(str(args, "uid"));
  } else if (cb == "onCallBusy") {
    target->onCallBusy(str(args, "uid"));
  } else if (cb == "onCallNoAnswer") {
    target->onCallNoAnswer(str(args, "uid"));
  } else if (cb == "onHandledOnOtherDevice") {
    target->onHandledOnOtherDevice(str(args, "call_id"), str(args, "action"));
  } else if (cb == "onUserEnter") {
    target->onUserEnter(str(args, "uid"));
  } else if (cb == "onUserLeave") {
    target->onUserLeave(str(args, "uid"));
  } else if (cb == "onUserAccept") {
    target->onUserAccept(str(args, "uid"));
  } else if (cb == "onUserReject") {
    target->onUserReject(str(args, "uid"));
  } else if (cb == "onUserNoResponse") {
    target->onUserNoResponse(str(args, "uid"));
  } else if (cb == "onUserAudioAvailable") {
    target->onUserAudioAvailable(str(args, "uid"), boolean(args, "available"));
  } else if (cb == "onUserVideoAvailable") {
    target->onUserVideoAvailable(str(args, "uid"), boolean(args, "available"));
  } else if (cb == "onActiveSpeakers") {
    target->onActiveSpeakers(speakersOf(args));
  } else if (cb == "onNetworkQuality") {
    target->onNetworkQuality(entriesOf(args));
  } else if (cb == "onRoomJoined") {
    target->onRoomJoined(str(args, "room_id"));
  } else if (cb == "onRoomLeft") {
    target->onRoomLeft(str(args, "room_id"));
  } else if (cb == "onRoomClosed") {
    target->onRoomClosed(str(args, "room_id"), str(args, "reason"));
  } else {
    // 状态机抛了一个观察者表里没有的回调名——**加回调时两处要一起改**。
    // 返回 false 让 tests/CallEngineTest.cpp 的那条覆盖用例把漏网的抓出来，
    // 否则这种漏改是静默的：宿主永远收不到那个事件，而且没人报错。
    return false;
  }
  return true;
}

void CallEngine::emitEvent(const EmittedEvent& event) {
  const std::shared_ptr<CallEngineObserver> target = observer();
  // 观察者已经没了：宿主放手即注销，这里静默跳过而不是崩（CONVENTIONS §5）。
  if (!target) return;
  if (event.cb == "onKickedOut") {
    // 把连接层给的原因补进 args 再派发。**不改状态机**：那条 emit 的 args 是
    // 五仓共用的一致性向量钉死的 `{}`，往里加字段等于单方面改五端契约。
    EmittedEvent enriched = event;
    enriched.args.set("reason", Json::make(kickedReasonName(kickedReason_)));
    dispatchObserverEvent(*target, enriched);
    return;
  }
  dispatchObserverEvent(*target, event);
}

}  // namespace imrtc
