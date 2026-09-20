#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imrtc/ActionResult.h"
#include "imrtc/CallHistory.h"
#include "imrtc/CallEngineObserver.h"
#include "imrtc/Connection.h"
#include "imrtc/EngineMachine.h"
#include "imrtc/HttpClient.h"
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
  /**
   * 造 HttpClient 的工厂，只有 `fetchCallHistory`（通话记录，`GET /v1/calls`）用。
   * **留空 = 不支持查记录**：`fetchCallHistory` 回 2005。engine 不认识任何具体的 HTTP 库。
   */
  HttpClientFactory httpClientFactory;
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
 * CallOptions 是 `call()` 的可选参数（HOST_INTEGRATION_DESIGN §3.2/§3.3）。
 *
 * 三个字段原样进 `call.invite`；本地先拦 `chatGroupId` 超 64 字节/含空白/换行、
 * `userData` 超 4096 字节——与「callee_ids 里有自己」同一个出口：
 * 结果回 `1004`（for_type `call.invite`）+ `onCallEnd(error)`，不上线路。
 */
struct CallOptions {
  /** 宿主自己的群号，opaque，≤64 字节，禁止空白与换行，可空。通话期间不可改。 */
  std::string chatGroupId;
  /** opaque 字节，≤4096，原样透传给被叫与接通者，Engine 不解析。 */
  std::string userData;
  /** 振铃超时秒数；0 = 使用协议默认值（30），范围 5~120，越界钳到边界。 */
  std::int64_t timeoutSec = 0;
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
 * # 调用结果回给调用方（2.0.0）
 *
 * 发请求的方法（`login` / `call` / `accept` / `reject` / `cancel` / `hangup` / `inviteMore` /
 * `joinCall` / `joinRoom` / `leaveRoom`）末尾带一个可选的 `ActionCompletion`：本地拒绝、服务端拒绝、
 * 超时、没连接、等应答期间断线都**只**从它回来，不再发 `onError`；成功只管这次调用直接发出的那一帧。
 * 引擎随后自动发的连锁帧失败找不到调用方，才走 `onError`（带 for_type）。**不传回调时失败退回
 * `onError`**。通话 / 房间因此收场时 `onCallEnd(error)` / `onRoomLeft` 照发，并排在结果回调之前；
 * 退出类（reject / cancel / hangup / leaveRoom）失败时本地照样收场，错误只供日志。
 * 引擎析构时还没回来的结果一律回 `2005`。规则全文见 server `docs/design/ACTION_RESULT_DESIGN.md`。
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

  /** Settlement 是一次宿主调用的结算记账（定义在 CallEngineRequests.cpp）。 */
  struct Settlement;

  /**
   * setObserver 注册回调表。
   *
   * 存的是 `weak_ptr`：宿主放手即自动注销，不必记得反注册
   * （「对象先死、回调后到」是 C++ 端头号崩因，CONVENTIONS §5）。
   */
  void setObserver(std::weak_ptr<CallEngineObserver> observer);

  /**
   * login 用一枚票建立信令连接。断线会自动重连（§1.4）。
   *
   * 结果：首次握手成功回 `value = session_id`；握手被拒回那个码、握手前连接就断了回 2003、
   * 还没连上就 logout / 再次 login 回 2005。**失败之后连接层仍按既有退避策略重试**（能不能好由码决定），
   * 之后真连上了照样抛 `onConnected`。
   */
  void login(const std::string& token, ActionCompletion done = {});
  /** logout 主动断开，**不会**重连。 */
  void logout();
  /**
   * updateToken 换票。**下次重连生效，不打断当前连接**（§1.5）。
   * 换票是宿主的事——票从宿主的账号体系来，Engine 不认识那套东西。
   */
  void updateToken(const std::string& token);
  /**
   * setAppForeground / notifyNetworkChanged：回到前台（**含睡眠唤醒**）、系统网络变了。
   * 断线后不再按退避白等：正等着重连的下一个 tick 就连；连着的探 3 秒，没回音就重连。
   * 提示类：没登录时空操作。规则见 `engine/src/signaling/ConnectionNudge.cpp`。
   */
  void setAppForeground(bool foreground);
  void notifyNetworkChanged();

