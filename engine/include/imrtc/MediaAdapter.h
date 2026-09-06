#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace imrtc {

/**
 * 媒体适配器：**engine 里唯一碰 libwebrtc 的地方**（CONVENTIONS §1）。
 *
 * 抽成接口有三个理由：
 * 1. engine 的其余部分必须能在**没有 libwebrtc 的机器上**编译并跑完全部测试——
 *    那个包 300 MB 起步，而且不是每个平台都有现成的（macOS 目前只有 arm64）；
 * 2. libwebrtc 的 API 随 Chromium 里程碑变，变化被挡在这一层里面（设计 §8）；
 * 3. 状态机与信令一行都不认识 SDP —— 它们只说「现在该 offer 了」，字节由这层给。
 */

/** PcRole 是两条 PeerConnection 之一。**每条的 offerer 是固定的**（协议 §3.3）。 */
enum class PcRole {
  /** 上行：由**客户端** offer。 */
  Pub,
  /** 下行：由**服务端** offer。 */
  Sub,
};

const char* pcRoleName(PcRole role);
bool parsePcRole(const std::string& text, PcRole& out);

/** MediaKind 是轨道类型。 */
enum class MediaKind { Audio, Video };
const char* mediaKindName(MediaKind kind);

/** LocalTrack 是一条本端轨道。 */
struct LocalTrack {
  /**
   * cid 是**客户端生成**的本地 track 标识（协议 §2.5）。
   *
   * 它必须出现在随后 pub offer 的 msid 里——服务端靠它把 SDP 的 m-line
   * 认回 track_id（§3.2）。所以顺序是先拿轨道、再拿 cid、最后才发 `room.publish`。
   */
  std::string cid;
  MediaKind kind = MediaKind::Audio;
  /** "microphone" / "camera"。 */
  std::string source;
};

/** IceCandidate 是一个 ICE 候选。`candidate` 为空表示收集结束（§3.3 要求容忍）。 */
struct IceCandidate {
  std::string candidate;
  std::string sdpMid;
  std::int64_t sdpMLineIndex = 0;
};

/** PcState 是一条 PeerConnection 的连接状态，取 libwebrtc 那一套的子集。 */
enum class PcState { New, Connecting, Connected, Disconnected, Failed, Closed };
const char* pcStateName(PcState state);

/**
 * MediaAdapterEvents 是媒体层回给 engine 的出口。
 *
 * 全部在**宿主线程**上抛——libwebrtc 的回调来自它自己的信令线程，
 * 实现方必须像 `IxTransport` 那样排队投递，否则整套单线程假设就塌了。
 */
struct MediaAdapterEvents {
  /** 本端收集到一个 ICE 候选，engine 要把它发成 `room.ice_candidate`。 */
  std::function<void(PcRole pc, const IceCandidate& candidate)> onLocalCandidate;
  /** 收到一条下行轨道。trackId 就是协议里的 `track_id`（msid 第二段）。 */
  std::function<void(const std::string& trackId, MediaKind kind)> onRemoteTrack;
  /** 某条 PC 的连接状态变了。engine 据此判断「媒体就绪」。 */
  std::function<void(PcRole pc, PcState state)> onPcState;
  /** 某条远端视频轨**真的开始出数据**了。UI 用它撤 loading。 */
  std::function<void(const std::string& trackId)> onFirstVideoFrame;
};

/**
 * Completion 是一次异步媒体操作的结果。
 *
 * 用回调而不是阻塞：libwebrtc 的 createOffer / setRemoteDescription 全是异步的，
 * 包成同步就得在宿主线程上等它自己的信令线程——那是死锁的经典配方。
 */
using SdpCompletion = std::function<void(bool ok, const std::string& sdp, std::int32_t errorCode)>;
using VoidCompletion = std::function<void(bool ok, std::int32_t errorCode)>;
using TrackCompletion = std::function<void(bool ok, const LocalTrack& track, std::int32_t errorCode)>;

class MediaAdapter {
public:
  virtual ~MediaAdapter() = default;

