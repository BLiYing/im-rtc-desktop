#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/Handshake.h"
#include "imrtc/Reasons.h"

namespace imrtc {

/**
 * 回调总表：**五端同名**（设计文档 §7.5）。
 *
 * 「只引 Engine 自画 UI」这条路的全部内容就是这张表——宿主实现它，拿到事件后
 * 自行决定界面。将来的 Kit 与 Qt Demo 都只是这张表的消费者，**没有任何私有通道**。
 *
 * # 参数为什么是小结构体而不是 Json
 *
 * 公开面不泄漏内部类型（CONVENTIONS §1），而且这些结构体到了 C ABI 那一层
 * 正好一对一映射成 POD——现在图省事传 Json，第五刀就得把每个回调重新设计一遍。
 *
 * # 生命周期
 *
 * Engine 持有的是 `weak_ptr`：观察者先死、回调后到是 C++ 端的头号崩因
 * （CONVENTIONS §5），所以宿主必须用 `shared_ptr` 持有自己的观察者，
 * 放手即自动注销。所有方法都有空实现，只覆盖关心的那几个即可。
 */

/** Speaker 是一个正在说话的人。volume 0~100，**整数**。 */
struct Speaker {
  std::string uid;
  std::string participantId;
  std::int64_t volume = 0;
};

/** QualityEntry 是一个人的网络质量，level 0~6（0 = unknown）。 */
struct QualityEntry {
  std::string uid;
  std::string participantId;
  std::int64_t level = 0;
};

/** CallInvite 对应 `onCallReceived`。 */
struct CallInvite {
  std::string callId;
  std::string caller;
  /** **原样带上**：群通话里被叫要靠它摆占位格。 */
  std::vector<std::string> calleeIds;
  std::string mediaType;
  bool isGroup = false;
  /** 宿主自己的群号，可空（HOST_INTEGRATION_DESIGN §3.2，2026-09-15）。 */
  std::string chatGroupId;
  /** 宿主私有字节，原样透传，Engine 不解析。 */
  std::string userData;
  /**
   * 这次邀请是谁发的（2026-09-16）。**与 `caller` 不是一回事**：`caller` 恒为这通电话的
   * 发起人，而群通话里被别人 `invite_more` 拉进来时，`inviter` 才是按下「添加成员」的那个人
   * （离场的发起人被拉回来时，`caller` 就是他自己）。界面上「谁邀请你」要显示这个。
   * 服务端没带（旧版本）时 Engine 已回落成 `caller`，宿主不用自己兜底。
   */
  std::string inviter;
  /**
   * 此刻已经在通话里的人（2026-09-20，不含自己）。展开页据此把他们摆成正常格子，
   * `calleeIds` 里不在其中的才是「呼叫中…」。服务端没带（旧版本）时为空，宿主回落成只有 `caller`。
   * 进房时引擎会对这份名单对账：响铃阶段离场的人补一条 `onUserLeave`。
   */
  std::vector<std::string> joinedIds;
};

/** CallBegin 对应 `onCallBegin`：双方在这一刻同时开始计时。 */
struct CallBegin {
  std::string callId;
  std::string roomId;
  std::string mediaType;
  bool isGroup = false;
  /** "caller" / "callee"。 */
  std::string role;
  /**
   * 发起人。取 `call.connected` 里的值，为空时回落到本通 `call.incoming` /
   * `call()` 选项记下的值（§3.3）；`call.join` 加入的人没收过 `call.incoming`，
   * 只能靠 `call.connected` 自带。
   */
  std::string caller;
  /** 同上的回落规则，宿主自己的群号（§3.2）。 */
  std::string chatGroupId;
  /** 同上的回落规则，宿主私有字节。 */
  std::string userData;
};

/** CallEnd 对应 `onCallEnd`——**所有结束分支的唯一出口**。 */
struct CallEnd {
  std::string callId;
  /** §6 的封闭枚举，陌生值已折成 "error"。 */
  std::string reason;
  /** 未接通恒为 0。**客户端不自己算时长**，一律用服务端给的值（不变量 I8）。 */
  std::int64_t durationSec = 0;
  /** 动作发起人 uid；服务端自行判定时为空串。 */
  std::string endedBy;
  /**
   * `reason` 的类型化版本（2026-09-15 追加），与 C ABI 的 `imrtc_v1_end_reason`
   * 一一对应。**同一份数据的另一种形状**，不是新信息——陌生值同样折成 `Error`。
   */
  EndReason reasonCode = EndReason::Error;
};

/**
 * CallSummary 对应 `onCallSummary`（通话记录设计 §4）：这通电话的事实一次给齐。
 * **紧跟 `onCallEnd`、每通拿到 call_id 的电话恰好一次**；未接通、被拒、`*_elsewhere` 也来。
 * 宿主要发通话记录消息的话，只在 `role == "caller"` 时发，不用自己比对 uid。
 * 本地就地拒掉 / 发不出去的 `call()` 不触发。
 */
struct CallSummary {
  std::string callId;
  /** §6 的封闭枚举，陌生值已折成 "error"。 */
  std::string reason;
  EndReason reasonCode = EndReason::Error;
  /** 服务端给的秒数，未接通恒 0。 */
  std::int64_t durationSec = 0;
  std::string endedBy;
  /** "audio" / "video"。 */
  std::string mediaType;
  bool isGroup = false;
  std::string chatGroupId;
  std::string caller;
  /** "caller" / "callee"。 */
  std::string role;
  /** 1v1 的对端 uid；群通话为空串。 */
  std::string peer;
  /** 主叫拨号时透传的宿主私有字符串，原样返回。 */
  std::string userData;
};

/** CallMissed 对应 `onCallMissed`：通话中被第三个人呼叫，服务端已自动回了忙线。 */
struct CallMissed {
  std::string callId;
  std::string caller;
  std::string reason;
};

class CallEngineObserver {
public:
  virtual ~CallEngineObserver() = default;

