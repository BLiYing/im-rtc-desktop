#include "CallOverlay.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QStackedWidget>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

#include "Avatar.h"
#include "CallStrings.h"
#include "ControlButton.h"
#include "SoloVideo.h"
#include "Theme.h"
#include "VideoTile.h"

namespace {

/** 结束态停留几秒让人看清原因，再自动关。 */
constexpr int kEndedLingerMs = 2500;
}  // namespace

CallOverlay::CallOverlay(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(false);

  // ---- 顶栏 ----
  title_ = new QLabel(this);
  title_->setFont(theme::type::t3());
  timer_ = new QLabel(this);
  timer_->setFont(theme::type::m1());  // 等宽数字，否则每秒整行左右抖

  auto* top = new QHBoxLayout;
  top->setContentsMargins(16, 12, 16, 0);
  top->addWidget(title_);
  top->addStretch();
  top->addWidget(timer_);

  // ---- 1v1 主体 ----
  avatar_ = new AvatarWidget(theme::metric::kAvatarLarge, this);
  peerName_ = new QLabel(this);
  peerName_->setFont(theme::type::t1());
  peerName_->setAlignment(Qt::AlignCenter);
  status_ = new QLabel(this);
  status_->setFont(theme::type::b2());
  status_->setAlignment(Qt::AlignCenter);

  /*
    1v1 主体是**两页**，不是一页里藏几个控件：

      第 0 页 —— 头像 + 名字 + 状态（语音通话页、接通前、结束前都用它）
      第 1 页 —— 画面（远端铺满 + 本端小窗）

    一开始是一页里 `hide()` 掉头像那几个再塞画面，结果画面只分到 520×16——
    两个 `addStretch()` 把空间吃光了，**隐藏控件并不腾地方**。堆叠页没这个问题。
  */
  auto* avatarPage = new QWidget(this);
  auto* solo = new QVBoxLayout(avatarPage);
  solo->setContentsMargins(0, 0, 0, 0);
  solo->addStretch();
  solo->addWidget(avatar_, 0, Qt::AlignHCenter);
  solo->addSpacing(12);
  solo->addWidget(peerName_);
  solo->addWidget(status_);
  solo->addStretch();

  soloPane_ = new QStackedWidget(this);
  static_cast<QStackedWidget*>(soloPane_)->addWidget(avatarPage);

  // ---- 九宫格主体 ----
  gridPane_ = new QWidget(this);
  grid_ = new QGridLayout(gridPane_);
  grid_->setContentsMargins(16, 8, 16, 8);
  grid_->setSpacing(theme::metric::kTileGap);

  // ---- 结束态 ----
  endText_ = new QLabel(this);
  endText_->setFont(theme::type::t2());
  endText_->setAlignment(Qt::AlignCenter);
  endPane_ = new QWidget(this);
  auto* ended = new QVBoxLayout(endPane_);
  ended->addStretch();
  ended->addWidget(endText_);
  ended->addStretch();

  // ---- 控制条 ----
  mic_ = new ControlButton(ControlButton::Kind::Toggle, this);
  mic_->setSymbols(icons::Name::Mic, icons::Name::MicSlash);
  camera_ = new ControlButton(ControlButton::Kind::Toggle, this);
  camera_->setSymbols(icons::Name::Video, icons::Name::VideoSlash);
  screen_ = new ControlButton(ControlButton::Kind::Toggle, this);
  screen_->setSymbols(icons::Name::ScreenShare, icons::Name::ScreenShare);
  danger_ = new ControlButton(ControlButton::Kind::Danger, this);
  danger_->setSymbols(icons::Name::PhoneDown, icons::Name::PhoneDown);
  answer_ = new ControlButton(ControlButton::Kind::Accept, this);
  answer_->setSymbols(icons::Name::Phone, icons::Name::Phone);

  controls_ = new QHBoxLayout;
  controls_->setContentsMargins(16, 8, 16, 16);
  controls_->setSpacing(theme::metric::kControlGap);
  controls_->addStretch();
  for (ControlButton* button : {mic_, camera_, screen_, danger_, answer_}) {
    controls_->addWidget(button, 0, Qt::AlignTop);
  }
  controls_->addStretch();

  modeNotice_ = new QLabel(this);
  modeNotice_->setFont(theme::type::c2());
  modeNotice_->setAlignment(Qt::AlignCenter);
  modeNotice_->setWordWrap(true);
  {
    QPalette palette = modeNotice_->palette();
    palette.setColor(QPalette::WindowText, theme::call::warn());
    modeNotice_->setPalette(palette);
  }

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addLayout(top);
  layout->addWidget(soloPane_, 1);
  layout->addWidget(gridPane_, 1);
  layout->addWidget(endPane_, 1);
  layout->addWidget(modeNotice_);
  layout->addLayout(controls_);

  setFixedSize(theme::metric::kOverlayWidth, theme::metric::kOverlayHeight);

  // 媒体没接上时这两个按钮是空操作，按 §06 的禁用态处理（点了要出提示）。
  const QString blocked = tr("纯信令模式：媒体还没接入，这个按钮现在不会有任何效果。");
  mic_->setBlocked(blocked);
  camera_->setBlocked(blocked);
  screen_->setBlocked(tr("共享屏幕是后续期的事，当前构建没有。"));
  for (ControlButton* button : {mic_, camera_, screen_}) {
    connect(button, &ControlButton::blockedClicked, this, &CallOverlay::notice);
  }

  danger_->setObjectName(QStringLiteral("dangerButton"));
  answer_->setObjectName(QStringLiteral("answerButton"));
  camera_->setObjectName(QStringLiteral("cameraButton"));

  connect(answer_, &ControlButton::clicked, this, &CallOverlay::acceptRequested);
  connect(danger_, &ControlButton::clicked, this, [this] {
    // 规则本身在 dangerAction() 里，这里只负责把它翻成信号。
    switch (dangerAction()) {
      case DangerAction::Reject:    emit rejectRequested(); break;
      case DangerAction::Cancel:    emit cancelRequested(); break;
      case DangerAction::Hangup:    emit hangupRequested(); break;
      case DangerAction::LeaveRoom: emit leaveRoomRequested(); break;
      case DangerAction::None:      break;
    }
  });

  clock_ = new QTimer(this);
  clock_->setInterval(1000);
  connect(clock_, &QTimer::timeout, this, [this] {
    ++elapsedSec_;
    timer_->setText(callstrings::duration(elapsedSec_));
  });

  autoClose_ = new QTimer(this);
  autoClose_->setSingleShot(true);
  autoClose_->setInterval(kEndedLingerMs);
  connect(autoClose_, &QTimer::timeout, this, [this] { emit closed(); });

  retranslateUi();
  applyPhase();
}

