#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "imrtc/Json.h"
#include "imrtc/MediaAdapter.h"

namespace imrtc {

/**
 * MediaPlane 是**信令面与媒体面之间的接线**。
 *
 * 它不做决策：什么时候该 offer、该 publish，是状态机说了算；这里只负责
 * 「把状态机说的那件事翻译成对 MediaAdapter 的调用」，以及反过来
 * 「把媒体层的产出翻译成该发的帧 / 该报的内部事件」。
 *
 * # 为什么 SDP 是在这里填的
 *
 * 房间状态机产出的 `room.offer` / `room.answer` 里 **sdp 是空串**——它是纯逻辑，
 * 不认识 libwebrtc，也不能等一个异步结果。所以门面在发帧前把空 SDP 的那两种帧
 * 拦下来交给这里：这里去问适配器要真正的 SDP，拿到了再发。
 *
 * 这样状态机的契约不变（一致性向量照跑），而「字节从哪来」全在媒体面。
 */
class MediaPlane {
public:
  /** Deps 是这条接线用到的全部东西。**显式列出来**，反向依赖门面会让边界看不见。 */
  struct Deps {
    /** 发一帧（请求或应答都走它，reqId 空串表示由连接层分配）。 */
    std::function<void(const std::string& type, const std::string& reqId, const Json& data)> send;
    /** 往状态机喂一个内部事件（media_ready / …）。 */
    std::function<void(const std::string& name)> dispatchInternal;
    /** 往状态机喂一次宿主动作（publish / …）。 */
    std::function<void(const std::string& op, const Json& args)> dispatchAct;
    /** 报错给宿主。 */
    std::function<void(std::int32_t code, const std::string& forType)> reportError;
    /** 某条远端轨道属于谁；不知道时返回空串。 */
    std::function<std::string(const std::string& trackId)> uidOf;
    /**
     * 本端某条 cid 对应的服务端 track_id；`room.publish.ok` 还没回来时返回空串。
     *
     * 媒体面手里只有自己生成的 cid，而线路上的 `room.mute` 要的是 track_id
     * （§3.2，room_fsm.json 第 10 步）。映射记在房间机的 publishTrackIds 里，
     * 所以这里跟 uidOf 一样，往门面借一次查表。
     */
    std::function<std::string(const std::string& cid)> trackIdOfCid;
    /** 首帧到达（本地事件，没有对应的信令帧）。 */
    std::function<void(const std::string& uid, const std::string& trackId)> onFirstVideoFrame;
  };

  MediaPlane(std::shared_ptr<MediaAdapter> adapter, Deps deps);

  /**
   * 析构里**兜底关掉**适配器（CONVENTIONS §5：「资源必须有明确的关闭路径，
   * 且析构里也要兜底关闭」）。
   *
   * attach() 交给适配器的那几个回调捕获的是裸 `this`，而适配器是 `shared_ptr`——
   * 宿主自己也可能攥着一份，完全能比引擎活得久。此前唯一的解绑路径是 close()，
   * 而它只有 logout() 会走；宿主不 logout 直接销毁引擎（GC 语言宿主的常态，
   * C ABI 也允许），适配器就攥着一把野指针，下一个 ICE 候选或 PC 状态变化即崩。
   */
  ~MediaPlane();

  MediaPlane(const MediaPlane&) = delete;
  MediaPlane& operator=(const MediaPlane&) = delete;

  /** attach 建立两条 PC 并把回调接上。login 之后调一次即可。 */
  void attach();

  /**
   * onRoomJoined 是「房间开了，可以推流了」。
   *
   * `wantVideo` 来自通话的 `media_type`：语音通话不开摄像头（拍板 §11-10 ——
   * 语音通话里**根本没有**摄像头按钮，也就没有中途开摄像头这回事）。
   */
  void onRoomJoined(bool wantVideo);

  /**
   * fillSdp 处理状态机产出的空 SDP 帧。
   *
   * 返回 true = 这一帧被接管了（异步填好 SDP 后自己发出去），调用方不要再发。
   */
  bool fillSdp(const std::string& type, const std::string& reqId, const Json& data);

  /** onSignalingFrame 处理与媒体有关的下行帧（answer / offer / ice_candidate）。 */
  void onSignalingFrame(const std::string& type, const std::string& reqId, const Json& data);

  /** setMuted 开关本端轨道，并把意图发成 `room.mute`。 */
  void setMuted(MediaKind kind, bool muted);

  /** reset 一轮结束（通话终局 / 离房 / 被踢）时归零。 */
  void reset();
  /** close 彻底关掉（logout）。 */
  void close();
  /** poll 由 tick 调用，把媒体层攒下的事件放出来。 */
  void poll();

  /** localCid 取某种轨道的 cid，供诊断与 UI 预览。没有则空串。 */
  std::string localCid(MediaKind kind) const;

private:
  void publishTrack(const LocalTrack& track);

  std::shared_ptr<MediaAdapter> adapter_;
  Deps deps_;
  bool attached_ = false;
  /** kind → cid。本端最多一条音频 + 一条视频（同 source 最多一条 Track，§3.2）。 */
  std::map<std::string, std::string> localCids_;
  /**
   * 服务端最近一次下发的 sub offer。
   *
   * 服务端的 offer 先到，状态机随后才产出那条空 SDP 的 `room.answer`——
   * 两件事之间要有个地方把 offer 存住，就是这里。
   */
  std::string pendingSubOffer_;
};

}  // namespace imrtc
