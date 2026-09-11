#include "IncomingBanner.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>

#include "Avatar.h"
#include "CallStrings.h"
#include "ControlButton.h"
#include "Theme.h"

namespace {

/** 内边距与间隔，与 Web `styles.toast` 同值：左 12、右 10、头像到字 10、按钮之间 8。 */
constexpr int kPadLeft = 12;
constexpr int kPadRight = 10;
constexpr int kTextGap = 10;
constexpr int kButtonGap = 8;
/** 头像首字母字号（Web 横幅头像 13px）。 */
constexpr int kAvatarPointSize = 13;
constexpr int kShadowBlur = 34;
constexpr int kShadowOffsetY = 14;

}  // namespace

IncomingBanner::IncomingBanner(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("incomingBanner"));
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setFixedHeight(theme::metric::kBannerHeight);

  camera_ = new ControlButton(ControlButton::Kind::Toggle, this);
  camera_->setSymbols(icons::Name::Video, icons::Name::VideoSlash);
  reject_ = new ControlButton(ControlButton::Kind::Danger, this);
  reject_->setSymbols(icons::Name::Xmark, icons::Name::Xmark);
  // 接听恒为 phone（v3.7）：与点开后的来电浮层那颗长得一样。
  accept_ = new ControlButton(ControlButton::Kind::Accept, this);
  accept_->setSymbols(icons::Name::Phone, icons::Name::Phone);

  camera_->setObjectName(QStringLiteral("bannerCameraButton"));
  reject_->setObjectName(QStringLiteral("bannerRejectButton"));
  accept_->setObjectName(QStringLiteral("bannerAcceptButton"));

  // 左边那一段（头像 + 两行字）是 paintEvent 画的，布局里只放一根弹簧把按钮推到右边。
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(kPadLeft, 0, kPadRight, 0);
  row->setSpacing(kButtonGap);
  row->addStretch(1);
  for (ControlButton* button : {camera_, reject_, accept_}) {
    button->setCompact(true);
    button->setFocusPolicy(Qt::NoFocus);
    row->addWidget(button, 0, Qt::AlignVCenter);
  }

  connect(camera_, &ControlButton::blockedClicked, this, &IncomingBanner::notice);
  connect(reject_, &ControlButton::clicked, this, &IncomingBanner::rejectRequested);
  connect(accept_, &ControlButton::clicked, this, &IncomingBanner::acceptRequested);

  // 深色卡片压在跟随系统的浅色主界面上，没有阴影会像贴上去的一块色块。
  auto* shadow = new QGraphicsDropShadowEffect(this);
  shadow->setBlurRadius(kShadowBlur);
  shadow->setOffset(0, kShadowOffsetY);
  shadow->setColor(theme::call::bannerShadow());
  setGraphicsEffect(shadow);

  if (parent != nullptr) parent->installEventFilter(this);
  retranslateUi();
}

void IncomingBanner::showCall(const QString& caller, const QString& mediaType, bool isGroup) {
  caller_ = caller;
  isVideo_ = mediaType == QLatin1String("video");
  isGroup_ = isGroup;
  // 视频来电（含群视频）才有摄像头这一颗；语音来电藏起来。
  camera_->setVisible(isVideo_);
  place();
  show();
  raise();
  update();
}

void IncomingBanner::place() {
  const QWidget* host = parentWidget();
  if (host == nullptr) return;
  const int inset = theme::metric::kBannerInset;
  const int width = qMax(0, qMin(theme::metric::kBannerMaxWidth, host->width() - 2 * inset));
  setGeometry((host->width() - width) / 2, inset, width, theme::metric::kBannerHeight);
}

bool IncomingBanner::eventFilter(QObject* watched, QEvent* event) {
  if (watched == parentWidget() && event->type() == QEvent::Resize) place();
  return QWidget::eventFilter(watched, event);
}

int IncomingBanner::textRight() const {
  for (const ControlButton* button : {camera_, reject_, accept_}) {
    if (button->isVisibleTo(this)) return button->geometry().left() - kTextGap;
  }
  return width() - kPadRight;
}

void IncomingBanner::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(theme::call::banner());
  const qreal radius = theme::metric::kBannerRadius;
  painter.drawRoundedRect(QRectF(rect()), radius, radius);

  const int avatar = theme::metric::kBannerAvatar;
  const QRect avatarRect(kPadLeft, (height() - avatar) / 2, avatar, avatar);
  paintAvatar(&painter, avatarRect, caller_, caller_, kAvatarPointSize);

  // 放不下省略号收尾，**不许压到按钮底下**，也不许硬切半个字。
  const int left = avatarRect.right() + 1 + kTextGap;
  const int textWidth = qMax(0, textRight() - left);
  const QFont nameFont = theme::type::t3();
  const QFont inviteFont = theme::type::b1();
  const QFontMetrics nameMetrics(nameFont);
  const QFontMetrics inviteMetrics(inviteFont);
  const int top = (height() - nameMetrics.height() - inviteMetrics.height()) / 2;

  painter.setFont(nameFont);
  painter.setPen(theme::call::fg());
  painter.drawText(QRect(left, top, textWidth, nameMetrics.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   nameMetrics.elidedText(caller_, Qt::ElideRight, textWidth));

  painter.setFont(inviteFont);
  painter.setPen(theme::call::fgDim());
  const QString invite = callstrings::incomingInviteText(isVideo_, isGroup_);
  painter.drawText(QRect(left, top + nameMetrics.height(), textWidth, inviteMetrics.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   inviteMetrics.elidedText(invite, Qt::ElideRight, textWidth));
}

void IncomingBanner::mousePressEvent(QMouseEvent* event) {
  // 接住按下，释放才会回到本体。按钮上的按下被按钮自己 accept 了，到不了这里。
  if (event->button() == Qt::LeftButton) {
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void IncomingBanner::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
    emit expandRequested();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void IncomingBanner::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
    update();
  }
  QWidget::changeEvent(event);
}

void IncomingBanner::retranslateUi() {
  camera_->setCaptions(tr("摄像头"), tr("已关闭"));
  reject_->setCaptions(tr("拒绝"), tr("拒绝"));
  accept_->setCaptions(tr("接听"), tr("接听"));
  camera_->setBlocked(tr("纯信令模式：媒体还没接入，这个按钮现在不会有任何效果。"));
}
