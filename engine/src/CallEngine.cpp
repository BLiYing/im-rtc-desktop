#include "imrtc/CallEngine.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "imrtc/Errors.h"
#include "imrtc/Log.h"
#include "imrtc/MachineTypes.h"
#include "imrtc/Reasons.h"
#include "imrtc/RoomPaging.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/** kEmptyString 给 sessionId() 在没有连接时返回。 */
const std::string kEmptyString;

/** containsSelf：`uid` 已知且出现在名单里。没登录（uid 为空）时不拦，交给没连接那条路。 */
bool containsSelf(const std::string& uid, const std::vector<std::string>& calleeIds) {
  return !uid.empty() && std::find(calleeIds.begin(), calleeIds.end(), uid) != calleeIds.end();
}

/** locallyRejectedCallEnd 是「call() 上线路之前就被拒」给界面的收场信号，同 CallMachine 的 localCallRejected。 */
EmittedEvent locallyRejectedCallEnd() {
  return eventOf("onCallEnd", obj({{"call_id", Json::make(std::string())},
                                   {"reason", Json::make(reason::kError)},
                                   {"duration_sec", Json::make(std::int64_t{0})},
                                   {"ended_by", Json::make(std::string())}}));
}

/** stringArray 把 vector<string> 装成线路形状的 Json 数组。 */
Json stringArray(const std::vector<std::string>& values) {
  Json array = Json::makeArray();
  for (const std::string& value : values) array.push(Json::make(value));
  return array;
}

}  // namespace

std::int64_t steadyClock() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::int64_t systemClock() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

CallEngine::CallEngine(CallEngineOptions options) : options_(std::move(options)) {
  if (!options_.clock) options_.clock = &steadyClock;
  if (!options_.wallClock) options_.wallClock = &systemClock;
  if (!options_.mediaAdapter) return;

  MediaPlane::Deps deps;
  deps.send = [this](const std::string& type, const std::string& reqId, const Json& data) {
    // **返回「这一帧到底有没有走出去」**：上行协商闸门要靠它才关得住。
    // 发不出去却当成发出去了，闸门就永远停在「有一个在飞」，上行从此协商不出去
    // 而日志里一切正常——正是这道闸要防的那种病。
    if (!connection_) return false;
    const std::int64_t now = options_.clock();
    // 媒体面产出的帧里，pub offer 是**我们发起的请求**，其余（answer / 候选）是应答
    // 或单向通知（§3.3）。这里的判断与 sendOne 里那条是同一条规则。
    if (type == frame::kRoomOffer && str(data, "pc") == "pub") {
      const std::string offerType = type;
      return connection_->request(offerType, data, now,
                                  [this, offerType, data](const RequestResult& result) {
                                    if (!result.ok) {
                                      onRequestFailed(offerType, data, result, nullptr);
                                      return;
                                    }
                                    handleIncoming(result.envelope.type, "", result.data);
                                  });
    }
    connection_->sendFrame(type, reqId, data, now);
    return true;
  };
  deps.dispatchInternal = [this](const std::string& name) {
    apply(MachineInput::internal(name), "");
  };
  deps.dispatchAct = [this](const std::string& op, const Json& args) {
    apply(MachineInput::act(op, args), "");
  };
  deps.reportError = [this](std::int32_t code, const std::string& forType) {
    if (tearingDown_) return;
    /*
      也走队列。媒体面的错误多半是异步回来的（那时 sendDepth_ 是 0，就地抛），
      但**同步的适配器会让它落在发帧循环里**：sendOne → fillSdp → createPubOffer
      当场失败 → 这里。就地抛的话又是一次「错误跑到它所属的那次转移的事件前面」。
      让所有抛给宿主的事件都走同一条路，比在每个入口各自判断可靠。
    */
    Json args = Json::makeObject();
    args.set("code", Json::make(static_cast<std::int64_t>(code)));
    args.set("name", Json::make(errorName(code)));
    args.set("for_type", Json::make(forType));
    emitOrDefer(EmittedEvent{"onError", args});
  };
  deps.uidOf = [this](const std::string& trackId) {
    const auto it = context_.room.remoteTracks.find(trackId);
    return it == context_.room.remoteTracks.end() ? std::string() : it->second.uid;
  };
  deps.trackIdOfCid = [this](const std::string& cid) {
    const auto it = context_.room.publishTrackIds.find(cid);
    return it == context_.room.publishTrackIds.end() ? std::string() : it->second;
  };
  deps.onFirstVideoFrame = [](const std::string&, const std::string&) {
    // onFirstVideoFrame 还没进 CallEngineObserver（§7.5 里有，但要等渲染路径定下来
    // 一起加，否则宿主拿到一个自己没法用的事件）。留着接口，第五刀补。
  };

  media_.reset(new MediaPlane(options_.mediaAdapter, std::move(deps)));
}

