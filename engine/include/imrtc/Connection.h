#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "imrtc/Backoff.h"
#include "imrtc/Heartbeat.h"
#include "imrtc/PendingRequests.h"
#include "imrtc/Transport.h"

namespace imrtc {

/** ConnectionState 是连接状态。 */
enum class ConnectionState { Idle, Connecting, Connected, Reconnecting, Closed };

const char* connectionStateName(ConnectionState state);

/** Limits 是服务端下发的限额，让客户端能本地预校验（§2.6）。 */
struct Limits {
  std::int64_t maxFrameBytes = 0;
  std::int64_t maxCallees = 0;
  std::int64_t maxRoomParticipants = 0;
  std::int64_t maxUserDataBytes = 0;
  std::int64_t ringTimeoutSecDefault = 0;
};

/** HelloOk 是握手成功后的服务端信息（§1.2）。 */
struct HelloOk {
  std::string uid;
  std::string deviceId;
  std::string sessionId;
  /** true = 恢复了旧会话，房间成员关系还在（§1.4）。 */
  bool resumed = false;
  std::int64_t pingIntervalSec = 15;
  Limits limits;
};

/** ConnectionEvents 是连接层对外的回调。空的就不抛。 */
struct ConnectionEvents {
  /** 握手完成。 */
  std::function<void(const HelloOk&)> onConnected;
  /** 连接断开。willReconnect=false 时不会再自动回来。 */
  std::function<void(int code, const std::string& reason, bool willReconnect)> onDisconnected;
  /** 被踢，或鉴权连续失败到上限。宿主该回登录页换票。 */
  std::function<void()> onKickedOut;
  /**
   * 断得太久了，**服务端那一侧的会话已经不可能再恢复**（§1.4 的恢复窗口过了）。
   *
   * 与「重连上了但 `resumed=false`」是同一件事，只是**不必等重连成功**——
   * 网络一直不回来的话那一刻永远不会到。少了它，界面就永远停在「正在重连」、
   * 连挂断都点不动（挂断只产出一帧发不出去的 `call.hangup`，本地状态按 §4.2
   * 铁律 1 一动不动）。真机 2026-09-08 的 iOS 端就是这一幕，四端同形。
   */
  std::function<void()> onSessionUnrecoverable;
  /** 收到服务端主动推送的事件（req_id 为空的帧）。data 是**线路形状 + 默认值**。 */
  std::function<void(const std::string& type, const Json& data, const Envelope& envelope)> onEvent;
  /** 内部错误。 */
  std::function<void(std::int32_t code, const std::string& name, const std::string& forType)>
      onError;
};

/** ConnectionOptions 是构造参数。带工厂/随机数的都是为了测试可注入。 */
struct ConnectionOptions {
  std::string url;
  std::string token;
  std::string deviceId;
  /** 仅用于日志与灰度，**禁止参与逻辑**（§1.2）。 */
  std::string sdk = "desktop/0.0.1";
  /** 请求超时。协议建议 10 秒（§2.2）。 */
  std::int64_t requestTimeoutMs = 10000;
  /** 必填：造 Transport 的工厂。engine 不认识任何具体的 WS 库。 */
  TransportFactory transportFactory;
  /** 退避抖动用的随机源。留空则用默认实现。 */
  Random01 random;
  /**
   * 墙上时钟，**只**用来填信封的 `ts`。
   *
   * 与 `tick(nowMs)` 喂进来的那条时间线是两回事：那条必须单调（心跳、超时、退避
   * 都靠它），而 `ts` 按协议是一个 Unix 毫秒时间戳，给对端记日志用。两者混用的话，
   * 要么时序被 NTP 一校正就散架，要么线路上的 ts 是个没有意义的开机计数。
   */
  std::function<std::int64_t()> wallClock;
};

/**
 * Connection 是一条信令连接：握手、心跳、请求应答配对、退避重连。
 *
 * # 三个设计取舍
 *
 * 1. **不持有定时器，时间从外面喂**。心跳周期、请求超时、重连延时全靠
 *    `tick(nowMs)` 推进。于是「45 秒静默判死」「10 秒请求超时」「退避到第 6 档」
 *    这些规则不用真的等就能测，也不必在 engine 里塞一个事件循环
 *    （宿主的事件循环长什么样我们不知道——CONVENTIONS §6）。
 * 2. **按 req_id 配对，不按帧类型**。pub 侧的 `room.offer` 是由 `room.answer`
 *    应答的（§3.3 固定 offerer），只看类型对不上号。
 * 3. **socket 藏在 Transport 后面**。engine 零 Qt 依赖，也不该把某个 WS 库
 *    焊死在信令逻辑里。
 *
 * 线程：本类**不是线程安全的**。所有方法（含 tick）要在同一个线程上调用，
 * Transport 的回调也必须投递到那个线程。
 */
class Connection : private TransportListener {
public:
  Connection(ConnectionOptions options, ConnectionEvents events);
  ~Connection() override;

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  /** connect 建立连接并发起握手。已连上或正在连时什么都不做。 */
  void connect(std::int64_t nowMs);

  /** close 主动关闭，**不会**触发重连。幂等。 */
  void close();

  /**
   * updateToken 换一枚新的接入票（旧票过期时用）。**下次连接生效，不打断当前连接**。
   *
   * 顺带把鉴权失败计数清零：换票就是「这次不一样了」的唯一信号，
   * 不清的话已经用光重试次数的连接换了新票也再没有机会试。
   */
  void updateToken(std::string token);

