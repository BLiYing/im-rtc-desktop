#include "imrtc/CallEngine.h"

#include <chrono>
#include <utility>

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

std::int64_t systemClock() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

CallEngine::CallEngine(CallEngineOptions options) : options_(std::move(options)) {
  if (!options_.clock) options_.clock = &systemClock;
}

CallEngine::~CallEngine() = default;

void CallEngine::setObserver(std::weak_ptr<CallEngineObserver> observer) {
  observer_ = std::move(observer);
}

void CallEngine::login(const std::string& token) {
  ConnectionOptions connectionOptions;
  connectionOptions.url = options_.url;
  connectionOptions.token = token;
  connectionOptions.deviceId = options_.deviceId;
  connectionOptions.sdk = options_.sdk;
  connectionOptions.requestTimeoutMs = options_.requestTimeoutMs;
  connectionOptions.transportFactory = options_.transportFactory;
  connectionOptions.random = options_.random;

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
    apply(MachineInput::recv(frame::kHelloOk, data), "");
  };
  events.onKickedOut = [this]() { kickedOutPending_ = true; };
  events.onDisconnected = [this](int code, const std::string&, bool) {
    if (tearingDown_) return;
    const bool kicked = kickedOutPending_ || code == closecode::kKickedOut;
    kickedOutPending_ = false;
    // ws_closed_4403 会抛 onKickedOut + onDisconnected 并把一切清空；
    // 普通断开只进 reconnecting，通话要保持在 connected 并展示「正在重连…」（§1.4）。
    apply(MachineInput::internal(kicked ? "ws_closed_4403" : "disconnected"), "");
  };
  events.onEvent = [this](const std::string& type, const Json& data, const Envelope& envelope) {
    // **非请求帧的应答要回显对方的 req_id**：sub 侧的 offer 是服务端发起的请求，
    // 我们的 room.answer 就是它的应答（§3.3）。状态机不记 req_id，由这里带上。
    apply(MachineInput::recv(type, data), envelope.reqId);
  };
  events.onError = [this](std::int32_t code, const std::string& name, const std::string& forType) {
    if (tearingDown_) return;
    if (const std::shared_ptr<CallEngineObserver> target = observer()) {
      target->onError(code, name, forType);
    }
  };

  connection_.reset(new Connection(connectionOptions, events));
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
  if (connection_) connection_->close();
  /*
    用 `reset` 而不是 `ws_closed_4403`：**主动登出不是被踢**。走后者会抛一条
    onKickedOut，宿主据此弹「您的账号在别处登录」——用户自己点的退出，
    却收到一条说别人挤掉了他的提示。（真机 smoke 第一次跑就撞上了这个。）

    reset 只清状态，并在通话中时本地合成一条 onCallEnd。
  */
  tearingDown_ = false;
  apply(MachineInput::internal("reset"), "");
}

void CallEngine::updateToken(const std::string& token) {
  if (connection_) connection_->updateToken(token);
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

void CallEngine::notifyMediaReady() { apply(MachineInput::internal("media_ready"), ""); }

void CallEngine::tick() {
  if (connection_) connection_->tick(options_.clock());
}

ConnectionState CallEngine::connectionState() const {
  return connection_ ? connection_->state() : ConnectionState::Idle;
}

const std::string& CallEngine::sessionId() const {
  return connection_ ? connection_->sessionId() : kEmptyString;
}

void CallEngine::apply(const MachineInput& input, const std::string& replyReqId) {
  dispatchOutput(reduceEngine(context_, input, options_.clock()), replyReqId);
}

void CallEngine::dispatchOutput(EngineOutput output, const std::string& replyReqId) {
  context_ = std::move(output.state);

  // **先发帧再抛回调**：回调里宿主很可能立刻再调 Engine（比如 onCallBegin 里就开麦），
  // 那时状态已经是新的、该发的帧也已经在路上，不会出现「回调看到的状态比线路超前」。
  for (const OutgoingFrame& frame : output.send) sendOne(frame, replyReqId);
  for (const EmittedEvent& event : output.emit) emitEvent(event);
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
  if (!connection_) return;
  const std::int64_t now = options_.clock();

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
        apply(MachineInput::recv(result.envelope.type, result.data), "");
      });
  if (!sent) {
    // 连接不可用时 request 不会回调，但状态机已经把状态推过去了。这一帧**根本没上线路**，
    // 所以必须当作彻底失败：否则通话会永远停在 inviting，之后每次挂断都发向一个
    // 不存在的 call（换回 1401，永远退不出去）。
    failLocally(type, codeValue(ErrorCode::NetworkUnreachable));
  }
}

void CallEngine::onRequestFailed(const std::string& type, const RequestResult& result) {
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
  if (const std::shared_ptr<CallEngineObserver> target = observer()) {
    target->onError(code, errorName(code), type);
  }

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