// 析构在 CallEngineRequests.cpp：它要结算还没交出的调用结果，得看得见 Settlement 的定义。

void CallEngine::setObserver(std::weak_ptr<CallEngineObserver> observer) {
  observer_ = std::move(observer);
}

void CallEngine::call(const std::vector<std::string>& calleeIds, const std::string& mediaType,
                      bool isGroup, ActionCompletion done) {
  call(calleeIds, mediaType, isGroup, CallOptions{}, std::move(done));
}

void CallEngine::call(const std::vector<std::string>& calleeIds, const std::string& mediaType,
                      bool isGroup, const CallOptions& options, ActionCompletion done) {
  /*
    **这道本地关卡只在 `Idle` 时抢在状态机前面拦**：`onCallEnd(error)` 假定界面刚乐观地进了
    「正在呼叫…」。不是 `Idle`（这通 `call()` 其实是在另一通电话进行中时误调的，比如名单里
    误含自己）时抢先拒掉只会给**正在进行的**那通电话发一条假的 `onCallEnd`，把它错杀——
    让状态机去拒，按 `CallMachine::startCall` 正常收成 `2005`，不碰当前那通。
  */
  if (context_.call.state == CallState::Idle && containsSelf(uid_, calleeIds)) {
    /*
      **名单里不能有自己**（HOST_INTEGRATION_DESIGN §3.3，Web `callGuards.ts` 的 `rejectsSelf`）。
      服务端会回 1004，但界面这时已经乐观地进了「正在呼叫…」，那条 1004 没头没尾。
      出口与群号 / user_data 超限同一个：先 `onCallEnd(error)` 给界面收场，再把 1004 交给调用方。
    */
    log(LogLevel::Warn, "呼叫名单里含自己，已就地拒掉", {{logfield::kUid, uid_}});
    EngineOutput ended{context_, {}, {locallyRejectedCallEnd()}, LocalReject{}};
    dispatchOutput(std::move(ended), "");
    rejectLocally(std::move(done), frame::kCallInvite);
    return;
  }
  Json args = Json::makeObject();
  args.set("callee_ids", stringArray(calleeIds));
  args.set("media_type", Json::make(mediaType));
  args.set("is_group", Json::make(isGroup));
  // 三个都是可选项：**真的省略**才对——状态机据此决定要不要把它们塞进 call.invite。
  if (!options.chatGroupId.empty()) args.set("chat_group_id", Json::make(options.chatGroupId));
  if (!options.userData.empty()) args.set("user_data", Json::make(options.userData));
  if (options.timeoutSec > 0) args.set("timeout_sec", Json::make(options.timeoutSec));
  request(MachineInput::act("call", args), std::move(done), "call_id");
}

void CallEngine::accept(ActionCompletion done) { request(MachineInput::act("accept"), std::move(done)); }
void CallEngine::reject(ActionCompletion done) { request(MachineInput::act("reject"), std::move(done)); }
void CallEngine::cancel(ActionCompletion done) { request(MachineInput::act("cancel"), std::move(done)); }
void CallEngine::hangup(ActionCompletion done) { request(MachineInput::act("hangup"), std::move(done)); }

void CallEngine::inviteMore(const std::vector<std::string>& calleeIds, ActionCompletion done) {
  // 同 call()：本地关卡只在「状态机本来就会受理这次 inviteMore」（Connected/Connecting）时
  // 抢在前面拦，不然不在通话里调 inviteMore 时名单含自己会被误判成 1004，
  // 盖过更准确的 2005（不在通话中）。加人不动通话，所以不抛 onCallEnd。
  const bool inCall = context_.call.state == CallState::Connected ||
                       context_.call.state == CallState::Connecting;
  if (inCall && containsSelf(uid_, calleeIds)) {
    log(LogLevel::Warn, "加人名单里含自己，已就地拒掉", {{logfield::kUid, uid_}});
    rejectLocally(std::move(done), frame::kCallInviteMore);
    return;
  }
  Json args = Json::makeObject();
  args.set("callee_ids", stringArray(calleeIds));
  request(MachineInput::act("invite_more", args), std::move(done));
}

