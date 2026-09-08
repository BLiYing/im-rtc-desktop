#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/Handshake.h"

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
};

/** CallBegin 对应 `onCallBegin`：双方在这一刻同时开始计时。 */
struct CallBegin {
  std::string callId;
  std::string roomId;
  std::string mediaType;
  bool isGroup = false;
  /** "caller" / "callee"。 */
  std::string role;
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
  /** 信令通道断开。Engine 会按退避档自动重连，除非关闭码明说不该重连。 */
  virtual void onDisconnected() {}
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
  /** 通话中有人打进来、已被自动回忙线。界面据此提示一句谁来过电话。 */
  virtual void onCallMissed(const CallMissed& missed) { (void)missed; }
  /** 便利事件：主叫取消。**随后必有一条 onCallEnd**（不变量 I2）。 */
  virtual void onCallCancelled(const std::string& by) { (void)by; }
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
