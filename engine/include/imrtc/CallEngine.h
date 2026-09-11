#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "imrtc/CallEngineObserver.h"
#include "imrtc/Connection.h"
#include "imrtc/EngineMachine.h"
#include "imrtc/MediaAdapter.h"
#include "imrtc/MediaPlane.h"
#include "imrtc/Transport.h"
#include "imrtc/Version.h"

namespace imrtc {

/** Clock 返回 Unix 毫秒。可注入以便测试。 */
using Clock = std::function<std::int64_t()>;

/**
 * steadyClock 是**驱动时序的**默认时钟：心跳、请求超时、重连退避全用它。
 *
 * 必须单调：墙上时钟会被 NTP 校正、被用户改、被休眠唤醒挪动。往回跳一次，
 * `nextPingAtMs_` / `reconnectAtMs_` 全都落在遥远的未来——心跳不再发、在途请求
 * 不再超时、排好的重连永远等不到，引擎抱着一条死连接一声不吭。往前跳一次则相反：
 * 所有在途请求同时超时，一通正在进行的电话被 failLocally 直接判死。
 */
std::int64_t steadyClock();

/**
 * systemClock 是**墙上时钟**，只用来填信封的 `ts`（发送方的 Unix 毫秒时间戳）。
 *
 * 协议规定接收方 `ts` **只用于日志、禁止参与逻辑判断**（§2.1）——本端的时序逻辑
 * 同样不该用它，那正是 steadyClock 存在的理由。
 */
std::int64_t systemClock();

/** CallEngineOptions 是构造参数。 */
struct CallEngineOptions {
  /** 信令端点，生产必须 wss://（§1.1）。 */
  std::string url;
  /** ≤64，同一 uid 下唯一且跨重启稳定（§2.5）。 */
  std::string deviceId;
  /** 仅用于日志与灰度，**禁止参与逻辑**。 */
  std::string sdk = std::string("desktop/") + kSdkVersion;
  /** 请求超时，协议建议 10 秒（§2.2）。 */
  std::int64_t requestTimeoutMs = 10000;
  /** 必填：造 Transport 的工厂。engine 不认识任何具体的 WS 库。 */
  TransportFactory transportFactory;
  /** 退避抖动的随机源。留空用默认实现。 */
  Random01 random;
  /** 单调时钟，驱动心跳/超时/退避。留空用 steadyClock；测试注入假的。 */
  Clock clock;
  /** 墙上时钟，**只**用来填信封的 ts。留空用 systemClock；测试一般不必管。 */
  Clock wallClock;
  /**
   * 媒体适配器。**留空 = 纯信令模式**：能登录、能拨号、能收发所有帧，
   * 就是没有声音画面。第四刀之前的全部测试都跑在这个模式下。
   */
  std::shared_ptr<MediaAdapter> mediaAdapter;
};

/**
 * dispatchObserverEvent 把状态机的一条 `EmittedEvent` 翻译成观察者的方法调用。
 *
 * **返回 false = 这个回调名没人接**。它之所以是公开自由函数而不是私有成员，
 * 是为了让测试能把两台状态机可能抛出的每一个回调名都过一遍——
 * 「加回调时忘了改映射表」这种漏改是静默的（宿主永远收不到那个事件，也没人报错）。
 */
bool dispatchObserverEvent(CallEngineObserver& observer, const EmittedEvent& event);

/**
 * CallEngine 是**无 UI 的门面**：把信令连接与两台状态机缝起来。
 *
 * 它做的事只有三件——
 * 1. 宿主的方法调用 → 状态机 → 该发的帧交给 Connection；
 * 2. Connection 收到的帧 → 状态机 → 该抛的回调交给 Observer；
 * 3. 请求失败时把「这通电话没建起来 / 这个房间没进去」告诉状态机，
 *    让它退回 idle 并给界面一个收场信号。
 *
 * # 谁来喂时钟：**门面**
 *
 * 状态机与 Connection 都不读时钟（前者是不变量 I4，后者是为了可测）。这一层把
 * 时钟收口：宿主只要按 ~200ms~1s 的粒度调 `tick()`，心跳、请求超时、退避重连
 * 就都跑起来了。**engine 不自己起线程**——宿主的事件循环长什么样我们不知道，
 * 而多起一条线程就等于把「回调在哪个线程」的问题甩给宿主（CONVENTIONS §6）。
 *
 * # 线程
 *
 * **不是线程安全的**：所有方法（含 `tick`）必须在同一个线程上调用。
 * 回调也在那个线程上抛出——切到 UI 线程是宿主的事。
 */
class CallEngine {
public:
  explicit CallEngine(CallEngineOptions options);
  ~CallEngine();

