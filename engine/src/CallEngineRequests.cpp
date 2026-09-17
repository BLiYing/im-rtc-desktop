#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "imrtc/CallEngine.h"
#include "imrtc/Errors.h"
#include "imrtc/Log.h"
#include "imrtc/MachineTypes.h"
#include "imrtc/Registry.h"

namespace imrtc {

/**
 * 发帧与**调用结果的结算**（server `docs/design/ACTION_RESULT_DESIGN.md`，2.0.0）。
 *
 * 与 CallEngine.cpp 拆开是体量红线（CONVENTIONS §3），也因为这一段自成一体：
 * 「一帧发出去之后，成败交给谁、状态机怎么收场」。门面那边只剩宿主方法到状态机输入的转译。
 *
 * # 两种来路
 *
 * - **宿主调用**（`request()`）：这次调用直接产出的请求帧挂一个 `Settlement`。失败交给调用方，
 *   **不再**发 onError；成功只管这几帧收到 `.ok`，应答落进状态机就结算，不等连锁帧。
 * - **找不到调用方**（下行帧、内部事件、媒体面自发的动作、连锁帧）：`settlement` 为 null，
 *   失败照旧发 onError（带 for_type）。
 *
 * 宿主没传回调（`done` 为空）时按第二种处理——失败退回 onError（R7），免得静默丢失。
 */
struct CallEngine::Settlement {
  /** 调用方的回调；空 = 失败退回 onError。 */
  ActionCompletion done;
  /** 还在飞的直接请求帧数。 */
  int pending = 0;
  /** 结果已经交出去（或已经退回 onError）。**恰好一次**靠它。 */
  bool delivered = false;
  /** 还在 `request()` 那次同步派发里：成功要等派发走完再判。 */
  bool dispatching = true;
  /** 成功时从应答 data 里取值的键，与取到的值。 */
  std::string valueKey;
  std::string value;
};

namespace {

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

/** isExitFrame：退出类的结束帧。它们没成也要本地收场（ACTION_RESULT_DESIGN D2）。 */
bool isExitFrame(const std::string& type) {
  return type == frame::kCallHangup || type == frame::kCallReject || type == frame::kCallCancel;
}

ActionResult failureOf(std::int32_t code, const std::string& forType) {
  return ActionResult{code, errorName(code), forType, ""};
}

}  // namespace

CallEngine::~CallEngine() {
  /*
    **还没交出结果的调用一律回 2005**（ACTION_RESULT_DESIGN R5：每次被受理的调用恰好回一次）。
    在途请求的应答处理随 Connection 一起析构，永远不会再被调到，不补的话宿主那边的回调就石沉大海。
    成员此刻都还活着；回调里再调 Engine 的方法是宿主的错（C ABI 那一层句柄已经在拆了）。
  */
  std::vector<std::shared_ptr<Settlement>> open;
  open.swap(openSettlements_);
  if (loginSettlement_) open.push_back(std::move(loginSettlement_));
  const std::int32_t code = codeValue(ErrorCode::InvalidState);
  for (const std::shared_ptr<Settlement>& settlement : open) {
    if (settlement->delivered || !settlement->done) continue;
    settlement->delivered = true;
    ActionCompletion done = std::move(settlement->done);
    done(ActionResult{code, errorName(code), "", ""});
  }
}

void CallEngine::beginLogin(ActionCompletion done) {
  loginSettlement_ = std::make_shared<Settlement>();
  loginSettlement_->done = std::move(done);
}

void CallEngine::request(const MachineInput& input, ActionCompletion done,
                         const std::string& valueKey) {
  auto settlement = std::make_shared<Settlement>();
  settlement->done = std::move(done);
  settlement->valueKey = valueKey;
  // 顺手清掉已经结算过的，表只留还在等的那几条。
  openSettlements_.erase(
      std::remove_if(openSettlements_.begin(), openSettlements_.end(),
                     [](const std::shared_ptr<Settlement>& open) { return open->delivered; }),
      openSettlements_.end());
  openSettlements_.push_back(settlement);

  EngineOutput output = reduceEngine(context_, input, options_.clock());
  LocalReject reject = output.reject;
  if (reject.rejected()) {
    // 与 Web `FrameLoop.logLocalReject` 同一条：宿主拿到的只有一个码，哪个动作、什么状态进日志。
    log(LogLevel::Warn, "动作被状态机本地拒绝",
        {{"op", input.name},
         {"call_state", callStateName(context_.call.state)},
         {"room_state", roomStateName(context_.room.state)}});
  }
  dispatchOutput(std::move(output), "", settlement);
  settlement->dispatching = false;

  if (reject.rejected()) {
    // call 的参数本地拒绝（1004）说的是那一帧没发出去；状态非法（2005）不对应哪一帧。
    const std::string forType =
        input.name == "call" && reject.code == codeValue(ErrorCode::BadParams) ? frame::kCallInvite : "";
    settleFailure(settlement, ActionResult{reject.code, reject.name, forType, ""});
    return;
  }
  // 没有直接请求帧（意图被缓存、拨出中挂起的 cancel）：调用已受理，立即成功。
  if (settlement->pending == 0) settleSuccess(settlement);
}

void CallEngine::rejectLocally(ActionCompletion done, const std::string& forType) {
  auto settlement = std::make_shared<Settlement>();
  settlement->done = std::move(done);
  settlement->dispatching = false;
  settleFailure(settlement, failureOf(codeValue(ErrorCode::BadParams), forType));
}

void CallEngine::settleFailure(const std::shared_ptr<Settlement>& settlement,
                               const ActionResult& result) {
  if (!settlement || !settlement->done || settlement->delivered) {
    // 找不到调用方、没传回调、或调用方已经拿到过一个失败：发 onError，一个错误只报一次。
    if (settlement) settlement->delivered = true;
    emitError(result.code, result.forType);
    return;
  }
  settlement->delivered = true;
  deliverOrDefer(settlement, result);
}

void CallEngine::settleSuccess(const std::shared_ptr<Settlement>& settlement) {
  if (settlement->delivered) return;
  settlement->delivered = true;
  if (!settlement->done) return;
  deliverOrDefer(settlement, ActionResult{0, "", "", settlement->value});
}

void CallEngine::deliverOrDefer(const std::shared_ptr<Settlement>& settlement, ActionResult result) {
  if (sendDepth_ > 0) {
    // 这一轮的事件还没抛：结果排到它们后面（见 dispatchOutput）。
    deferredResults_.emplace_back(settlement, std::move(result));
    return;
  }
  if (!settlement->done) return;
  // 先搬出来再调：回调里宿主可能再调 Engine，而这条记账不该再被碰到。
  ActionCompletion done = std::move(settlement->done);
  settlement->done = nullptr;
  done(result);
}

void CallEngine::emitError(std::int32_t code, const std::string& forType) {
  /*
    走 emitOrDefer 而不是直接调 observer：失败几乎总是在**某一层的发帧循环里**
    被发现（帧没发出去才叫失败）。直接抛的话，这条错误会跑到「它所属的那次状态转移」
    自己的事件前面去——宿主先看见 onError，才看见 onCallBegin。见 dispatchOutput。
  */
  Json args = Json::makeObject();
  args.set("code", Json::make(static_cast<std::int64_t>(code)));
  args.set("name", Json::make(errorName(code)));
  args.set("for_type", Json::make(forType));
  emitOrDefer(EmittedEvent{"onError", args});
}

void CallEngine::sendOne(const OutgoingFrame& frame, const std::string& replyReqId,
                         const std::shared_ptr<Settlement>& settlement) {
  if (!connection_) {
    /*
      还没 login 就调了业务方法。状态机已经把状态推过去了（比如进了 inviting），
      而这一帧根本没地方发——**必须补一次失败**，否则通话永远停在 inviting，
      界面「正在呼叫…」转个不停，之后每次挂断都发向一个不存在的 call。

      早先这里是 `if (!connection_) return;`，静默吞掉。ABI 冒烟测试
      「没登录就拨号」把它抓了出来。
    */
    failLocally(frame.type, frame.data, codeValue(ErrorCode::NotLoggedIn), settlement);
    return;
  }
  const std::int64_t now = options_.clock();

  // 状态机产出的 SDP 帧里 sdp 是空串——它不认识 libwebrtc。媒体面把它接管过去，
  // 异步拿到真正的 SDP 再发（见 MediaPlane::fillSdp）。
  if (media_ && media_->fillSdp(frame.type, replyReqId, frame.data)) return;

  if (!isOutgoingRequest(frame)) {
    // 不是请求就是「别人请求的应答」（当前只有 sub 侧的 room.answer），回显对方的 req_id。
    connection_->sendFrame(frame.type, replyReqId, frame.data, now);
    return;
  }

  const std::string type = frame.type;
  // 先记上再发：万一应答同步回来，结算时这一帧已经在账上。
  if (settlement) ++settlement->pending;
  const bool sent = connection_->request(
      type, frame.data, now,
      [this, type, data = frame.data, settlement](const RequestResult& result) {
        if (settlement) --settlement->pending;
        if (!result.ok) {
          onRequestFailed(type, data, result, settlement);
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
        handleIncoming(result.envelope.type, "", result.data);
        /*
          应答落进状态机就结算（R2 / D1）：它连锁出来的帧（join.ok → 重放缓存的发布……）
          有自己的出口，失败走 onError，不算这次调用的。
        */
        if (!settlement) return;
        if (!settlement->valueKey.empty()) settlement->value = str(result.data, settlement->valueKey);
        if (!settlement->dispatching && settlement->pending == 0) settleSuccess(settlement);
      });
  if (!sent) {
    if (settlement) --settlement->pending;
    // 连接不可用时 request 不会回调，但状态机已经把状态推过去了。这一帧**根本没上线路**，
    // 所以必须当作彻底失败：否则通话会永远停在 inviting，之后每次挂断都发向一个
    // 不存在的 call（换回 1401，永远退不出去）。
    failLocally(type, frame.data, codeValue(ErrorCode::NetworkUnreachable), settlement);
  }
}

void CallEngine::onRequestFailed(const std::string& type, const Json& data,
                                 const RequestResult& result,
                                 const std::shared_ptr<Settlement>& settlement) {
  /*
    **先放上行协商的闸，再谈要不要报错。**

    这一条在 tearingDown_ 与 2003 两个 return 之前：那两条 return 说的是
    「这次失败不该打扰宿主」，而闸门是引擎自己的记账——offer 失败了 answer 就不会
    再来，不放闸的话上行从此协商不出去，而且没有任何症状可查。
  */
  if (media_ && type == frame::kRoomOffer) media_->releasePubOffer();

  const ActionResult failure = failureOf(result.errorCode, type);
  const bool hasCaller = settlement && settlement->done && !settlement->delivered;
  /*
    拆除期间的失败是我们自己造成的，不往 onError 传（见 logout() 的注释）——
    **但调用方的结果照样要回**：每次被受理的调用恰好回一次（R5）。
  */
  if (tearingDown_) {
    if (hasCaller) settleFailure(settlement, failure);
    return;
  }
  /*
    **断线导致的失败不回滚**。协议 §1.4 规定断开期间通话要保持在
    connected 并展示「正在重连…」，成不成由随后的 `sys.hello.ok` 的 resumed 裁决：
    resumed=true 就接着打，false 才合成 onCallEnd(network)（不变量 I8）。

    在这里把在途的 call.invite / room.join 当成失败，会在**断线的瞬间**就把通话
    拆掉——onDisconnected 之前先冒出一条 onCallEnd(error)，界面直接收场，
    而重连成功后那通电话其实还在。onDisconnected 已经是断线的信号，
    这里再报一条 2003 onError 只是噪声。

    两个例外：
    - **调用方要拿到结果**（2003）：那一帧的成败确实不知道了，调用方不能永远等下去。
    - **退出类照样本地收场**（D2）：用户按的是「结束」，断线不该让界面停在通话里。
  */
  if (result.errorCode == codeValue(ErrorCode::NetworkUnreachable)) {
    if (isExitFrame(type) || type == frame::kRoomLeave) rollback(type, data);
    if (hasCaller) settleFailure(settlement, failure);
    return;
  }

  failLocally(type, data, result.errorCode, settlement);
}

void CallEngine::failLocally(const std::string& type, const Json& data, std::int32_t code,
                             const std::shared_ptr<Settlement>& settlement) {
  const ActionResult failure = failureOf(code, type);
  if (settlement && settlement->done && !settlement->delivered) {
    // 有调用方：先让状态机收场（onCallEnd / onRoomLeft 照发），结果排在这些事件之后。
    rollback(type, data);
    settleFailure(settlement, failure);
    return;
  }
  // 找不到调用方：onError 在前、收场事件在后，与 2.0.0 之前同序。
  settleFailure(settlement, failure);
  rollback(type, data);
}

void CallEngine::rollback(const std::string& type, const Json& data) {
  /*
    几个帧的失败必须让状态机退回 idle，否则界面永远收不了场：

    - call.invite / call.accept / call.join 失败 → 通话机停在 inviting / accepting，
      界面「正在呼叫…」「接通中…」转个不停，而那通电话服务端根本没接纳我们；
      之后每次挂断都换回 1401，**永远退不出去**。
      （Web 端实测：群呼把主叫自己也放进了 callee_ids，服务端回 1004，
      然后连点五次挂断全是 1401。accept / join 两条与 Web `FrameLoop.rollback` 对齐。）
    - call.hangup / call.reject / call.cancel 失败 → **本地照样收场**（ACTION_RESULT_DESIGN D2）：
      用户按的是「结束」，服务端拒了（最常见的是通话已经结束 1402 / 1401）或根本没发出去，
      都不该让界面停在通话里。与 forceEnd 同一份收场计算，只是不再发帧。
    - room.join 失败 → 房间机停在 joining，之后每次 publish 都被 R1 拒成 2005，
      界面停在「正在进入会议…」。
    - room.leave 失败 → 房间机停在 leaving，**媒体停不掉**（摄像头与前台资源一直开着），
      之后 leave 被 R1 拒成 2005、join 因为「不在 idle」也被拒——除非 logout，
      这台 Engine 再也进不了任何房间。而服务端回 1203 的语义恰恰是
      **我们已经不在房里了**，正是最该收场的时刻。

    其余帧的失败只报错：它们不改变「有没有一通电话 / 在不在房里」。
    **请求超时（2004）走的也是这条路**——十秒没应答，那通电话确实没建起来。

    这条推理漏了一维——**有没有在推流**（静默失败审计 §A）：`room.publish` /
    `room.subscribe` 被拒（或没送到）原先谁都不认，那条轨道永远停在 publishing /
    subscribing：`.ok` 不会来，上行从未协商、下行订阅永久悬空，界面显示已接通、
    对方全程听不见看不见，零提示。
    - `room.publish` 通话里（`call.state != idle`）被拒：直接强制收场整通电话，
      reason=error——留在通话里只报错也救不回来，服务端会拒的几种情形（房间没了、
      重复发布、请求超时）重试也没用。复用 `call_failed` 的合成路径（CallMachine.cpp
      按此刻状态挑该发的结束帧，含 call.hangup）。
      没有通话（会议房）时只摘掉那条 publishing 记账，不收场、不额外抛回调。
    - `room.subscribe` 被拒只摘记账，不收场：最常见的 1301 是订阅与对方停推赛跑输了，
      通话本身没事——留着不摘的话不变量 R3 会把之后每次重订都当成换层，
      再也发不出 room.subscribe。
  */
  if (type == frame::kCallInvite || type == frame::kCallAccept || type == frame::kCallJoin) {
    apply(MachineInput::internal("call_failed"), "");
  } else if (isExitFrame(type)) {
    endLocally();
  } else if (type == frame::kRoomJoin) {
    apply(MachineInput::internal("join_failed"), "");
  } else if (type == frame::kRoomLeave) {
    apply(MachineInput::internal("leave_failed"), "");
    // 已经不在 leaving（比如等应答期间断线进了 reconnecting）时 leave_failed 不认——
    // 这一帧只可能是宿主要离房才发的，没有通话时照样本地收场。
    if (context_.room.state != RoomState::Idle && context_.call.state == CallState::Idle) endLocally();
  } else if (type == frame::kRoomPublish) {
    if (context_.call.state != CallState::Idle) {
      log(LogLevel::Warn, "发布被拒，结束本端通话", {{logfield::kCallId, context_.call.callId}});
      apply(MachineInput::internal("call_failed"), "");
    } else {
      apply(MachineInput::internal("publish_failed", obj({{"cid", Json::make(str(data, "cid"))}})),
           "");
    }
  } else if (type == frame::kRoomSubscribe) {
    apply(MachineInput::internal("subscribe_failed",
                                 obj({{"track_id", Json::make(str(data, "track_id"))}})),
         "");
  }
}

void CallEngine::endLocally() {
  EngineOutput output = imrtc::forceEnd(context_, options_.clock(), callStartedAtMs_);
  if (output.emit.empty()) return;
  log(LogLevel::Warn, "结束帧失败，本地收场",
      {{logfield::kCallId, context_.call.callId}, {logfield::kRoomId, context_.room.roomId}});
  // 结束帧已经试过了：只落状态与事件，不再发一遍。
  output.send.clear();
  dispatchOutput(std::move(output), "");
}

void CallEngine::settleLogin(const ActionResult& result) {
  if (!loginSettlement_) return;
  std::shared_ptr<Settlement> settlement = std::move(loginSettlement_);
  loginSettlement_.reset();
  settlement->dispatching = false;
  if (result.ok()) {
    settlement->value = result.value;
    settleSuccess(settlement);
    return;
  }
  settleFailure(settlement, result);
}

}  // namespace imrtc
