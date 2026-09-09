#include "imrtc/CallEngine.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "imrtc/Errors.h"
#include "imrtc/MachineTypes.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/** kEmptyString 给 sessionId() 在没有连接时返回。 */
const std::string kEmptyString;

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
                                  [this, offerType](const RequestResult& result) {
                                    if (!result.ok) {
                                      onRequestFailed(offerType, result);
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

CallEngine::~CallEngine() = default;

void CallEngine::setObserver(std::weak_ptr<CallEngineObserver> observer) {
  observer_ = std::move(observer);
}

void CallEngine::call(const std::vector<std::string>& calleeIds, const std::string& mediaType,
                      bool isGroup) {
  Json args = Json::makeObject();
  args.set("callee_ids", stringArray(calleeIds));
  args.set("media_type", Json::make(mediaType));
  args.set("is_group", Json::make(isGroup));
  apply(MachineInput::act("call", args), "");
}

void CallEngine::accept() { apply(MachineInput::act("accept"), ""); }
void CallEngine::reject() { apply(MachineInput::act("reject"), ""); }
void CallEngine::cancel() { apply(MachineInput::act("cancel"), ""); }
void CallEngine::hangup() { apply(MachineInput::act("hangup"), ""); }

void CallEngine::inviteMore(const std::vector<std::string>& calleeIds) {
  Json args = Json::makeObject();
  args.set("callee_ids", stringArray(calleeIds));
  apply(MachineInput::act("invite_more", args), "");
}

void CallEngine::joinCall(const std::string& callId) {
  Json args = Json::makeObject();
  args.set("call_id", Json::make(callId));
  apply(MachineInput::act("join_call", args), "");
}

void CallEngine::joinRoom(const std::string& roomId, const std::string& roomToken) {
  Json args = Json::makeObject();
  args.set("room_id", Json::make(roomId));
  args.set("room_token", Json::make(roomToken));
  apply(MachineInput::act("join", args), "");
}

void CallEngine::leaveRoom() { apply(MachineInput::act("leave"), ""); }

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
    apply(MachineInput::act("update_layer", args), "");
  }
}

void CallEngine::notifyMediaReady() { apply(MachineInput::internal("media_ready"), ""); }