  CallEngine(const CallEngine&) = delete;
  CallEngine& operator=(const CallEngine&) = delete;

  /**
   * setObserver 注册回调表。
   *
   * 存的是 `weak_ptr`：宿主放手即自动注销，不必记得反注册
   * （「对象先死、回调后到」是 C++ 端头号崩因，CONVENTIONS §5）。
   */
  void setObserver(std::weak_ptr<CallEngineObserver> observer);

  /** login 用一枚票建立信令连接。断线会自动重连（§1.4）。 */
  void login(const std::string& token);
  /** logout 主动断开，**不会**重连。 */
  void logout();
  /**
   * updateToken 换票。**下次重连生效，不打断当前连接**（§1.5）。
   * 换票是宿主的事——票从宿主的账号体系来，Engine 不认识那套东西。
   */
  void updateToken(const std::string& token);

  /** call 发起通话。1v1 恰好 1 个被叫；群 ≤8。 */
  void call(const std::vector<std::string>& calleeIds, const std::string& mediaType, bool isGroup);
  /** accept 接听。第二次调用会被**本地**拒绝（2005），不发上去。 */
  void accept();
  /** reject 拒接。状态由随后的 `call.ended` 推进——服务端才是裁决方。 */
  void reject();
  /** cancel 取消呼出（仅接通前）。 */
  void cancel();
  /** hangup 挂断（接通中或接通后）。 */
  void hangup();
  /** inviteMore 群通话中途加人，仅主叫可发。 */
  void inviteMore(const std::vector<std::string>& calleeIds);
  /** joinCall 主动加入一通进行中的群通话。「怎么知道它在进行」是宿主的事。 */
  void joinCall(const std::string& callId);

  /** joinRoom 直接进一个会议房（不走振铃）。 */
  void joinRoom(const std::string& roomId, const std::string& roomToken);
  /** leaveRoom 离房。 */
  void leaveRoom();

  /**
   * setRemoteLayer 报某人画面的**层上界**（协议 §3.5：`room.update_layer`）。
   *
   * 九宫格缩略图报 `"l"`、双击放大报 `"h"`。`layer` 取 `none | l | m | h`，
   * `"none"` = 暂停下发该 Track 的媒体但保留订阅关系。
   *
   * **是上界不是命令**：SFU 按 `min(你报的上界, 带宽估计允许的层, 实际存在的层)` 选层，
   * 所以调完不保证立刻变——它还要等目标层的关键帧。**不触发重协商**。
   *
   * 这条**不依赖媒体适配器**：它是一条纯信令帧，`WebRTCAdapter` 没落地时也照发。
   *
   * **那个人的视频轨还没发布时这次调用会被丢掉**（不报错）。宿主在 `onUserEnter`
   * 就把格子建好是最自然的写法，而轨道可能几百毫秒后才到——所以**要在
   * `onUserVideoAvailable` 里再报一次**。Web 端同样如此，两端行为一致。
   */
  void setRemoteLayer(const std::string& uid, const std::string& layer);

  // ---- 媒体（`mediaAdapter` 为空时全部是空操作）----

  /**
   * probeMicrophone **只探麦克风权限**，拿到即放。
   *
   * 时机是硬要求（交互稿 §01）：主叫在 `call()` **之前**、被叫在 `accept()` **之前**
   * 调——拿不到就不该去响别人的铃。
   */
  void probeMicrophone(VoidCompletion done);
  /** startLocalPreview 只起采集不发布，拨出中就能让人看见自己（草图 §03-E）。 */
  void startLocalPreview(TrackCompletion done);
  /** openMic / closeMic 开关麦克风。**不是 unpublish**，轨道与协商都保留。 */
  void openMic();
  void closeMic();
  /** openCamera / closeCamera 开关摄像头。 */
  void openCamera();
  void closeCamera();
  /**
   * attachView 把某个 uid 的远端画面挂到宿主的原生窗口上（设计 §8.3 渲染路径 A）。
   * Windows 传 `HWND`、macOS 传 `NSView*`；传 nullptr 卸载。
   */
  void attachView(const std::string& uid, void* nativeHandle);

  /**
   * attachLocalView 把本端摄像头预览挂到宿主的原生窗口上（1v1 那一屏的小窗）。
   * 传 nullptr 卸载。没有媒体适配器时是空操作。
   */
  void attachLocalView(void* nativeHandle);