  /**
   * fetchCallHistory 查自己的通话记录，按发起时间倒序，**游标翻页**（`GET /v1/calls`）。
   *
   * `limit` 夹在 1..200，默认 20；`cursor` 首页传 0，下一页传上一页的 `nextCursor`，
   * `hasNext == false` 表示到底。**必须已登录**（用登录那枚票，含 `updateToken` 换过的），
   * 否则回 2007；票被拒 1101；网络不通 2003；其它失败 1501；没注入 HttpClient 回 2005。
   * 服务端只返回本人参与过的通话，所以没有 uid 参数。宿主也可以不用它，自己拿 `onCallEnd` 存。
   *
   * 结果恰好回一次，在 `tick()` 里（本地就地拒绝时在返回之前）；引擎析构时还没回来的回 2005。
   */
  void fetchCallHistory(std::int64_t limit, std::int64_t cursor, CallHistoryCompletion done);

  /**
   * call 发起通话。1v1 恰好 1 个被叫；群 ≤8。成功回 `value = call_id`（取自 `call.invite.ok`）。
   * 名单里有自己时本地拒掉：先抛 `onCallEnd(error)`，再回 1004。
   */
  void call(const std::vector<std::string>& calleeIds, const std::string& mediaType, bool isGroup,
           ActionCompletion done = {});
  /** call 的带选项重载：群号 / user_data / 振铃超时（HOST_INTEGRATION_DESIGN §3.3）。 */
  void call(const std::vector<std::string>& calleeIds, const std::string& mediaType, bool isGroup,
           const CallOptions& options, ActionCompletion done = {});
  /** accept 接听。第二次调用会被**本地**拒绝（2005），不发上去。 */
  void accept(ActionCompletion done = {});
  /** reject 拒接。状态由随后的 `call.ended` 推进；帧失败时本地照样收场。 */
  void reject(ActionCompletion done = {});
  /** cancel 取消呼出（仅接通前）。帧失败时本地照样收场。 */
  void cancel(ActionCompletion done = {});
  /** hangup 挂断（接通中或接通后）。帧失败时本地照样收场。 */
  void hangup(ActionCompletion done = {});
  /**
   * forceEnd 强制结束当前这一场：**结束帧立刻发出，本地立刻收场，不等服务端。**
   *
   * 给「红键按下去、等不到结束事件」用：`hangup()` 只发帧，状态由服务端的 `call.ended` 推进，
   * 帧没发出去或被拒了这一场就收不掉。按此刻状态挑结束帧（通话中 hangup、响铃中 reject、
   * 拨出中 cancel、会议里 room.leave），本地抛 `onCallEnd`（会议抛 `onRoomLeft`）；
   * 服务端随后的 `call.ended` 因为本地已是 idle 被丢掉，不会抛第二次。
   *
   * 拨出时 `call.invite.ok` 还没回来就收场：那条 invite.ok 迟到时补发 `call.cancel`，
   * 迟到的是 `call.connected` 就补发 `call.hangup`；迟到的 `room.join.ok` 补发 `room.leave`。
   * 连接断着时帧发不出去，只做本地收场。没有通话也不在房里时是空操作。
   */
  void forceEnd();
  /** inviteMore 群通话中途加人，通话里的任何人都能发（不在通话里回 1407）。 */
  void inviteMore(const std::vector<std::string>& calleeIds, ActionCompletion done = {});
  /**
   * joinCall 主动加入一通进行中的群通话。「怎么知道它在进行」是宿主的事。
   * 被拒（1401 / 1402 / 1202 / 1408 / 1409）时回那个码，同时照发 `onCallEnd(error)`。
   */
  void joinCall(const std::string& callId, ActionCompletion done = {});