void CallEngine::tick() {
  if (media_) media_->poll();
  if (connection_) connection_->tick(options_.clock());
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
  if (media_) media_->onSignalingFrame(type, reqId, data);
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
void CallEngine::dispatchOutput(EngineOutput output, const std::string& replyReqId) {
  context_ = std::move(output.state);

  ++sendDepth_;
  for (const OutgoingFrame& frame : output.send) sendOne(frame, replyReqId);
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
  */
  while (!deferredEmits_.empty()) {
    std::vector<EmittedEvent> batch;
    batch.swap(deferredEmits_);
    emitAll(batch);
  }
}

void CallEngine::emitAll(const std::vector<EmittedEvent>& events) {
  for (const EmittedEvent& event : events) emitEvent(event);
  reactToEvents(events);
}

void CallEngine::emitOrDefer(EmittedEvent event) {
  if (sendDepth_ > 0) {
    deferredEmits_.push_back(std::move(event));
    return;
  }
  emitEvent(event);
}

/**
 * isOutgoingRequest 判断一帧该按「请求」发还是按「应答」发。
 *
 * 大多数帧看 type 就够了，**SDP 那两帧不行**：`room.offer` / `room.answer` 是双向的，
 * 谁是请求方由 `pc` 决定（§3.3——pub 由客户端 offer、sub 由服务端 offer）。
 * 所以 pub 侧的 offer 是**我们发起的请求**（它的应答是 `room.answer`，没有 `.ok`），
 * 而 sub 侧的 answer 是**服务端那个 offer 的应答**，要回显对方的 req_id。
 */
bool isOutgoingRequest(const OutgoingFrame& frame) {
  if (isRequestType(frame.type)) return true;
  return frame.type == frame::kRoomOffer && str(frame.data, "pc") == "pub";
}

void CallEngine::sendOne(const OutgoingFrame& frame, const std::string& replyReqId) {
  if (!connection_) {
    /*
      还没 login 就调了业务方法。状态机已经把状态推过去了（比如进了 inviting），
      而这一帧根本没地方发——**必须补一次失败**，否则通话永远停在 inviting，
      界面「正在呼叫…」转个不停，之后每次挂断都发向一个不存在的 call。

      早先这里是 `if (!connection_) return;`，静默吞掉。ABI 冒烟测试
      「没登录就拨号」把它抓了出来。
    */
    failLocally(frame.type, codeValue(ErrorCode::NotLoggedIn));
    return;
  }
  const std::int64_t now = options_.clock();

  // 状态机产出的 SDP 帧里 sdp 是空串——它不认识 libwebrtc。媒体面把它接管过去，
  // 异步拿到真正的 SDP 再发（见 MediaPlane::fillSdp）。
  if (media_ && media_->fillSdp(frame.type, replyReqId, frame.data)) return;

  if (!isOutgoingRequest(frame)) {
    // 不是请求就是「别人请求的应答」（当前只有 sub 侧的 room.answer），回显对方的 req_id。
    connection_->sendFrame(frame.type, replyReqId, frame.data, now);
    return;
  }

  const std::string type = frame.type;
  const bool sent = connection_->request(
      type, frame.data, now, [this, type](const RequestResult& result) {
        if (!result.ok) {
          onRequestFailed(type, result);
          return;
        }
        /*
          **成功的应答也要喂回状态机**。漏了这一步，`room.join.ok` 就没人接：
          房间机永远停在 joining，界面卡在「接通中」，之后每次 publish 都被 R1
          本地拒成 2005。同一条路上的还有 `call.invite.ok`（拿 call_id）、
          `room.publish.ok`（拿 track_id 并发 pub offer）、以及 pub offer 的
          应答 `room.answer`（把发布状态坐实）。

          replyReqId 传空串：这是**我们自己请求的应答**，状态机若因此再发帧，
          那是一次新的请求，不该回显我们自己的 req_id。
        */
        handleIncoming(result.envelope.type, "", result.data);
      });
  if (!sent) {
    // 连接不可用时 request 不会回调，但状态机已经把状态推过去了。这一帧**根本没上线路**，
    // 所以必须当作彻底失败：否则通话会永远停在 inviting，之后每次挂断都发向一个
    // 不存在的 call（换回 1401，永远退不出去）。
    failLocally(type, codeValue(ErrorCode::NetworkUnreachable));
  }
}

void CallEngine::onRequestFailed(const std::string& type, const RequestResult& result) {
  /*
    **先放上行协商的闸，再谈要不要报错。**

    这一条在 tearingDown_ 与 2003 两个 return 之前：那两条 return 说的是
    「这次失败不该打扰宿主」，而闸门是引擎自己的记账——offer 失败了 answer 就不会
    再来，不放闸的话上行从此协商不出去，而且没有任何症状可查。
  */
  if (media_ && type == frame::kRoomOffer) media_->releasePubOffer();

  // 拆除期间的失败是我们自己造成的，不往外传（见 logout() 的注释）。
  if (tearingDown_) return;
  /*
    **断线导致的失败不算「这件事失败了」**。协议 §1.4 规定断开期间通话要保持在
    connected 并展示「正在重连…」，成不成由随后的 `sys.hello.ok` 的 resumed 裁决：
    resumed=true 就接着打，false 才合成 onCallEnd(network)（不变量 I8）。

    在这里把在途的 call.invite / room.join 当成失败，会在**断线的瞬间**就把通话
    拆掉——onDisconnected 之前先冒出一条 onCallEnd(error)，界面直接收场，
    而重连成功后那通电话其实还在。onDisconnected 已经是断线的信号，
    这里再报一条 2003 只是噪声。
  */
  if (result.errorCode == codeValue(ErrorCode::NetworkUnreachable)) return;

  failLocally(type, result.errorCode);
}

void CallEngine::failLocally(const std::string& type, std::int32_t code) {
  /*
    走 emitOrDefer 而不是直接调 observer：failLocally 几乎总是在**某一层的发帧循环里**
    被调到（帧没发出去才叫失败）。直接抛的话，这条错误会跑到「它所属的那次状态转移」
    自己的事件前面去——宿主先看见 onError，才看见 onCallBegin。见 dispatchOutput。
  */
  Json args = Json::makeObject();
  args.set("code", Json::make(static_cast<std::int64_t>(code)));
  args.set("name", Json::make(errorName(code)));
  // for_type 让宿主知道是哪一帧没成。状态机产出的 onError 不带它，取不到就是空串。
  args.set("for_type", Json::make(type));
  emitOrDefer(EmittedEvent{"onError", args});

  /*
    两个帧的失败必须让状态机退回 idle，否则界面永远收不了场：

    - call.invite 失败 → 通话机停在 inviting，界面「正在呼叫…」转个不停，
      而那通电话服务端根本没建；之后每次挂断都换回 1401，**永远退不出去**。
      （Web 端实测：群呼把主叫自己也放进了 callee_ids，服务端回 1004，
      然后连点五次挂断全是 1401。）
    - room.join 失败 → 房间机停在 joining，之后每次 publish 都被 R1 拒成 2005，
      界面停在「正在进入会议…」。

    其余帧的失败只报错：它们不改变「有没有一通电话 / 在不在房里」。
    **请求超时（2004）走的也是这条路**——十秒没应答，那通电话确实没建起来。
  */
  if (type == frame::kCallInvite) {
    apply(MachineInput::internal("call_failed"), "");
  } else if (type == frame::kRoomJoin) {
    apply(MachineInput::internal("join_failed"), "");
  }
}

}  // namespace imrtc