  /**
   * request 发一个请求并登记它的应答处理。
   *
   * 返回 false = 没发出去（连接不可用），此时 handler **不会**被调用。
   * 返回 true 时 handler 恰好被调用一次：成功、服务端 sys.error、超时或断线。
   */
  bool request(const std::string& type, const Json& data, std::int64_t nowMs,
               ResponseHandler handler);

  /** sendFrame 发一帧但不等应答（用于回应服务端主动事件，如 sub 的 answer）。 */
  void sendFrame(const std::string& type, const std::string& reqId, const Json& data,
                 std::int64_t nowMs);

  /** tick 推进时间：心跳、请求超时、重连全靠它。宿主按 ~1 秒的粒度调用即可。 */
  void tick(std::int64_t nowMs);

  ConnectionState state() const { return state_; }
  /** sessionId 是会话 id；重连时会带上它请求恢复（§1.4）。 */
  const std::string& sessionId() const { return sessionId_; }
  /** authFailures 是连续鉴权失败次数，供测试与诊断观察。 */
  int authFailures() const { return authFailures_; }
  /** reconnectAttempts 是当前退避档，供测试与诊断观察。 */
  int reconnectAttempts() const { return reconnectAttempt_; }
  /** pendingCount 是在途请求数，供测试与诊断观察。 */
  std::size_t pendingCount() const { return pending_.size(); }

private:
  /** kMaxAuthFailures 是连续几次 4401 之后彻底放弃（§1.5）。见 .cpp 里的长注释。 */
  static constexpr int kMaxAuthFailures = 3;
  /** 协议 §1.4 的恢复窗口：30 秒。**四端同一个值**，服务端的 ResumeWindow 也是它。 */
  static constexpr std::int64_t kResumeWindowSec = 30;
  /** 服务端判一条连接死掉要连续几个心跳周期收不到东西（§1.3）。 */
  static constexpr std::int64_t kServerDeathPings = 3;
  /** 余量：跨过服务端窗口到期那一刻再收场，别跟它抢同一秒。 */
  static constexpr std::int64_t kGiveUpGraceSec = 5;

  void onTransportOpen() override;
  void onTransportMessage(const std::string& raw) override;
  void onTransportClosed(int code, const std::string& reason) override;

  void sendHello(std::int64_t nowMs);
  void handleHelloOk(const RequestResult& result);
  /** dispatch 不检查连接状态——**sys.hello 本身就要在 connecting 状态下发出去**。 */
  bool dispatch(const std::string& type, const Json& data, std::int64_t nowMs,
                ResponseHandler handler);
  void dispatchEvent(const Envelope& envelope);
  Json decodeData(const Envelope& envelope) const;
  std::string nextReqId();
  /** wallNowMs 是信封 `ts` 用的墙上时间——与 tick 的单调时间线刻意分开。 */
  std::int64_t wallNowMs() const;
  void scheduleReconnect(std::int64_t nowMs);

  /*
    断开多久之后可以断定「服务端那一侧的会话没了」。

    # 为什么不是恢复窗口那 30 秒

    服务端的 30 秒**不是从我们断开的那一刻算起的**，是从**它自己察觉**的那一刻算起。
    而它靠读超时察觉：连续 3 个心跳周期收不到任何东西才判死（§1.3）。
    我们断开时距离上一帧最多一个心跳周期，所以最晚的到期时刻是
    `断开 + 3×ping + 30s`——按默认 15 秒心跳就是 45 + 30 = 75 秒，再加一点余量。

    # 为什么必须取上界

    取短了就会撒谎：真机 2026-09-08 实测，断开 14 秒后重连**成功恢复**，通话照常继续。
    在那之前宣布「通话已结束」是把一通还能救回来的电话杀掉，而且服务端还认为我们在房里，
    房间会挂着一个幽灵成员。**宁可让用户多看几十秒「正在重连」，也不能提前下结论。**
  */
  std::int64_t giveUpDelayMs() const;
  void emitError(std::int32_t code, const std::string& forType);

  ConnectionOptions options_;
  ConnectionEvents events_;

  std::unique_ptr<Transport> transport_;
  ConnectionState state_ = ConnectionState::Idle;
  std::string sessionId_;
  std::int64_t seq_ = 0;
  /** tick 喂进来的最后一个时刻。回调里要发帧时用得上（回调本身不带时间）。 */
  std::int64_t nowMs_ = 0;
  /** 服务端最近一次告知的心跳周期。[giveUpDelayMs] 要拿它推算服务端何时判死。 */
  std::int64_t pingIntervalSec_ = 15;
  /** 「服务端已经彻底放弃这条会话」的时刻。0 = 没在倒计时。 */
  std::int64_t unrecoverableAtMs_ = 0;

  PendingRequests pending_;
  Heartbeat heartbeat_;

  /** 连续鉴权失败次数。握手一成功就清零——只有**连续**失败才说明票是死的。 */
  int authFailures_ = 0;
  int reconnectAttempt_ = 0;
  /** 已排定的重连时刻；0 = 没排。 */
  std::int64_t reconnectAtMs_ = 0;
  /** 彻底放弃重连的闩（被踢、鉴权用尽、主动 close）。 */
  bool reconnectStopped_ = false;
};

}  // namespace imrtc
