#include <string>

#include "imrtc/CallEngine.h"
#include "imrtc/Errors.h"
#include "imrtc/Handshake.h"
#include "imrtc/MachineTypes.h"
#include "imrtc/Registry.h"

namespace imrtc {

/**
 * 会话装配：login / logout / updateToken —— 「一条连接怎么建起来、怎么拆干净」。
 *
 * 与 CallEngine.cpp 拆开是体量红线（CONVENTIONS §3，那边已经 589/600），也因为这一段
 * 与状态机那部分**几乎没有耦合**：它只做两件事——把 options_ 摊成 ConnectionOptions、
 * 把连接层的六个回调接到 `apply()` 上——之后所有判断都在状态机里，这里不再参与。
 *
 * 挑这一刀而不是别处，是因为拆完之后两边各自内聚：门面那边剩下的是「宿主动作 →
 * 状态机输入」的转译与出帧，这边是连接的生老病死。**别把新的业务判断加到这里**，
 * 那属于状态机；这里只负责接线。
 */

void CallEngine::login(const std::string& token, ActionCompletion done) {
  // 上一次 login 还没等到握手就又来一次：那一次的结果回 2005，不许悬着（R5）。
  settleLogin(ActionResult{codeValue(ErrorCode::InvalidState), errorName(codeValue(ErrorCode::InvalidState)),
                           frame::kHello, ""});
  /*
    `device_id` 在**开 socket 之前**校验（协议 §2.5）。

    不拦的症状是「登录失败，没有下文」：服务端一律回 1004，而它那句说得很清楚的
    「device_id 只允许 [A-Za-z0-9_-]」到不了宿主手里，宿主看到的只有一个 bad_params。
    Android 真机上踩过一次——`Build.MODEL` 就是 `Pixel 2 XL`，带空格。

    抛的是 `bad_params`(1004)，**和服务端拒绝时同一个码**，宿主不用为「本地拦的」
    和「服务端拒的」写两遍分支。C ABI 那一层还会更早地同步拒掉（engine_create），
    这一条守的是直接用 C++ 门面的那条路。
  */
  if (!deviceIdValid(options_.deviceId)) {
    rejectLocally(std::move(done), frame::kHello);
    return;
  }
  beginLogin(std::move(done));
  ticket_ = token;

  ConnectionOptions connectionOptions;
  connectionOptions.url = options_.url;
  connectionOptions.token = token;
  connectionOptions.deviceId = options_.deviceId;
  connectionOptions.sdk = options_.sdk;
  connectionOptions.requestTimeoutMs = options_.requestTimeoutMs;
  connectionOptions.transportFactory = options_.transportFactory;
  connectionOptions.random = options_.random;
  connectionOptions.wallClock = options_.wallClock;

  ConnectionEvents events;
  events.onConnected = [this](const HelloOk& hello) {
    /*
      Connection 已经把 sys.hello.ok 当作握手请求的应答吃掉了，但状态机也要看它：
      resumed=false 意味着服务端那边的会话已经过期，房间与通话都得归零，
      而且要本地合成一条 onCallEnd(network)（不变量 I8）。所以这里把它**还原成
      一帧**再喂进去——两边看到的是同一件事，不必各写一套判断。
    */
    Json data = Json::makeObject();
    data.set("uid", Json::make(hello.uid));
    data.set("device_id", Json::make(hello.deviceId));
    data.set("session_id", Json::make(hello.sessionId));
    data.set("resumed", Json::make(hello.resumed));
    uid_ = hello.uid;
    apply(MachineInput::recv(frame::kHelloOk, data), "");
    settleLogin(ActionResult{0, "", "", hello.sessionId});

    /*
      **恢复成功之后补一次上行协商**（协议 §1.4）。

      顺序要紧：`apply` 先跑完，房间机才从 reconnecting 回到 joined，
      这一帧才发得出去——反过来就正好撞上那条「不进缓冲、直接被拒」的路。
      `resumed=false` 那条不走这里：房间已经归零，没有上行可谈。
    */
    if (hello.resumed && media_) media_->renegotiateAfterResume();
  };
  events.onKickedOut = [this](KickedReason reason) {
    kickedOutPending_ = true;
    kickedReason_ = reason;
  };
  events.onSessionUnrecoverable = [this]() {
    if (tearingDown_) return;
    // 与「重连上了但 resumed=false」同一件事，只是不必等重连成功。
    apply(MachineInput::internal("session_unrecoverable"), "");
  };
  events.onDisconnected = [this](int code, const std::string&, bool willReconnect) {
    if (tearingDown_) return;
    const bool kicked = kickedOutPending_ || code == closecode::kKickedOut;
    kickedOutPending_ = false;
    // 供 emitEvent 在派发 onDisconnected 前补进 args（状态机那条 emit 不知道这两样）。
    lastDisconnectCode_ = code;
    lastDisconnectWillReconnect_ = willReconnect;
    // 首次握手之前连接就断了：login 回 2003（握手被拒的那条已经在 onError 里结算过）。
    // 这张票没换到会话，别留着——否则 fetchCallHistory 会拿它去撞 401，而不是回 2007。
    if (loginSettlement_) ticket_.clear();
    settleLogin(ActionResult{codeValue(ErrorCode::NetworkUnreachable),
                             errorName(codeValue(ErrorCode::NetworkUnreachable)), frame::kHello, ""});
    // ws_closed_4403 会抛 onKickedOut + onDisconnected 并把一切清空；
    // 普通断开只进 reconnecting，通话要保持在 connected 并展示「正在重连…」（§1.4）。
    apply(MachineInput::internal(kicked ? "ws_closed_4403" : "disconnected"), "");
  };
  events.onEvent = [this](const std::string& type, const Json& data, const Envelope& envelope) {
    handleIncoming(type, envelope.reqId, data);
  };
  events.onError = [this](std::int32_t code, const std::string& name, const std::string& forType) {
    if (tearingDown_) return;
    // 首次握手被拒：这是 login 这次调用的结果，交给调用方（没传回调时 settleLogin 退回 onError）。
    if (loginSettlement_ && forType == frame::kHello) {
      ticket_.clear();  // 票被拒，同上：fetchCallHistory 应回 2007
      settleLogin(ActionResult{code, name, forType, ""});
      return;
    }
    if (const std::shared_ptr<CallEngineObserver> target = observer()) {
      target->onError(code, name, forType);
    }
  };

  connection_.reset(new Connection(connectionOptions, events));
  if (media_) media_->attach();
  connection_->connect(options_.clock());
}

void CallEngine::logout() {
  /*
    拆除期间连接层会同步抛两样东西上来，**都不该传给宿主**：

    - `onDisconnected`：宿主自己点的退出，再告诉它一次「断开了」是噪声，
      有的界面还会据此闪一下「正在重连…」。
    - 在途请求的失败：`close()` 会把它们全部结算掉。要是这时候在途的是
      `room.join`，那条失败会被当成「进房失败」，凭空多抛一条 onRoomLeft。

    真机 smoke 跑出来的就是这个：一次 logout 冒出 error:2005 + roomLeft + disconnected
    三条，而宿主只想要一条通话终局。
  */
  tearingDown_ = true;
  ticket_.clear();
  // 还在等握手就 logout：login 回 2005。
  settleLogin(ActionResult{codeValue(ErrorCode::InvalidState), errorName(codeValue(ErrorCode::InvalidState)),
                           frame::kHello, ""});
  if (connection_) {
    connection_->close();
    /*
      **光靠 tearingDown_ 这个瞬时标记挡不住它**，必须把连接一起放掉。

      真实的 Transport 是**异步**投递关闭事件的：IxTransport::close() 里 ws_->stop()
      只是让 IX 的后台线程把一条 Closed 排进队列，要到下一次 tick() 的 poll() 才放出来。
      那时候本函数早已返回、标记也早已清零，于是上面那条 onDisconnected 照样传给宿主——
      恰恰是这段注释说要挡掉的东西。

      测试里看不出来：FakeTransport::close() 是**同步**回调 listener 的，标记还是 true。
      换句话说这道闸只在假的传输上成立。

      ~Connection 会先摘监听再析构 Transport，ws_->stop() 阻塞到后台线程 join——
      队列里攒着的事件随之一起丢掉，返回之后不会再有任何回调。下一次 login() 会重建。
    */
    connection_.reset();
  }
  /*
    用 `reset` 而不是 `ws_closed_4403`：**主动登出不是被踢**。走后者会抛一条
    onKickedOut，宿主据此弹「您的账号在别处登录」——用户自己点的退出，
    却收到一条说别人挤掉了他的提示。（真机 smoke 第一次跑就撞上了这个。）

    reset 只清状态，并在通话中时本地合成一条 onCallEnd。
  */
  tearingDown_ = false;
  if (media_) media_->close();
  apply(MachineInput::internal("reset"), "");
}

void CallEngine::updateToken(const std::string& token) {
  if (connection_) ticket_ = token;
  if (connection_) connection_->updateToken(token);
}

void CallEngine::setAppForeground(bool foreground) {
  if (connection_) connection_->appForeground(foreground, options_.clock());
}

void CallEngine::notifyNetworkChanged() {
  if (connection_) connection_->networkChanged(options_.clock());
}

}  // namespace imrtc