void CallEngine::joinCall(const std::string& callId, ActionCompletion done) {
  Json args = Json::makeObject();
  args.set("call_id", Json::make(callId));
  request(MachineInput::act("join_call", args), std::move(done));
}

void CallEngine::joinRoom(const std::string& roomId, const std::string& roomToken,
                          ActionCompletion done) {
  Json args = Json::makeObject();
  args.set("room_id", Json::make(roomId));
  args.set("room_token", Json::make(roomToken));
  request(MachineInput::act("join", args), std::move(done));
}

void CallEngine::leaveRoom(ActionCompletion done) { request(MachineInput::act("leave"), std::move(done)); }

void CallEngine::setRemoteLayer(const std::string& uid, const std::string& layer) {
  /*
    对**那个人的每一条视频轨**都报一遍，不是只报第一条。

    现在一个人只有一条摄像头轨，但屏幕共享一落地就是两条，那时「只改了一条、
    另一条还停在 m」是个看不出来的 bug——画面是对的，只是多花了带宽。
    Web 端（packages/call-engine/src/engine.ts）也是这么循环的，两端保持一致。

    音频轨不参与：分层只对视频有意义（§3.5）。

    **先收集再发**：`apply()` 会整个换掉 `context_`（状态机是不可变风格，
    每次转移产出一份新的 RoomContext），在 `remoteTracks` 上边遍历边 apply
    就是拿着已经失效的迭代器走——ASan 抓得到，但那时已经是线上崩溃了。
  */
  std::vector<std::string> trackIds;
  for (const auto& entry : context_.room.remoteTracks) {
    if (entry.second.uid == uid && entry.second.kind == "video") trackIds.push_back(entry.first);
  }

  for (const std::string& trackId : trackIds) {
    Json args = Json::makeObject();
    args.set("track_id", Json::make(trackId));
    args.set("max_layer", Json::make(layer));
    // 提示类没有结果：不传回调，失败（含本地拒绝）退回 onError（ACTION_RESULT_DESIGN D3）。
    request(MachineInput::act("update_layer", args), ActionCompletion{});
  }
}

void CallEngine::notifyMediaReady() { apply(MachineInput::internal("media_ready"), ""); }

void CallEngine::tick() {
  if (media_) media_->poll();
  if (http_) http_->poll();
  if (connection_) connection_->tick(options_.clock());
  fireExpiredUnsubscribes();
}

/**
 * fireExpiredUnsubscribes 把到点的翻页退订喂回状态机（RoomPaging.h）。
 *
 * **先收集再喂**：`apply()` 会整个换掉 `context_`，而那一轮又会回来对账
 * `unsubscribeDeadlines_`——边遍历边改是未定义行为。
 */
void CallEngine::fireExpiredUnsubscribes() {
  if (unsubscribeDeadlines_.empty()) return;
  const std::int64_t now = options_.clock();

  std::vector<std::string> due;
  for (const auto& entry : unsubscribeDeadlines_) {
    if (entry.second <= now) due.push_back(entry.first);
  }
  for (const std::string& trackId : due) {
    unsubscribeDeadlines_.erase(trackId);
    Json args = Json::makeObject();
    args.set("track_id", Json::make(trackId));
    apply(MachineInput::internal("unsubscribe_hysteresis_elapsed", args), "");
  }
}

/** syncUnsubscribeDeadlines 让截止时刻表与待退订清单一致。每轮状态推进后调一次。 */
void CallEngine::syncUnsubscribeDeadlines() {
  const std::vector<std::string>& pending = context_.room.pendingUnsubscribe;
  for (auto it = unsubscribeDeadlines_.begin(); it != unsubscribeDeadlines_.end();) {
    it = std::find(pending.begin(), pending.end(), it->first) == pending.end()
             ? unsubscribeDeadlines_.erase(it)
             : std::next(it);
  }
  const std::int64_t deadline = options_.clock() + kUnsubscribeHysteresisMs;
  for (const std::string& trackId : pending) {
    unsubscribeDeadlines_.emplace(trackId, deadline);
  }
}

