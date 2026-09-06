#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "imrtc/CallEngineObserver.h"
#include "imrtc/Connection.h"
#include "imrtc/EngineMachine.h"
#include "imrtc/Transport.h"

namespace imrtc {

/** Clock 返回 Unix 毫秒。可注入以便测试。 */
using Clock = std::function<std::int64_t()>;

/** systemClock 是运行期的默认时钟。 */
std::int64_t systemClock();

/** CallEngineOptions 是构造参数。 */
struct CallEngineOptions {
  /** 信令端点，生产必须 wss://（§1.1）。 */
  std::string url;
  /** ≤64，同一 uid 下唯一且跨重启稳定（§2.5）。 */
  std::string deviceId;
  /** 仅用于日志与灰度，**禁止参与逻辑**。 */
  std::string sdk = "desktop/0.0.1";
  /** 请求超时，协议建议 10 秒（§2.2）。 */
  std::int64_t requestTimeoutMs = 10000;
  /** 必填：造 Transport 的工厂。engine 不认识任何具体的 WS 库。 */
  TransportFactory transportFactory;
  /** 退避抖动的随机源。留空用默认实现。 */
  Random01 random;
  /** 时钟。留空用系统时钟；测试注入假的。 */
  Clock clock;
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
  void dispatchOutput(EngineOutput output, const std::string& replyReqId);
  void emitEvent(const EmittedEvent& event);
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
  /**
   * Connection 的 onKickedOut 一定先于 onDisconnected 抛出（被踢与鉴权用尽两条路
   * 都是），所以用一个一次性标记把两者合成状态机认识的 `ws_closed_4403`。
   */
  bool kickedOutPending_ = false;
  /**
   * 正在执行 `logout()`。这期间连接层抛上来的东西**一概不往外传**——
   * 是宿主自己要拆的，见 logout() 里的注释。
   */
  bool tearingDown_ = false;
};

}  // namespace imrtc
