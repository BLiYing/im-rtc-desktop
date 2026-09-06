#include "VideoTile.h"

#include <QPainter>
#include <QPainterPath>

#include "Avatar.h"
#include "Icons.h"
#include "Theme.h"

namespace {

constexpr qreal kSpeakingBorder = 2.5;  ///< §04：2.5 内描边，**不撑大格子**
constexpr int kInset = 8;               ///< 标签与角标离边 8

}  // namespace

VideoTile::VideoTile(QWidget* parent) : QWidget(parent) {
  setMinimumSize(120, 90);
}

void VideoTile::setIdentity(const QString& uid, const QString& displayName) {
  uid_ = uid;
  displayName_ = displayName;
  update();
}

void VideoTile::setState(State state) {
  if (state_ == state) return;
  state_ = state;
  update();
}

void VideoTile::setSpeaking(bool speaking) {
  if (speaking_ == speaking) return;
  speaking_ = speaking;
  update();
}

void VideoTile::setMuted(bool muted) {
  if (muted_ == muted) return;
  muted_ = muted;
  update();
}

void VideoTile::setQualityLevel(int level) {
  if (quality_ == level) return;
  quality_ = level;
  update();
}

void VideoTile::setSelf(bool self) {
  self_ = self;
  update();
}

void VideoTile::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  QPainterPath clip;
  clip.addRoundedRect(box, theme::metric::kTileRadius, theme::metric::kTileRadius);

  painter.setPen(Qt::NoPen);
  painter.setBrush(theme::call::tile());
  painter.drawPath(clip);

  // ---- 没有画面时露头像。等媒体那一刀接上，画面会盖在这一层之上。 ----
  const int avatarSize = qMin(qMin(width(), height()) / 2, theme::metric::kAvatarLarge);
  const QRect avatarBox(rect().center().x() - avatarSize / 2,
                        rect().center().y() - avatarSize / 2 - 6, avatarSize, avatarSize);
  painter.save();
  if (state_ == State::Ringing || state_ == State::Left) painter.setOpacity(0.45);
  paintAvatar(&painter, avatarBox, uid_, displayName_, qMax(11, avatarSize * 32 / 96));
  painter.restore();


  // ---- 左下名字标签：高 18、圆角 6、内边距 2/8 ----
  const QString label = self_ ? tr("我") : (displayName_.isEmpty() ? uid_ : displayName_);
  painter.setFont(theme::type::c1());
  const int labelWidth = painter.fontMetrics().horizontalAdvance(label) + 16;
  const QRect labelBox(kInset, height() - kInset - theme::metric::kTileLabelHeight, labelWidth,
                       theme::metric::kTileLabelHeight);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0, 0, 0, 140));
  painter.drawRoundedRect(labelBox, theme::metric::kTileLabelRadius,
                          theme::metric::kTileLabelRadius);
  painter.setPen(theme::call::fg());
  painter.drawText(labelBox, Qt::AlignCenter, label);

  // 状态说明与名字标签同一条带、右对齐——格子很矮，另起一行必然和标签打架。
  if (state_ != State::Present) {
    const QString note = state_ == State::Ringing ? tr("呼叫中…") : tr("已离开");
    painter.setPen(theme::call::fgDim());
    painter.drawText(QRect(labelBox.right() + 6, labelBox.top(),
                           width() - labelBox.right() - 6 - kInset, labelBox.height()),
                     Qt::AlignRight | Qt::AlignVCenter, note);
  }

  // ---- 右上静音角标：24 圆、黑 55% 底 ----
  if (muted_) {
    const QRect badge(width() - kInset - theme::metric::kMuteBadge, kInset,
                      theme::metric::kMuteBadge, theme::metric::kMuteBadge);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawEllipse(badge);
    const int glyph = 14;
    painter.drawPixmap(badge.center().x() - glyph / 2 + 1, badge.center().y() - glyph / 2 + 1,
                       glyph, glyph,
                       icons::pixmap(icons::Name::MicSlash, glyph, theme::call::fg(),
                                     devicePixelRatioF()));
  }

  // ---- 弱网：右下 net-bars，用 warn 色 ----
  if (quality_ > 0 && quality_ <= 2) {
    const int glyph = 16;
    painter.drawPixmap(width() - kInset - glyph, height() - kInset - glyph, glyph, glyph,
                       icons::pixmap(icons::Name::NetBars, glyph, theme::call::warn(),
                                     devicePixelRatioF()));
  }

  // ---- 正在说话：2.5 内描边，画在最后免得被别的盖住 ----
  if (speaking_) {
    QPen pen(theme::call::accept());
    pen.setWidthF(kSpeakingBorder);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const qreal offset = kSpeakingBorder / 2.0;
    painter.drawRoundedRect(box.adjusted(offset, offset, -offset, -offset),
                            theme::metric::kTileRadius - offset,
                            theme::metric::kTileRadius - offset);
  }
}