void CallEngine::probeMicrophone(VoidCompletion done) {
  if (!options_.mediaAdapter) {
    if (done) done(false, codeValue(ErrorCode::DeviceNotFound));
    return;
  }
  options_.mediaAdapter->probeMicrophone(std::move(done));
}

void CallEngine::startLocalPreview(TrackCompletion done) {
  if (!options_.mediaAdapter) {
    if (done) done(false, LocalTrack{}, codeValue(ErrorCode::DeviceNotFound));
    return;
  }
  options_.mediaAdapter->startLocalPreview(std::move(done));
}

void CallEngine::openMic() {
  if (media_) media_->setMuted(MediaKind::Audio, false);
}
void CallEngine::closeMic() {
  if (media_) media_->setMuted(MediaKind::Audio, true);
}
void CallEngine::openCamera() {
  if (media_) media_->setMuted(MediaKind::Video, false);
}
void CallEngine::closeCamera() {
  if (media_) media_->setMuted(MediaKind::Video, true);
}

void CallEngine::attachView(const std::string& uid, void* nativeHandle) {
  if (!options_.mediaAdapter) return;
  const std::string trackId = videoTrackOf(uid);
  // 挂一个还不存在的轨道不是错误：宿主在 onUserEnter 就把格子建好是最自然的写法，
  // 而那个人的视频轨可能几百毫秒后才发布。轨道到了再挂由宿主重调一次。
  if (trackId.empty()) return;
  options_.mediaAdapter->attachView(trackId, nativeHandle);
}

void CallEngine::attachLocalView(void* nativeHandle) {
  // 本端预览不依赖任何轨道：摄像头没开也能先把窗口挂上，开了就有画面。
  if (options_.mediaAdapter) options_.mediaAdapter->attachLocalView(nativeHandle);
}

std::string CallEngine::videoTrackOf(const std::string& uid) const {
  for (const auto& entry : context_.room.remoteTracks) {
    if (entry.second.uid == uid && entry.second.kind == "video") return entry.first;
  }
  return {};
}

void CallEngine::reactToEvents(const std::vector<EmittedEvent>& events) {
  if (!media_) return;
  for (const EmittedEvent& event : events) {
    if (event.cb == "onRoomJoined") {
      // 语音通话不开摄像头：协议上 media_type 只在 call.invite 时定死，
      // 而拍板 §11-10 说语音通话里**根本没有**摄像头按钮。
      media_->onRoomJoined(context_.call.mediaType == "video");
    } else if (event.cb == "onCallEnd" || event.cb == "onRoomLeft" ||
               event.cb == "onRoomClosed" || event.cb == "onKickedOut") {
      // 一轮结束就重建 PC：它是**跟着房间走**的，不重建的话上一轮的 transceiver
      // 还挂着，下一轮的 offer 会多出几条服务端不认识的 m-line（表现为黑屏）。
      media_->reset();
    }
  }
}

ConnectionState CallEngine::connectionState() const {
  return connection_ ? connection_->state() : ConnectionState::Idle;
}

const std::string& CallEngine::sessionId() const {
  return connection_ ? connection_->sessionId() : kEmptyString;
}

void CallEngine::handleIncoming(const std::string& type, const std::string& reqId,
                                const Json& data) {
  /*
    **媒体面先看一眼**，理由有两个：
    - 服务端的 sub offer 要在状态机产出那条空 SDP 的 room.answer **之前**被记下来，
      否则轮到填 SDP 时手里没有 offer；
    - pub offer 的应答（room.answer，没有 .ok）要落到 pub PC 上才算协商完成。
  */
  // 房间已经收场（forceEnd 之后）迟到的候选 / SDP 不交给媒体面，否则它会凭空再造一对 PC。
  if (media_ && context_.room.state != RoomState::Idle) media_->onSignalingFrame(type, reqId, data);
  // **非请求帧的应答要回显对方的 req_id**（§3.3）。状态机不记 req_id，由这里带上。
  apply(MachineInput::recv(type, data), reqId);
}

void CallEngine::apply(const MachineInput& input, const std::string& replyReqId) {
  dispatchOutput(reduceEngine(context_, input, options_.clock()), replyReqId);
}

