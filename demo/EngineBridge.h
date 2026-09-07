#pragma once

/**
 * EngineBridge.h —— C ABI ↔ Qt 的**唯一**接缝。
 *
 * # 它做什么
 *
 * 持有 `imrtc::capi::Engine`（走 header-only 包装，而包装自己也走 C ABI——
 * 也就是说这个 Demo 与集成方拿到的是**同一条路**，见 CONVENTIONS §2），
 * 把 25 个 C 回调翻译成 Qt 信号，并用 QTimer 驱动 `tick()`。
 *
 * # 它不做什么
 *
 * **不做任何策略判断**。「群通话的红按钮该调 hangup 还是 leaveRoom」这种事
 * 属于界面层，不在这里。这一层越薄，集成方照抄的时候要理解的东西越少。
 *
 * # 线程
 *
 * C ABI 的规矩是「回调发生在 Engine 的线程上，切回 UI 线程是宿主的责任」。
 * 本 Demo 的做法是**让那个线程就是 GUI 线程**：Engine 自己不起线程，一切
 * 由 `tick()` 推动，而 `tick()` 由 GUI 线程上的 QTimer 调用；真实 WS 收帧
 * 发生在 IXWebSocket 的后台线程，但那些事件被排好队、在 `tick()` 里才放出来。
 *
 * 于是所有回调天然落在 GUI 线程上，**不需要** QueuedConnection。
 * 这不是碰巧——`assertOnGuiThread()` 每次回调都在核这一点，
 * 哪天有人把 tick 挪到别的线程上，会当场炸而不是随机崩。
 *
 * 宿主如果偏要在别的线程上 tick，那就得自己 `QMetaObject::invokeMethod`
 * 切回来。《接入指南》第一页写的就是这条。
 */

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include "imrtc/CallEngine.hpp"

class QNetworkAccessManager;
class QTimer;

/** 正在说话的人。对应 `imrtc_v1_speaker`。 */
struct SpeakerInfo {
  QString uid;
  int volume = 0;
};

/** 某个人的网络质量。level 0~6，0 = unknown。 */
struct QualityInfo {
  QString uid;
  int level = 0;
};

class EngineBridge : public QObject, private imrtc::capi::Observer {
  Q_OBJECT

public:
  explicit EngineBridge(QObject* parent = nullptr);
  ~EngineBridge() override;

  /**
   * 取 demo 票。**只在开发构建可用**（服务端 `-demo-login`）。
   * 生产要换成宿主自己的 `/v1/tokens`——README 与《接入指南》都写了怎么换。
   */
  void fetchDemoToken(const QString& httpBase, const QString& username);

  /** 建引擎 + 登录。会先销毁上一个引擎（destroy 阻塞到回调静默）。 */
  void connectToServer(const QString& wsUrl, const QString& token);

  /**
   * 会议房那条路要走 REST：先建房（或直接用别人给的房号），
   * 再 `POST /v1/rooms/{id}/tokens` 换一枚**绑定 (room_id, uid, device_id)、
   * TTL 5 分钟、一次性**的进房票，最后才能 `join_room`。
   * 通话那条路不用——`room_token` 由 `call.accept.ok` / `call.connected` 直接下发。
   */
  void createMeetingRoom(const QString& httpBase);
  void fetchRoomToken(const QString& httpBase, const QString& roomId);

  /** 登出并销毁引擎。 */
  void disconnectFromServer();

  bool isEngineAlive() const { return engine_ != nullptr; }

  /** 跨重启稳定的设备 id，存在 QSettings 里（协议要求「同一 uid 下唯一」）。 */
  static QString deviceId();

  /** 把 `http://host:port` 换成 `ws://host:port/v1/ws`。 */
  static QString wsUrlFromHttpBase(const QString& httpBase);

  /* ---- 转发给引擎的动作。返回错误码，0 = 成功。 ---- */
  qint32 startCall(const QStringList& calleeIds, const QString& mediaType, bool isGroup);
  qint32 accept();
  qint32 reject();
  qint32 cancel();
  qint32 hangup();
  qint32 inviteMore(const QStringList& calleeIds);
  qint32 joinRoom(const QString& roomId, const QString& roomToken);
  qint32 leaveRoom();
  /**
   * 渲染路径 A（设计 §8.3）：把某个 uid 的远端画面挂到宿主的原生窗口上。
   * macOS 传 `NSView*`、Windows 传 `HWND`；**传 nullptr 卸载**。
   *
   * **当前构建里它到不了任何地方**：引擎在没有媒体适配器时直接返回，
   * 连轨道都不去解析。留着这条线是为了把宿主侧的顺序（建窗口 → attach →
   * detach → 销毁窗口）先跑正，等 WebRTCAdapter 接上就自动生效。
   */
  qint32 attachView(const QString& uid, void* nativeHandle);