  /**
   * notifyMediaReady 由媒体层在「`room.join.ok` 到手 + sub PC 的 ICE 连通」时调用，
   * 通话状态机据此从 connecting 走到 connected（§5.1）。
   *
   * 媒体层还没有（P5 第四刀），所以现在这是给测试与宿主自测用的口子。
   */
  void notifyMediaReady();

  /** tick 推进时间。宿主按 ~200ms~1s 的粒度调用。 */
  void tick();

  ConnectionState connectionState() const;
  CallState callState() const { return context_.call.state; }
  RoomState roomState() const { return context_.room.state; }
  /** sessionId 供诊断观察。 */
  const std::string& sessionId() const;

private:
  void apply(const MachineInput& input, const std::string& replyReqId);
  /**
   * handleIncoming 是**所有下行帧的唯一入口**：先给媒体面看一眼，再喂状态机。
   *
   * 「唯一」很重要——下行帧从两条路进来（服务端主动事件、我们请求的应答），
   * 漏掉一条的后果是静默的：pub offer 的应答是 `room.answer`（它没有 `.ok`，§3.3），
   * 走的正是应答那条路；只在事件那条路上接媒体面的话，上行协商永远完不成，
   * 而且不报错。
   */
  void handleIncoming(const std::string& type, const std::string& reqId, const Json& data);
  void dispatchOutput(EngineOutput output, const std::string& replyReqId);
  void emitEvent(const EmittedEvent& event);
  /** emitAll 抛一批事件给宿主，再让媒体面跟着这批事件动。 */
  void emitAll(const std::vector<EmittedEvent>& events);
  /**
   * emitOrDefer 抛一条本地补的事件；正处在某一层发帧循环里时先攒着。
   * 见 dispatchOutput 里关于重入顺序的长注释。
   */
  void emitOrDefer(EmittedEvent event);
  /** reactToEvents 让媒体面跟着通话/房间的生命周期走（进房推流、终局归零）。 */
  void reactToEvents(const std::vector<EmittedEvent>& events);
  /** videoTrackOf 找某个 uid 的远端视频轨道；没有则空串。 */
  std::string videoTrackOf(const std::string& uid) const;
  void sendOne(const OutgoingFrame& frame, const std::string& replyReqId);
  /**
   * onRequestFailed 处理一次请求的失败。
   * **断线导致的失败在这里被放过**——那不是「这件事失败了」，见 .cpp 的长注释。
   */
  void onRequestFailed(const std::string& type, const RequestResult& result);
  /** failLocally 把「这一帧确实没成」翻译成状态机认识的内部事件，并报给宿主。 */
  void failLocally(const std::string& type, std::int32_t code);
  std::shared_ptr<CallEngineObserver> observer() const { return observer_.lock(); }

  CallEngineOptions options_;
  std::weak_ptr<CallEngineObserver> observer_;
  EngineContext context_;
  std::unique_ptr<Connection> connection_;
  std::unique_ptr<MediaPlane> media_;
  /**
   * Connection 的 onKickedOut 一定先于 onDisconnected 抛出（被踢与鉴权用尽两条路
   * 都是），所以用一个一次性标记把两者合成状态机认识的 `ws_closed_4403`。
   */
  bool kickedOutPending_ = false;
  /**
   * 上一次被踢的原因，由 Connection 给出。
   *
   * **它绕开状态机**：状态机那条 emit 的 args 在一致性向量里就是 `{}`
   * （room_fsm.json 的 `ws_closed_4403`），而且同一个内部事件被 4403 与
   * 「鉴权失败到顶」两条路复用，它没有条件知道原因。门面在派发前补进 args。
   */
  KickedReason kickedReason_ = KickedReason::TakenOver;
  /**
   * sendDepth_ 是「正在几层发帧循环里」。>0 意味着**这一层还没轮到抛事件**，
   * 此刻产生的任何事件都要攒进 deferredEmits_，等最外层 unwind 之后再放。
   * 见 dispatchOutput 的长注释。
   */
  int sendDepth_ = 0;
  /** deferredEmits_ 是重入期间攒下的事件，按产生顺序排队。 */
  std::vector<EmittedEvent> deferredEmits_;
  /**
   * 正在执行 `logout()`。这期间连接层抛上来的东西**一概不往外传**——
   * 是宿主自己要拆的，见 logout() 里的注释。
   */
  bool tearingDown_ = false;
};

}  // namespace imrtc