CallOverlay::DangerAction CallOverlay::dangerAction() const {
  switch (phase_) {
    case Phase::Incoming:
      return DangerAction::Reject;
    case Phase::Outgoing:
      // 接通前是「取消」，不是「挂断」——协议上是两条不同的帧。
      return DangerAction::Cancel;
    case Phase::Connected:
      // **只判会议房，不判人数。**群通话是有 call 的，必须走 hangup——
      // 发 room.leave 的话离开者永远收不到 onCallEnd（见头注释第 2 条）。
      return isRoomMode() ? DangerAction::LeaveRoom : DangerAction::Hangup;
    case Phase::Ended:
      break;
  }
  return DangerAction::None;
}

QString CallOverlay::dangerCaption() const {
  switch (phase_) {
    case Phase::Incoming: return tr("拒绝");
    case Phase::Outgoing: return tr("取消");
    default: break;
  }
  // 文案按人数分叉，与 dangerAction() 的依据不同——群通话写「离开」但调 hangup。
  return (isGroup_ || isRoomMode()) ? tr("离开") : tr("挂断");
}

void CallOverlay::beginOutgoing(const QStringList& members, const QString& mediaType,
                                bool isGroup) {
  outgoing_ = true;
  isGroup_ = isGroup;
  isVideo_ = mediaType == QLatin1String("video");
  members_ = members;
  peer_ = members.isEmpty() ? QString() : members.first();
  roomId_.clear();
  phase_ = Phase::Outgoing;
  elapsedSec_ = 0;
  rebuildTiles();
  applyPhase();
}

void CallOverlay::beginIncoming(const QString& caller, const QStringList& callees,
                                const QString& mediaType, bool isGroup) {
  outgoing_ = false;
  isGroup_ = isGroup;
  isVideo_ = mediaType == QLatin1String("video");
  caller_ = caller;
  peer_ = caller;
  members_ = callees;
  if (!members_.contains(caller)) members_.prepend(caller);
  roomId_.clear();
  phase_ = Phase::Incoming;
  elapsedSec_ = 0;
  rebuildTiles();
  applyPhase();
}

void CallOverlay::beginRoom(const QString& roomId) {
  outgoing_ = true;
  isGroup_ = true;
  isVideo_ = false;
  roomId_ = roomId;
  members_.clear();
  phase_ = Phase::Connected;  // 会议房没有振铃阶段
  elapsedSec_ = 0;
  rebuildTiles();
  applyPhase();
  clock_->start();
}

