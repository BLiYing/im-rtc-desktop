#include "imrtc/MediaPlane.h"

#include "imrtc/Log.h"

#include <utility>

#include "imrtc/Errors.h"
#include "imrtc/MachineTypes.h"
#include "imrtc/Registry.h"

namespace imrtc {
namespace {

/** candidateOf 把线路上的 room.ice_candidate 解成适配器认识的形状。 */
IceCandidate candidateOf(const Json& data) {
  IceCandidate candidate;
  candidate.candidate = str(data, "candidate");
  candidate.sdpMid = str(data, "sdp_mid");
  candidate.sdpMLineIndex = num(data, "sdp_mline_index");
  return candidate;
}

/** candidateFrame 把候选装回线路形状。 */
Json candidateFrame(PcRole pc, const IceCandidate& candidate) {
  Json data = Json::makeObject();
  data.set("pc", Json::make(pcRoleName(pc)));
  data.set("candidate", Json::make(candidate.candidate));
  data.set("sdp_mid", Json::make(candidate.sdpMid));
  data.set("sdp_mline_index", Json::make(candidate.sdpMLineIndex));
  return data;
}

Json sdpFrame(PcRole pc, const std::string& sdp) {
  Json data = Json::makeObject();
  data.set("pc", Json::make(pcRoleName(pc)));
  data.set("sdp", Json::make(sdp));
  return data;
}

}  // namespace

MediaPlane::MediaPlane(std::shared_ptr<MediaAdapter> adapter, Deps deps)
    : adapter_(std::move(adapter)), deps_(std::move(deps)) {}

MediaPlane::~MediaPlane() {
  // 见头文件：attach() 埋下的回调捕获了裸 this，解绑只能靠 close()。
  if (attached_) close();
}

void MediaPlane::attach() {
  if (!adapter_ || attached_) return;
  attached_ = true;

  MediaAdapterEvents events;
  events.onLocalCandidate = [this](PcRole pc, const IceCandidate& candidate) {
    // 本端候选一路发上去。空候选表示收集结束，**照发不误**——协议要求接收方容忍，
    // 而少发这一条会让某些服务端一直等下去。
    deps_.send(frame::kRoomIceCandidate, "", candidateFrame(pc, candidate));
  };
  events.onPcState = [this](PcRole pc, PcState state) {
    /*
      **「媒体就绪」只看 sub PC**（§5.1）：通话状态机从 connecting 走到 connected
      的条件是「room.join.ok 到手 + sub PC 的 ICE 连通」。pub 通了只说明我们发得出去，
      对方能不能听见是另一回事；而界面上的「接通」是给用户看的，它该等能听见对方。
    */
    if (pc == PcRole::Sub && state == PcState::Connected) {
      deps_.dispatchInternal("media_ready");
    }
    /*
      **ICE 失败不是终点，是该重连的信号**（协议 §3.3）。

      不救的后果不是「画质差一点」：切网 / 休眠唤醒之后人**永久掉出这通通话**，
      对端格子从此是一块黑，而界面上一切正常、计时还在走、谁也不挂断。

      `pub` 的 offerer 恒为本端，只能自己救；`sub` 由服务端救——各自重启自己 offer
      的那条，所以不需要新协议帧，也不会两边同时 offer 打架。
      重启失败还会再进 failed，于是天然形成一个重试节奏。

      **但重试节奏不能没有尽头**（协议 §7.2）：一律自愈、永不上报的话，宿主从头到尾
      收不到任何信号——对端格子已经黑了、计时器还在走，而界面上一切正常。
      所以连续 kPubIceGiveUp 次重启后仍判 failed，抛一次 2006；之后继续重试但不再重复抛。

      **用 failed 不用 disconnected**：后者是几秒的抖动，见着就重启等于自己制造风暴。
    */
    if (pc == PcRole::Pub && state == PcState::Connected) {
      // 救回来了，下一轮重新计数。
      pubIceRestarts_ = 0;
      pubIceGaveUp_ = false;
    }
    if (pc == PcRole::Pub && state == PcState::Failed) {
      log(LogLevel::Warn, "上行通路失败，重启 ICE");
      ++pubIceRestarts_;
      if (pubIceRestarts_ >= kPubIceGiveUp && !pubIceGaveUp_) {
        pubIceGaveUp_ = true;
        log(LogLevel::Error, "上行通路连续重启仍失败，上报宿主");
        deps_.reportError(codeValue(ErrorCode::MediaNegotiationFailed), "");
      }
      restartPubIce();
      return;
    }
    if (state == PcState::Failed) {
      // sub 那条我们救不了（offerer 是服务端），只能报给宿主。
      log(LogLevel::Warn, "下行通路失败，等服务端重启");
      deps_.reportError(codeValue(ErrorCode::MediaNegotiationFailed), "");
    }
  };
  events.onFirstVideoFrame = [this](const std::string& trackId) {
    if (deps_.onFirstVideoFrame) deps_.onFirstVideoFrame(deps_.uidOf(trackId), trackId);
  };
  events.onRemoteTrack = [](const std::string&, MediaKind) {
    // 远端轨道到手这件事本身不抛给宿主：宿主关心的是「谁的画面能看了」，
    // 那件事由 onUserVideoAvailable（信令）与 onFirstVideoFrame（媒体）合起来说。
  };

  adapter_->open(std::move(events));
}

void MediaPlane::onRoomJoined(bool wantVideo) {
  if (!adapter_) return;

  adapter_->acquireMicrophone([this](bool ok, const LocalTrack& track, std::int32_t code) {
    if (!ok) {
      // 麦克风拿不到 = 这通电话没法进行（交互稿 §02：麦克风被拒整通取消）。
      deps_.reportError(code, frame::kRoomPublish);
      return;
    }
    publishTrack(track);
  });

  if (!wantVideo) return;
  adapter_->acquireCamera([this](bool ok, const LocalTrack& track, std::int32_t code) {
    if (!ok) {
      // 摄像头被拒**只降级为语音**，通话继续（交互稿 §02）。
      deps_.reportError(code, frame::kRoomPublish);
      return;
    }
    publishTrack(track);
  });
}

void MediaPlane::publishTrack(const LocalTrack& track) {
  localCids_[mediaKindName(track.kind)] = track.cid;

  // 走状态机的 publish 动作而不是直接发帧：本地记账（cid → publishing）要跟着走，
  // 而且 joining 期间的调用要被缓存下来重放（不变量 R2）。
  Json args = Json::makeObject();
  args.set("cid", Json::make(track.cid));
  args.set("kind", Json::make(mediaKindName(track.kind)));
  args.set("source", Json::make(track.source));
  args.set("simulcast", Json::make(track.kind == MediaKind::Video));
  deps_.dispatchAct("publish", args);
}

bool MediaPlane::fillSdp(const std::string& type, const std::string& reqId, const Json& data) {
  if (!adapter_) return false;
  // 只接管**空 SDP** 的那两种帧：状态机说「该协商了」，字节由这里补。
  if (!str(data, "sdp").empty()) return false;

  if (type == frame::kRoomOffer && str(data, "pc") == "pub") {
    startPubOffer();
    return true;
  }
  if (type == frame::kRoomAnswer && str(data, "pc") == "sub") {
    // 这条 answer 是**服务端那个 offer 的应答**，必须回显它的 req_id（§3.3）。
    // reqId 在这里被捕获住带进异步回调——不带的话服务端配不上号，
    // 那条下行流永远挂不上来，而且不报错、只是没画面。
    const std::string replyReqId = reqId;
    adapter_->answerSubOffer(pendingSubOffer_, [this, replyReqId](bool ok, const std::string& sdp,
                                                                  std::int32_t code) {
      if (!ok) {
        deps_.reportError(code, frame::kRoomAnswer);
        return;
      }
      deps_.send(frame::kRoomAnswer, replyReqId, sdpFrame(PcRole::Sub, sdp));
    });
    return true;
  }
  return false;
}

void MediaPlane::onSignalingFrame(const std::string& type, const std::string& reqId,
                                  const Json& data) {
  (void)reqId;
  if (!adapter_) return;

  if (type == frame::kRoomOffer && str(data, "pc") == "sub") {
    // 记下服务端的 offer；状态机随后会产出一条空 SDP 的 room.answer，
    // fillSdp 拿这份 offer 去生成应答。
    pendingSubOffer_ = str(data, "sdp");
    return;
  }
  if (type == frame::kRoomAnswer && str(data, "pc") == "pub") {
    adapter_->applyPubAnswer(str(data, "sdp"), [this](bool ok, std::int32_t code) {
      if (!ok) deps_.reportError(code, frame::kRoomAnswer);
      // **落地与落地失败都要放闸**：失败了也不会再有第二条 answer 回来，
      // 不放就是把上行永久锁死。
      releasePubOffer();
    });
    return;
  }
  if (type == frame::kRoomIceCandidate) {
    PcRole pc = PcRole::Pub;
    if (!parsePcRole(str(data, "pc"), pc)) return;
    const IceCandidate candidate = candidateOf(data);
    // 空候选 = 收集结束，忽略（§3.3）。
    if (candidate.candidate.empty()) return;
    /*
      **这条路径最容易整条漏掉**：候选只往上发、不往下收，于是下行连接能不能建立
      全看运气——服务端的 SDP 里碰巧带上了主机候选就通，没带上就永远停在 new，
      界面上是「格子在、画面黑」，而且不报任何错。（Web 端踩过。）
    */
    adapter_->addRemoteCandidate(pc, candidate);
  }
}

void MediaPlane::setMuted(MediaKind kind, bool muted) {
  const std::string cid = localCid(kind);
  if (cid.empty() || !adapter_) return;
  adapter_->setMuted(cid, muted);

  /*
    **线路上要的是 track_id，不是 cid**。

    cid 是本端生成的，只活在 `room.publish` 请求与 pub offer 的 msid 里——服务端靠它
    把 SDP 的 m-line 认回自己分配的 track_id（§3.2），两者是两套命名。把 cid 填进
    `room.mute` 的 track_id 里，服务端找不到那条轨道，于是**不广播 `room.track_muted`**：
    本端确实停发了，可房里其他人的麦克风图标永远不变。没有任何一端会报错。

    映射记在房间机的 publishTrackIds 里，由门面借出来查（deps_.trackIdOfCid）。
  */
  const std::string trackId = deps_.trackIdOfCid ? deps_.trackIdOfCid(cid) : std::string();
  if (trackId.empty()) {
    // `room.publish.ok` 还在路上，线路上还没有任何东西能指代这条轨道。
    // 本端已经停发了，但对端这次不会知道——**报出去**，别静悄悄发一帧废的上去。
    deps_.reportError(codeValue(ErrorCode::InvalidState), frame::kRoomMute);
    return;
  }

  Json args = Json::makeObject();
  args.set("track_id", Json::make(trackId));
  args.set("muted", Json::make(muted));
  deps_.dispatchAct("mute", args);
}

/*
  上行协商闸门：**同一时刻只许有一个 pub offer 在飞**（协议 §3.3）。

  # 不加会怎样

  发布 audio 与 video 两条轨道 → 两次 `room.publish.ok` → 房间机连吐两帧
  `room.offer{pub}`。两个一起在飞时，offer#2 的 setLocalDescription 覆盖掉 offer#1，
  answer#1 回来时本端已经不是当初那个 offer 了：iOS 真机上是
  `Called in wrong state: stable (INVALID_STATE)` + `error 1501`，那次自愈了；
  **Android 上同一个缺陷的后果是上行再也协商不出去**。

  # 为什么闸门在这一层（与 iOS 刻意不同）

  iOS 把闸放在 `IMFrameLoop`，因为在它那儿只有帧泵能表达「这一帧先别发」。
  本仓不一样：`fillSdp` 本身就是**帧的接管点**——返回 true 等于「这一帧我收下了，
  什么时候真发由我说了算」。闸门放在这里最短，也不必再给帧泵加一个它不关心的概念。

  另外两处与 iOS 相同、与 Android 不同，理由值得记住：
  - **不用锁**：本仓是单线程 tick 模型，适配器的回调按约定也投递在宿主线程。
  - **不记 pendingIceRestart**：那一位在适配器上（见 MediaAdapter::restartPubICE），
    排队等一轮再发也不会弄丢——这正是「位记在适配器上」的价值。

  # 放闸的四个终局，一个都不能少

  answer 落地 / answer 应用失败 / 帧根本没发出去 / 会话恢复与收场（resetPubNegotiation）。
  漏掉任何一个，闸门就永远停在「有一个在飞」，上行从此沉默而日志里一切正常。
*/
void MediaPlane::startPubOffer() {
  if (!adapter_) return;
  if (pubOfferInFlight_) {
    // **不是丢掉，是攒下来**：放闸时补一条。攒一条就够——offer 描述的是当前全部
    // 轨道的状态，两条待办合成一条不丢任何东西。
    pubOfferQueued_ = true;
    log(LogLevel::Debug, "上行协商进行中，这一条先攒着");
    return;
  }
  pubOfferInFlight_ = true;
  adapter_->createPubOffer([this](bool ok, const std::string& sdp, std::int32_t code) {
    if (!ok) {
      deps_.reportError(code, frame::kRoomOffer);
      releasePubOffer();
      return;
    }
    if (!deps_.send(frame::kRoomOffer, "", sdpFrame(PcRole::Pub, sdp))) {
      // 帧根本没走出去（没连接 / 连接不在 connected）。**这一条最容易漏**：
      // 不放闸的话下一次协商永远等一个不会回来的 answer。
      log(LogLevel::Warn, "上行 offer 没发出去，放闸");
      releasePubOffer();
    }
  });
}

/** releasePubOffer 放闸；有攒着的就立刻补一条。 */
void MediaPlane::releasePubOffer() {
  pubOfferInFlight_ = false;
  if (!pubOfferQueued_) return;
  pubOfferQueued_ = false;
  log(LogLevel::Debug, "补发攒下的上行协商");
  startPubOffer();
}

/*
  resetPubNegotiation 无条件清零。

  **会话恢复时必须调**：换了连接，旧那条 offer 的 answer **永远不会回来**了
  （在途请求在断线时已被 2003 结掉，而门面对 2003 是刻意放过的）。
  不清零就是 Android 上那个「上行永久沉默」——闸门停在「有一个在飞」，
  而那个「在飞」的东西活在一条已经不存在的连接上。
*/
void MediaPlane::resetPubNegotiation() {
  pubOfferInFlight_ = false;
  pubOfferQueued_ = false;
  // 换连接 / 一轮结束都算新一轮，ICE 放弃计数跟着归零（协议 §7.2）。
  pubIceRestarts_ = 0;
  pubIceGaveUp_ = false;
}

/*
  restartPubIce 是两个触发点共用的那两步：**先置位、再发帧**。

  顺序不能反：帧一发出去，房间机就产出 `room.offer{pub, sdp:""}`，媒体面随即
  被叫去 fillSdp → createPubOffer。那一刻若位还没置上，出去的就是个**普通 offer**，
  ICE 不会重来，那条连接永远回不来而日志里一切正常。
*/
void MediaPlane::restartPubIce() {
  if (!adapter_) return;
  adapter_->restartPubICE();
  deps_.dispatchAct("restart_pub_ice", Json::makeObject());
}

/*
  会话恢复之后重新协商上行（协议 §1.4）。

  **这个触发点是必需的，光有上面那条 failed 不够。**

  网一断信令也跟着断，房间立刻变成 reconnecting，而 PC 要等约 30 秒才判 failed——
  那时 `restart_pub_ice` 会被房间机以 2005 拒掉（它刻意不进 isBufferable），
  于是**在它唯一该生效的场景里等于不存在**。iOS 真机 2026-09-07 抓到过实证
  （`动作被状态机本地拒绝 op=restart_pub_ice room_state=reconnecting`），四端同一条路。

  **不查 PC 当前状态、无条件重启**：换了连接就等于换了网络路径，旧候选多半已废；
  服务端那侧也是无条件重启 sub，两边对称。多一次协商比漏一次自愈便宜得多。
  房间不在 joined 时房间机自会拒掉，不必在这里判。
*/
void MediaPlane::renegotiateAfterResume() {
  if (!attached_) return;
  log(LogLevel::Info, "会话已恢复，重新协商上行");
  // **先清零再重启**：闸门可能还停在断线前那条 offer 上，而它的 answer 永远不会
  // 回来了。不清零，下面这一条就会被自己的闸挡住，上行从此沉默。
  resetPubNegotiation();
  restartPubIce();
}

void MediaPlane::reset() {
  // 一轮结束，闸门跟着归零：那条 offer 的 answer 不会再来了。
  resetPubNegotiation();
  localCids_.clear();
  pendingSubOffer_.clear();
  if (adapter_) adapter_->reset();
}

void MediaPlane::close() {
  resetPubNegotiation();
  localCids_.clear();
  pendingSubOffer_.clear();
  attached_ = false;
  if (adapter_) adapter_->close();
}

void MediaPlane::poll() {
  if (adapter_) adapter_->poll();
}

std::string MediaPlane::localCid(MediaKind kind) const {
  const auto it = localCids_.find(mediaKindName(kind));
  return it == localCids_.end() ? std::string() : it->second;
}

}  // namespace imrtc