  // ---- 连接 ----
  /** 信令通道建立。resumed=true 表示恢复了旧会话，房间成员关系还在。 */
  virtual void onConnected(const std::string& sessionId, bool resumed) {
    (void)sessionId;
    (void)resumed;
  }
  /**
   * 信令通道断开。Engine 会按退避档自动重连，除非关闭码明说不该重连。
   *
   * `code` 是 WebSocket 关闭码，`willReconnect` 是引擎判断的这次断开会不会自动
   * 重连（2026-09-15 追加，desktop 的 `Connection` 层本来就知道这两样）。
   */
  virtual void onDisconnected(std::int32_t code, bool willReconnect) {
    (void)code;
    (void)willReconnect;
  }
  /**
   * 被踢、鉴权连续失败到上限、或握手被拒。**不会自动重连，只能重新 login。**
   *
   * `reason` 决定宿主该做什么：`TakenOver` 回登录页、`AuthExpired` 换一枚票再来、
   * `ConfigRejected` 去改配置。**三者不许合并**——把「换票就能好」报成「去改配置」，
   * 宿主只能把可以静默恢复的场景也弹成「请重新登录」。
   */
  virtual void onKickedOut(KickedReason reason) { (void)reason; }
  /** 任意内部错误。`name` 是机读名，`forType` 是出错的请求帧。 */
  virtual void onError(std::int32_t code, const std::string& name, const std::string& forType) {
    (void)code;
    (void)name;
    (void)forType;
  }

  // ---- 来电与拨出 ----
  /** 收到邀请（被叫）。 */
  virtual void onCallReceived(const CallInvite& invite) { (void)invite; }
  /** 通话接通，主被叫都抛。 */
  virtual void onCallBegin(const CallBegin& begin) { (void)begin; }
  /** **所有结束分支的唯一出口**。宿主只监听它也必须能完整记录一通电话。 */
  virtual void onCallEnd(const CallEnd& end) { (void)end; }
  /** 紧跟 `onCallEnd` 的通话事实汇总，见 `CallSummary`。 */
  virtual void onCallSummary(const CallSummary& summary) { (void)summary; }
  /** 通话中有人打进来、已被自动回忙线。界面据此提示一句谁来过电话。 */
  virtual void onCallMissed(const CallMissed& missed) { (void)missed; }
  /** 便利事件：主叫取消。**随后必有一条 onCallEnd**（不变量 I2）。 */
  virtual void onCallCancelled(const std::string& uid) { (void)uid; }
  /** 便利事件：被叫拒接。**只在 1v1 抛**（不变量 I7）。 */
  virtual void onCallRejected(const std::string& uid) { (void)uid; }
  /** 便利事件：被叫忙线。**只在 1v1 抛**。 */
  virtual void onCallBusy(const std::string& uid) { (void)uid; }
  /** 便利事件：被叫超时未接。**只在 1v1 抛**。 */
  virtual void onCallNoAnswer(const std::string& uid) { (void)uid; }
  /** 本账号另一台设备接听/拒绝了这通电话。action 取 "accept" / "reject"。 */
  virtual void onHandledOnOtherDevice(const std::string& callId, const std::string& action) {
    (void)callId;
    (void)action;
  }

  // ---- 成员 ----
  /** 有人进房。 */
  virtual void onUserEnter(const std::string& uid) { (void)uid; }
  /** 有人离房。 */
  virtual void onUserLeave(const std::string& uid) { (void)uid; }
  /**
   * 某人的设备开始响铃（协议 `call.ringing`）。**通话里的人都收到**，不含正在响铃的人自己——
   * 群通话里别人加了人，你也能给他摆「呼叫中」占位格，随后由 onUserAccept / onUserReject /
   * onUserNoResponse 收掉。1v1 主叫也会收到。2026-09-17 增。
   */
  virtual void onUserRinging(const std::string& uid) { (void)uid; }
  /** 群通话里某人接听了（其余人都收到）。 */
  virtual void onUserAccept(const std::string& uid) { (void)uid; }
  /** 群通话里某人拒接了。 */
  virtual void onUserReject(const std::string& uid) { (void)uid; }
  /** 群通话里某人超时未接。 */
  virtual void onUserNoResponse(const std::string& uid) { (void)uid; }
  /** 某人开关了麦克风。 */
  virtual void onUserAudioAvailable(const std::string& uid, bool available) {
    (void)uid;
    (void)available;
  }
  /** 某人开关了摄像头。 */
  virtual void onUserVideoAvailable(const std::string& uid, bool available) {
    (void)uid;
    (void)available;
  }

  // ---- 媒体与质量 ----
  /** 主讲人变化（服务端判定，节流 300ms）。客户端**不得**依赖更高频率。 */
  virtual void onActiveSpeakers(const std::vector<Speaker>& speakers) { (void)speakers; }
  /** 各方网络质量（服务端节流 2s）。 */
  virtual void onNetworkQuality(const std::vector<QualityEntry>& entries) { (void)entries; }

  // ---- 房间 ----
  /** 进房成功。 */
  virtual void onRoomJoined(const std::string& roomId) { (void)roomId; }
  /** 离房，或进房失败后的收场信号。 */
  virtual void onRoomLeft(const std::string& roomId) { (void)roomId; }
  /** 房间被解散。 */
  virtual void onRoomClosed(const std::string& roomId, const std::string& reason) {
    (void)roomId;
    (void)reason;
  }
};

}  // namespace imrtc