void CallOverlay::markConnected(const QString& role) {
  Q_UNUSED(role);
  phase_ = Phase::Connected;
  elapsedSec_ = 0;
  clock_->start();
  applyPhase();
  syncSoloVideo();
}

void CallOverlay::markEnded(const QString& reason, qint64 durationSec) {
  // 画面层要在切到结束态**之前**拆掉：那时 peer_ 还在，detach 才发得对人。
  phase_ = Phase::Ended;
  syncSoloVideo();
  endReason_ = reason;
  endDuration_ = durationSec;
  clock_->stop();
  applyPhase();
  autoClose_->start();
}

void CallOverlay::updateTitle() {
  if (phase_ == Phase::Incoming) {
    // 来电页标题栏留空（UX_FLOWS §07 v3.7）：谁、什么通话，下面那两行已经说了。
    title_->clear();
  } else if (isRoomMode()) {
    title_->setText(tr("会议房间 %1").arg(roomId_));
  } else if (isGroup_) {
    title_->setText(tr("群通话 · %1 人").arg(tiles_.size()));
  } else {
    title_->setText(isVideo_ ? tr("视频通话") : tr("语音通话"));
  }
}

void CallOverlay::applyPhase() {
  const bool group = isGroup_ || isRoomMode();
  soloPane_->setVisible(phase_ != Phase::Ended && !group);
  gridPane_->setVisible(phase_ != Phase::Ended && group);
  endPane_->setVisible(phase_ == Phase::Ended);
  timer_->setVisible(phase_ == Phase::Connected);

  avatar_->setIdentity(peer_, peer_);
  peerName_->setText(peer_);
  updateTitle();

  answer_->setVisible(phase_ == Phase::Incoming);
  danger_->setVisible(phase_ != Phase::Ended);
  mic_->setVisible(phase_ == Phase::Connected);
  // 视频来电也有摄像头这一颗（v3.7：接听前可先关掉），今天与接通后那颗一样是禁用态。
  camera_->setVisible((phase_ == Phase::Connected || phase_ == Phase::Incoming) && isVideo_);
  // 共享屏幕只在桌面出现，且只在 1v1/群通话接通后（草图 §06-R）。
  screen_->setVisible(phase_ == Phase::Connected && !isRoomMode());
  modeNotice_->setVisible(phase_ == Phase::Connected);

  switch (phase_) {
    case Phase::Outgoing:
      status_->setVisible(true);
      status_->setText(tr("正在呼叫…"));
      break;
    case Phase::Incoming:
      status_->setVisible(true);
      status_->setText(callstrings::incomingInviteText(isVideo_, isGroup_));
      break;
    case Phase::Connected:
      status_->setVisible(false);
      timer_->setText(callstrings::duration(elapsedSec_));
      break;
    case Phase::Ended: {
      const QString reasonText = callstrings::callerEndText(endReason_);
      endText_->setText(endDuration_ > 0
                            ? tr("%1 · %2").arg(reasonText, callstrings::duration(endDuration_))
                            : reasonText);
      break;
    }
  }
  retranslateUi();
}

void CallOverlay::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  // 语音页是径向渐变底；有画面的视频页会被画面盖住（§02）。
  QLinearGradient gradient(rect().topLeft(), rect().bottomRight());
  gradient.setColorAt(0.0, theme::call::callBgTop());
  gradient.setColorAt(1.0, theme::call::callBgBottom());

  painter.setPen(Qt::NoPen);
  painter.setBrush(gradient);
  painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 14, 14);
}

void CallOverlay::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
    updateTitle();
  }
  QWidget::changeEvent(event);
}

void CallOverlay::retranslateUi() {
  // 文案跟着态走（§06）：常态说「点了会发生什么」，开启态说「现在是什么状态」。
  mic_->setCaptions(tr("静音"), tr("已静音"));
  camera_->setCaptions(tr("摄像头"), tr("已关闭"));
  screen_->setCaptions(tr("共享屏幕"), tr("共享中"));
  answer_->setCaptions(tr("接听"), tr("接听"));

  const QString caption = dangerCaption();
  danger_->setCaptions(caption, caption);
  danger_->setSymbols(phase_ == Phase::Incoming ? icons::Name::Xmark : icons::Name::PhoneDown,
                      phase_ == Phase::Incoming ? icons::Name::Xmark : icons::Name::PhoneDown);

  modeNotice_->setText(tr("纯信令模式 · 没有声音和画面"));

  // 通话页固定深色，所以这里的前景色不跟系统走。
  for (QLabel* label : {title_, timer_, peerName_, endText_}) {
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, theme::call::fg());
    label->setPalette(palette);
  }
  QPalette dim = status_->palette();
  dim.setColor(QPalette::WindowText, theme::call::fgDim());
  status_->setPalette(dim);
}