  /** open 建立两条 PeerConnection。**每个参与者最多两条**（CONVENTIONS §9）。 */
  virtual void open(MediaAdapterEvents events) = 0;

  /**
   * probeMicrophone **只探一下麦克风权限**，拿到就立刻放掉，不挂到任何 PC 上。
   *
   * 时机是硬要求（交互稿 §01）：主叫在发 `call.invite` **之前**、被叫在发
   * `call.accept` **之前**就得知道麦克风拿不拿得到——拿不到就不该去响别人的铃，
   * 也不该让对方那边显示已接通却听不到人。而 `acquireMicrophone` 要等房间开了
   * （pub PC 存在）才能调，那时早过了该问的时刻。
   *
   * 被拒回 `2001 device_permission_denied`，没设备回 `2002 device_not_found`。
   */
  virtual void probeMicrophone(VoidCompletion done) = 0;

  /**
   * startLocalPreview 只**起采集**，不发布。
   *
   * 拨出中还没有房间，推流无从谈起，但界面这时就该让人看见自己（草图 §03-E）。
   * 所以「采集」与「发布」必须是两件事；随后的 `acquireCamera` **复用同一条轨道**，
   * 否则会把摄像头开两次（第二次抢设备，画面闪一下甚至直接失败）。幂等。
   */
  virtual void startLocalPreview(TrackCompletion done) = 0;

  /** acquireMicrophone 拿麦克风轨道并挂到 pub PC 上。 */
  virtual void acquireMicrophone(TrackCompletion done) = 0;
  /** acquireCamera 拿摄像头轨道并挂到 pub PC 上。已在预览则**复用那条轨道**。 */
  virtual void acquireCamera(TrackCompletion done) = 0;

  /** createPubOffer 生成上行 offer。**pub 的 offerer 恒为本端**（§3.3）。 */
  virtual void createPubOffer(SdpCompletion done) = 0;
  /** applyPubAnswer 应用服务端对上行 offer 的应答。 */
  virtual void applyPubAnswer(const std::string& sdp, VoidCompletion done) = 0;
  /** answerSubOffer 应答服务端下发的下行 offer。**sub 的 offerer 恒为服务端**。 */
  virtual void answerSubOffer(const std::string& sdp, SdpCompletion done) = 0;

  /** addRemoteCandidate 加一个远端候选。**乱序到达是常态**，实现要容忍。 */
  virtual void addRemoteCandidate(PcRole pc, const IceCandidate& candidate) = 0;

  /**
   * setMuted 开关本端某条轨道。
   *
   * **这不是 unpublish**：轨道与协商都保留，只是停止发包。
   * 反复开关摄像头走 unpublish 会触发重协商风暴。
   */
  virtual void setMuted(const std::string& cid, bool muted) = 0;

  /**
   * attachView 把某条远端轨道的画面挂到宿主的原生窗口上（设计 §8.3 的渲染路径 A）。
   *
   * `nativeHandle` 在 Windows 上是 `HWND`、macOS 上是 `NSView*`；传 nullptr 卸载。
   * 自绘宿主走路径 B（原始帧回调），那条口子等第五刀随 C ABI 一起定。
   */
  virtual void attachView(const std::string& trackId, void* nativeHandle) = 0;

  /**
   * reset 把这一轮的媒体归零并**重建 PeerConnection**。
   *
   * PC 是**跟着房间走**的：服务端每次进房都新建一对。不重建的话上一轮的
   * transceiver 还挂着，下一轮的 offer 会多出几条服务端不认识的 m-line，
   * 服务端只会记一句「收到未登记的上行 Track」然后丢掉，界面上是黑屏。
   */
  virtual void reset() = 0;

  /** close 彻底关掉两条 PC 并停掉所有本端轨道。 */
  virtual void close() = 0;

  /** poll 把攒下的媒体事件投递给 engine，由 `CallEngine::tick()` 调用。 */
  virtual void poll() {}
};

}  // namespace imrtc
