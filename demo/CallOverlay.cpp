#include "CallOverlay.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

#include "Avatar.h"
#include "CallStrings.h"
#include "ControlButton.h"
#include "Theme.h"
#include "VideoTile.h"

namespace {

/** 结束态停留几秒让人看清原因，再自动关。 */
constexpr int kEndedLingerMs = 2500;
/** 九宫格：3×3（桌面与手机同值，草图 §05 / §06-S）。 */
constexpr int kGridColumns = 3;

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

  soloPane_ = new QWidget(this);
  auto* solo = new QVBoxLayout(soloPane_);
  solo->addStretch();
  solo->addWidget(avatar_, 0, Qt::AlignHCenter);
  solo->addSpacing(12);
  solo->addWidget(peerName_);
  solo->addWidget(status_);
  solo->addStretch();

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

  connect(answer_, &ControlButton::clicked, this, &CallOverlay::acceptRequested);
  connect(danger_, &ControlButton::clicked, this, [this] {
    switch (phase_) {
      case Phase::Incoming:
        emit rejectRequested();
        break;
      case Phase::Outgoing:
        // 接通前是「取消」，不是「挂断」——协议上是两条不同的帧。
        emit cancelRequested();
        break;
      case Phase::Connected:
        // **只判会议房，不判人数。**群通话是有 call 的，必须走 hangup——
        // 发 room.leave 的话离开者永远收不到 onCallEnd（见头注释第 2 条）。
        if (isRoomMode()) {
          emit leaveRoomRequested();
        } else {
          emit hangupRequested();
        }
        break;
      case Phase::Ended:
        break;
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
}

void CallOverlay::markEnded(const QString& reason, qint64 durationSec) {
  phase_ = Phase::Ended;
  endReason_ = reason;
  endDuration_ = durationSec;
  clock_->stop();
  applyPhase();
  autoClose_->start();
}

/* ---- 成员事件 ---- */

VideoTile* CallOverlay::tileFor(const QString& uid) {
  auto it = tiles_.find(uid);
  return it == tiles_.end() ? nullptr : it.value();
}

void CallOverlay::onMemberEntered(const QString& uid) {
  if (!members_.contains(uid)) {
    members_ << uid;
    rebuildTiles();
  }
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Present);
}

void CallOverlay::onMemberLeft(const QString& uid) {
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Left);
}

void CallOverlay::onMemberAccepted(const QString& uid) {
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Present);
}

void CallOverlay::onMemberRejected(const QString& uid) {
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Left);
}

void CallOverlay::onMemberNoResponse(const QString& uid) {
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Left);
}

void CallOverlay::onMemberAudio(const QString& uid, bool available) {
  if (VideoTile* tile = tileFor(uid)) tile->setMuted(!available);
}

void CallOverlay::onSpeakers(const QList<SpeakerInfo>& speakers) {
  QStringList talking;
  for (const SpeakerInfo& speaker : speakers) talking << speaker.uid;
  for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
    it.value()->setSpeaking(talking.contains(it.key()));
  }
}

void CallOverlay::onQuality(const QList<QualityInfo>& entries) {
  for (const QualityInfo& entry : entries) {
    if (VideoTile* tile = tileFor(entry.uid)) tile->setQualityLevel(entry.level);
  }
}

void CallOverlay::onReconnecting(bool reconnecting) {
  if (phase_ != Phase::Connected) return;
  status_->setVisible(reconnecting);
  status_->setText(reconnecting ? tr("正在重连…") : QString());
}

/* ---- 布局与外观 ---- */

void CallOverlay::rebuildTiles() {
  qDeleteAll(tiles_);
  tiles_.clear();
  while (QLayoutItem* item = grid_->takeAt(0)) delete item;

  if (!isGroup_ && !isRoomMode()) return;

  QStringList everyone = members_;
  if (!selfUid_.isEmpty() && !everyone.contains(selfUid_)) everyone.prepend(selfUid_);

  int index = 0;
  for (const QString& uid : everyone) {
    auto* tile = new VideoTile(gridPane_);
    tile->setIdentity(uid, uid);
    tile->setSelf(uid == selfUid_);
    // 拨出中时除自己外都还在振铃——这一格的「呼叫中…」是真的，由回调翻转。
    tile->setState(uid == selfUid_ || phase_ == Phase::Connected ? VideoTile::State::Present
                                                                 : VideoTile::State::Ringing);
    grid_->addWidget(tile, index / kGridColumns, index % kGridColumns);
    tiles_.insert(uid, tile);
    ++index;
  }
}

void CallOverlay::updateTitle() {
  if (isRoomMode()) {
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
  camera_->setVisible(phase_ == Phase::Connected && isVideo_);
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
      status_->setText(isVideo_ ? tr("邀请你视频通话") : tr("邀请你语音通话"));
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

  QString dangerCaption;
  switch (phase_) {
    case Phase::Incoming: dangerCaption = tr("拒绝"); break;
    case Phase::Outgoing: dangerCaption = tr("取消"); break;
    // **文案**按人数分叉：群通话与会议房都写「离开」，1v1 写「挂断」。
    // 注意这与上面调哪个方法是两件事——群通话写着「离开」但调的是 hangup()。
    default: dangerCaption = (isGroup_ || isRoomMode()) ? tr("离开") : tr("挂断"); break;
  }
  danger_->setCaptions(dangerCaption, dangerCaption);
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