  /**
   * 本端摄像头预览（1v1 那一屏右下角的小窗）。**不是** `attachView(自己的 uid, …)`——
   * 引擎不知道自己的 uid，本端画面也没有远端轨道 id。见 imrtc_c.h。
   */
  qint32 attachLocalView(void* nativeHandle);

  /**
   * 报某人画面的层上界（协议 §3.5）。九宫格报 `"l"`、1v1 铺满报 `"h"`。
   * **纯信令，不需要媒体实现**——这一条现在就真的发到服务端上。
   */
  qint32 setRemoteLayer(const QString& uid, const QString& layer);

  qint32 openMic();
  qint32 closeMic();
  qint32 openCamera();
  qint32 closeCamera();

  // 不是 const：包装层的 callState() 会记下 lastError，本来就是非 const 的。
  imrtc_v1_call_state callState();
  imrtc_v1_room_state roomState();
  static QString versionString();

signals:
  void tokenReady(const QString& token);
  void tokenFailed(const QString& message);
  void meetingRoomCreated(const QString& roomId);
  void roomTokenReady(const QString& roomId, const QString& roomToken);
  void restFailed(const QString& message);

  void connected(const QString& sessionId, bool resumed);
  void disconnected();
  void kickedOut();
  void engineError(qint32 code, const QString& name, const QString& forType);

  void callReceived(const QString& callId, const QString& caller, const QStringList& calleeIds,
                    const QString& mediaType, bool isGroup);
  void callBegan(const QString& callId, const QString& roomId, const QString& role);
  void callEnded(const QString& callId, const QString& reason, qint64 durationSec,
                 const QString& endedBy);
  void callMissed(const QString& callId, const QString& caller, const QString& reason);
  void callCancelled(const QString& by);
  void callRejected(const QString& uid);
  void callBusy(const QString& uid);
  void callNoAnswer(const QString& uid);
  void handledOnOtherDevice(const QString& callId, const QString& action);

  void userEntered(const QString& uid);
  void userLeft(const QString& uid);
  void userAccepted(const QString& uid);
  void userRejected(const QString& uid);
  void userNoResponse(const QString& uid);
  void userAudioAvailable(const QString& uid, bool available);
  void userVideoAvailable(const QString& uid, bool available);

  void activeSpeakers(const QList<SpeakerInfo>& speakers);
  void networkQuality(const QList<QualityInfo>& entries);

  void roomJoined(const QString& roomId);
  void roomLeft(const QString& roomId);
  void roomClosed(const QString& roomId, const QString& reason);

private:
  /* ---- imrtc::capi::Observer。全部只做「翻译成信号」这一件事。 ---- */
  void onConnected(const std::string& sessionId, bool resumed) override;
  void onDisconnected() override;
  void onKickedOut() override;
  void onError(std::int32_t code, const std::string& name, const std::string& forType) override;
  void onCallReceived(const std::string& callId, const std::string& caller,
                      const std::vector<std::string>& calleeIds, const std::string& mediaType,
                      bool isGroup) override;
  void onCallBegin(const std::string& callId, const std::string& roomId,
                   const std::string& role) override;
  void onCallEnd(const std::string& callId, const std::string& reason, std::int64_t durationSec,
                 const std::string& endedBy) override;
  void onCallMissed(const std::string& callId, const std::string& caller,
                    const std::string& reason) override;
  void onCallCancelled(const std::string& by) override;
  void onCallRejected(const std::string& uid) override;
  void onCallBusy(const std::string& uid) override;
  void onCallNoAnswer(const std::string& uid) override;
  void onHandledOnOtherDevice(const std::string& callId, const std::string& action) override;
  void onUserEnter(const std::string& uid) override;
  void onUserLeave(const std::string& uid) override;
  void onUserAccept(const std::string& uid) override;
  void onUserReject(const std::string& uid) override;
  void onUserNoResponse(const std::string& uid) override;
  void onUserAudioAvailable(const std::string& uid, bool available) override;
  void onUserVideoAvailable(const std::string& uid, bool available) override;
  void onActiveSpeakers(const std::vector<imrtc::capi::Speaker>& speakers) override;
  void onNetworkQuality(const std::vector<imrtc::capi::Quality>& entries) override;
  void onRoomJoined(const std::string& roomId) override;
  void onRoomLeft(const std::string& roomId) override;
  void onRoomClosed(const std::string& roomId, const std::string& reason) override;

  /** 每次回调都核一遍：这一刀的前提是「回调落在 GUI 线程上」。 */
  void assertOnGuiThread(const char* where) const;

  std::unique_ptr<imrtc::capi::Engine> engine_;
  /** 登录票。REST 那两个接口要拿它做 Bearer。**只在内存里，不落盘。** */
  QString token_;
  QTimer* ticker_ = nullptr;
  QNetworkAccessManager* http_ = nullptr;
};