  /** joinRoom 直接进一个会议房（不走振铃）。 */
  void joinRoom(const std::string& roomId, const std::string& roomToken,
                ActionCompletion done = {});
  /** leaveRoom 离房。帧失败时本地照样收场（`onRoomLeft` 照发）。 */
  void leaveRoom(ActionCompletion done = {});

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
   * 提示类：**没有结果**，失败走 `onError`。
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
  /** fireExpiredUnsubscribes 把到点的翻页退订喂回状态机（RoomPaging.h）。 */
  void fireExpiredUnsubscribes();
  /** syncUnsubscribeDeadlines 让截止时刻表与待退订清单一致。 */
  void syncUnsubscribeDeadlines();
  /**
   * request 把一次**宿主调用**喂进状态机，并把结果交回 `done`（见类注释「调用结果回给调用方」）。
   * `valueKey` 是成功时从应答 data 里取值的键（`call` 取 `call_id`），空串表示没有成功值。
   */
  void request(const MachineInput& input, ActionCompletion done, const std::string& valueKey = "");
  /** rejectLocally 在上线路之前就拒掉一次调用（1004，参数不合规）。 */
  void rejectLocally(ActionCompletion done, const std::string& forType);
  /** settleFailure 把一帧的失败交给调用方；没有调用方 / 没传回调 / 已经交过一个失败时发 onError。 */
  void settleFailure(const std::shared_ptr<Settlement>& settlement, const ActionResult& result);
  /** settleSuccess 在直接帧都收到 .ok 之后回成功。 */
  void settleSuccess(const std::shared_ptr<Settlement>& settlement);
  /** deliverOrDefer 交出结果；正处在发帧循环里时排到这一轮事件之后。 */
  void deliverOrDefer(const std::shared_ptr<Settlement>& settlement, ActionResult result);
  /** emitError 抛一条找不到调用方的 onError。 */
  void emitError(std::int32_t code, const std::string& forType);
  /** endLocally 按此刻状态本地收场（forceEnd 的收场计算，不发帧）。 */
  void endLocally();
  /** beginLogin 记下一次还在等首次握手的 login。 */
  void beginLogin(ActionCompletion done);
  /** settleLogin 结算还在等的 login。 */
  void settleLogin(const ActionResult& result);
  /**
   * handleIncoming 是**所有下行帧的唯一入口**：先给媒体面看一眼，再喂状态机。
   *
   * 「唯一」很重要——下行帧从两条路进来（服务端主动事件、我们请求的应答），
   * 漏掉一条的后果是静默的：pub offer 的应答是 `room.answer`（它没有 `.ok`，§3.3），
   * 走的正是应答那条路；只在事件那条路上接媒体面的话，上行协商永远完不成，
   * 而且不报错。
   */
  void handleIncoming(const std::string& type, const std::string& reqId, const Json& data);
  void dispatchOutput(EngineOutput output, const std::string& replyReqId,
                      const std::shared_ptr<Settlement>& settlement = nullptr);
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
  /** reconcileJoinedAtInvite 见 CallEngine.cpp。 */
  void reconcileJoinedAtInvite(const EmittedEvent& joined);
  /** videoTrackOf 找某个 uid 的远端视频轨道；没有则空串。 */
  std::string videoTrackOf(const std::string& uid) const;
  void sendOne(const OutgoingFrame& frame, const std::string& replyReqId,
               const std::shared_ptr<Settlement>& settlement);
  /**
   * onRequestFailed 处理一次请求的失败。
   * **断线导致的失败不回滚**——那不是「这件事失败了」，见 .cpp 的长注释；但结果照样交给调用方。
   * `data` 是那一帧原本要发的线路数据，`rollback` 靠它取 `room.publish` 的 cid /
   * `room.subscribe` 的 track_id（静默失败审计 §A）。
   */
  void onRequestFailed(const std::string& type, const Json& data, const RequestResult& result,
                       const std::shared_ptr<Settlement>& settlement);
  /** failLocally 把「这一帧确实没成」交给调用方（或报 onError），并让状态机收场。 */
  void failLocally(const std::string& type, const Json& data, std::int32_t code,
                   const std::shared_ptr<Settlement>& settlement = nullptr);
  /**
   * rollback 把「这一帧没成」翻译成状态机认识的内部事件。
   * `code` 只有 `room.publish` 用得上——没等到应答（`isUnansweredCode`）与被服务端
   * 真拒了走的是两条不同的路（挂起等重连 vs 判死），见 .cpp 的长注释。
   */
  void rollback(const std::string& type, const Json& data, std::int32_t code);
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
   * 上一次断开的 WebSocket 关闭码与「会不会自动重连」，由 Connection 给出。
   *
   * **同样绕开状态机**：道理与 kickedReason_ 一样——`onDisconnected` 那条 emit
   * 的 args 在一致性向量里是 `{}`，状态机不知道关闭码是多少，门面在派发前补进 args
   * （见 CallEngineEvents.cpp 的 emitEvent）。
   */
  std::int32_t lastDisconnectCode_ = 0;
  bool lastDisconnectWillReconnect_ = false;
  /**
   * sendDepth_ 是「正在几层发帧循环里」。>0 意味着**这一层还没轮到抛事件**，
   * 此刻产生的任何事件都要攒进 deferredEmits_，等最外层 unwind 之后再放。
   * 见 dispatchOutput 的长注释。
   */
  int sendDepth_ = 0;
  /** deferredEmits_ 是重入期间攒下的事件，按产生顺序排队。 */
  std::vector<EmittedEvent> deferredEmits_;
  /**
   * 翻页退订的五秒迟滞：track_id → 到点时刻（`tick()` 到点时喂内部事件，RoomPaging.h）。
   *
   * **桌面端没有自己的定时器**，一切由外面 `tick()` 驱动（与 `Heartbeat` 同一套做法），
   * 所以这里记的是截止时刻而不是句柄。每轮状态推进后按
   * `RoomContext::pendingUnsubscribe` **整体对账**：清单里有而没记的排上，
   * 有记而清单里没有的抹掉——翻回来撤销、人走了清空、订满时提前退，来路再多也不用改这里。
   */
  std::map<std::string, std::int64_t> unsubscribeDeadlines_;
  /**
   * 正在执行 `logout()`。这期间连接层抛上来的东西**一概不往外传**——
   * 是宿主自己要拆的，见 logout() 里的注释。
   */
  bool tearingDown_ = false;
  /** 本端抛 onCallBegin 的本地时刻，0 = 不在通话里。forceEnd 的时长从这里算。 */
  std::int64_t callStartedAtMs_ = 0;
  /** 来电时服务端说「此刻已在通话里的人」。进房快照到了就拿它对账，然后清空（只对被叫有值）。 */
  std::vector<std::string> joinedAtInvite_;
  /** 重入期间攒下的调用结果，排在 deferredEmits_ 之后放（状态事件先于结果）。 */
  std::vector<std::pair<std::shared_ptr<Settlement>, ActionResult>> deferredResults_;
  /** 还没交出结果的调用。析构时一律回 2005（R5：每次被受理的调用恰好回一次）。 */
  std::vector<std::shared_ptr<Settlement>> openSettlements_;
  /** 还在等首次握手的 login。 */
  std::shared_ptr<Settlement> loginSettlement_;
  /** 当前登录的 uid（取自 sys.hello.ok），`call` / `inviteMore` 拦「名单里有自己」用。 */
  std::string uid_;
  /** 当前在用的接入票（含 `updateToken` 换过的），logout 清空。给同一身份的 REST 调用（通话记录）用。 */
  std::string ticket_;
  /** 通话记录用的 HTTP 客户端，第一次查询时才造。 */
  std::unique_ptr<HttpClient> http_;
  /** 还没回来的通话记录查询（id → 结果回调）。析构时一律回 2005。 */
  std::map<std::uint64_t, CallHistoryCompletion> historyPending_;
  std::uint64_t historySeq_ = 0;
};

}  // namespace imrtc