/**
 * dispatchOutput 把一次状态转移的产物落地：换状态 → 发帧 → 抛事件。
 *
 * # 三条顺序，各有各的理由
 *
 * **先发帧再抛回调**：回调里宿主很可能立刻再调 Engine（比如 onCallBegin 里就开麦），
 * 那时状态已经是新的、该发的帧也已经在路上，不会出现「回调看到的状态比线路超前」。
 *
 * **先把事件抛给宿主，再让媒体面动**。反过来的话，采集失败的 onError 会跑到
 * onRoomJoined 前面——宿主还不知道自己进了房，就先收到一条「麦克风被拒」，
 * 界面上没有任何上下文可以挂这条错误。
 * （「媒体面动得晚了会不会影响宿主在 onRoomJoined 里调 openCamera」不成立：
 * 采集本来就是异步的，那时候轨道无论如何还没到手。）
 *
 * **重入期间产生的事件要等外层抛完再放**——这一条是上面那条「先发帧」的代价。
 * 发帧可能就地失败（`sendOne` → `failLocally` → `apply`），于是内层跑完了整个
 * dispatchOutput、事件全抛了，而**外层的事件一条都还没抛**。`call.connected` 那一步
 * 同时产出 onCallBegin 与一帧 room.join：room.join 发不出去时，宿主先收到
 * onRoomLeft、再收到 onCallBegin，生命周期是倒的，界面拿它没法收场。
 *
 * 正解不是把两个循环调个头（那会毁掉上面第一条），而是让重入的事件排队：
 * 发帧循环期间 `sendDepth_ > 0`，此时产生的事件一律进 `deferredEmits_`；
 * 回到最外层后先抛自己的，再按产生顺序放队列里的。于是顺序恢复成
 * onCallBegin → onError → onRoomLeft。
 *
 * **宿主在回调里回调进来不受影响**：那时 `sendDepth_` 已经归零，是一次新的最外层
 * 派发，事件照常就地抛出——它本来就该是同步可见的。
 */
void CallEngine::dispatchOutput(EngineOutput output, const std::string& replyReqId,
                                const std::shared_ptr<Settlement>& settlement) {
  context_ = std::move(output.state);
  // 翻页退订的截止时刻**每轮对账一次**，不在各条来路上各记各删（见 syncUnsubscribeDeadlines）。
  syncUnsubscribeDeadlines();

  ++sendDepth_;
  for (const OutgoingFrame& frame : output.send) sendOne(frame, replyReqId, settlement);
  --sendDepth_;

  if (sendDepth_ > 0) {
    // 自己是内层：把事件交给外层排队，等它抛完自己的再轮到这些。
    deferredEmits_.insert(deferredEmits_.end(), output.emit.begin(), output.emit.end());
    return;
  }

  emitAll(output.emit);
  /*
    再放重入期间攒下的。每一批都先搬到局部再抛：抛的过程中宿主可能回调进来，
    而那条路上的失败会往 deferredEmits_ 里继续追加——直接迭代成员容器会边遍历边扩容。
    循环到空为止，保证一条都不会漏在队列里过夜。

    **调用结果排在事件之后**（ACTION_RESULT_DESIGN：状态事件先于结果）：宿主在结果回调里
    看到的状态已经是收场之后的。
  */
  while (!deferredEmits_.empty() || !deferredResults_.empty()) {
    if (!deferredEmits_.empty()) {
      std::vector<EmittedEvent> batch;
      batch.swap(deferredEmits_);
      emitAll(batch);
      continue;
    }
    std::vector<std::pair<std::shared_ptr<Settlement>, ActionResult>> results;
    results.swap(deferredResults_);
    for (auto& entry : results) deliverOrDefer(entry.first, std::move(entry.second));
  }
}

void CallEngine::emitAll(const std::vector<EmittedEvent>& events) {
  for (const EmittedEvent& event : events) {
    // 本端进这通电话的那一刻：forceEnd 本地算时长用它，中途被拉进来的人不算上整通的时长。
    if (event.cb == "onCallBegin") callStartedAtMs_ = options_.clock();
    if (event.cb == "onCallEnd") callStartedAtMs_ = 0;
    emitEvent(event);
  }
  reactToEvents(events);
}

void CallEngine::emitOrDefer(EmittedEvent event) {
  if (sendDepth_ > 0) {
    deferredEmits_.push_back(std::move(event));
    return;
  }
  emitEvent(event);
}

}  // namespace imrtc
