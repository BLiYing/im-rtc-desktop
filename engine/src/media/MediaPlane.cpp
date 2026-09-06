#include "imrtc/MediaPlane.h"

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
    if (state == PcState::Failed) {
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
    adapter_->createPubOffer([this](bool ok, const std::string& sdp, std::int32_t code) {
      if (!ok) {
        deps_.reportError(code, frame::kRoomOffer);
        return;
      }
      deps_.send(frame::kRoomOffer, "", sdpFrame(PcRole::Pub, sdp));
    });
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

  Json args = Json::makeObject();
  args.set("track_id", Json::make(cid));
  args.set("muted", Json::make(muted));
  deps_.dispatchAct("mute", args);
}

void MediaPlane::reset() {
  localCids_.clear();
  pendingSubOffer_.clear();
  if (adapter_) adapter_->reset();
}

void MediaPlane::close() {
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
