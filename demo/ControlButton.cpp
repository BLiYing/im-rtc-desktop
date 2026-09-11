#include "ControlButton.h"

#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

#include "Theme.h"

namespace {

/** 按下反馈：缩放 0.92，120ms（§06 表里的「按下反馈」列）。 */
constexpr qreal kPressedScale = 0.92;
constexpr int kPressDurationMs = 120;
/** 禁用态：底与图标都降到 35% 不透明。 */
constexpr int kBlockedAlpha = 89;  // 35% → 89/255

}  // namespace

ControlButton::ControlButton(Kind kind, QWidget* parent)
    : QAbstractButton(parent), kind_(kind) {
  setCursor(Qt::PointingHandCursor);
  setAttribute(Qt::WA_Hover, true);

  pressAnimation_ = new QVariantAnimation(this);
  pressAnimation_->setDuration(kPressDurationMs);
  connect(pressAnimation_, &QVariantAnimation::valueChanged, this,
          [this](const QVariant& value) {
            scale_ = value.toReal();
            update();
          });
}

void ControlButton::setSymbols(icons::Name idle, icons::Name on) {
  idleSymbol_ = idle;
  onSymbol_ = on;
  update();
}

void ControlButton::setCaptions(const QString& idle, const QString& on) {
  idleCaption_ = idle;
  onCaption_ = on;
  syncCompactLabels();
  updateGeometry();
  update();
}

void ControlButton::setCompact(bool compact) {
  compact_ = compact;
  syncCompactLabels();
  updateGeometry();
  update();
}

void ControlButton::syncCompactLabels() {
  if (!compact_) return;
  setToolTip(idleCaption_);
  setAccessibleName(idleCaption_);
}

void ControlButton::setOn(bool on) {
  if (on_ == on) return;
  on_ = on;
  // 无障碍标签用状态式（§06）：「麦克风，已关闭，按钮」。
  setAccessibleName(on_ ? onCaption_ : idleCaption_);
  update();
}

void ControlButton::setBlocked(const QString& reason) {
  blockedReason_ = reason;
  setCursor(reason.isEmpty() ? Qt::PointingHandCursor : Qt::ArrowCursor);
  update();
}

int ControlButton::diameter() const {
  if (compact_) return theme::metric::kBannerButton;
  // 只有终止类动作放大到 64（§04）。
  return kind_ == Kind::Toggle ? theme::metric::kControl : theme::metric::kControlLarge;
}

int ControlButton::iconSize() const {
  if (compact_) return theme::metric::kBannerIcon;
  return kind_ == Kind::Toggle ? theme::metric::kIcon : theme::metric::kIconLarge;
}

QSize ControlButton::sizeHint() const {
  if (compact_) return QSize(diameter(), diameter());
  const bool hasCaption = !idleCaption_.isEmpty();
  // **两份文案都要量**：只量开启态的话，「共享屏幕」这种常态更长的按钮会被切掉
  // （英文更明显：Share screen vs Sharing）。
  const QFontMetrics metrics(theme::type::c2());
  const int captionWidth =
      hasCaption ? qMax(metrics.horizontalAdvance(idleCaption_),
                        metrics.horizontalAdvance(onCaption_))
                 : 0;
  const int captionHeight = hasCaption ? theme::metric::kControlCaptionGap + metrics.height() : 0;
  // 高度一律按**大按钮**算：一排里 56 与 64 混排时，若各按各的高度，
  // 说明字会分成两行高低——控制条看起来就是坏的。
  return QSize(qMax(diameter(), captionWidth) + 8,
               theme::metric::kControlLarge + captionHeight);
}

void ControlButton::mousePressEvent(QMouseEvent* event) {
  // 禁用态没有按下反馈（§06），但点击照样要到达——上层要弹提示。
  if (blockedReason_.isEmpty()) {
    pressAnimation_->stop();
    pressAnimation_->setStartValue(scale_);
    pressAnimation_->setEndValue(kPressedScale);
    pressAnimation_->start();
  }
  QAbstractButton::mousePressEvent(event);
}

void ControlButton::mouseReleaseEvent(QMouseEvent* event) {
  if (blockedReason_.isEmpty()) {
    pressAnimation_->stop();
    pressAnimation_->setStartValue(scale_);
    pressAnimation_->setEndValue(1.0);
    pressAnimation_->start();
  } else if (rect().contains(event->pos())) {
    // 「点了要出提示不能静默」——所以吃掉这次点击，只发提示信号。
    emit blockedClicked(blockedReason_);
    return;
  }
  QAbstractButton::mouseReleaseEvent(event);
}

void ControlButton::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  const bool blocked = !blockedReason_.isEmpty();

  QColor background;
  QColor foreground;
  switch (kind_) {
    case Kind::Toggle:
      background = on_ ? theme::call::ctlOn() : theme::call::ctlIdle();
      foreground = on_ ? theme::call::ctlOnFg() : theme::call::fg();
      break;
    case Kind::Danger:
      background = theme::call::danger();
      foreground = theme::call::fg();
      break;
    case Kind::Accept:
      background = theme::call::accept();
      foreground = QColor(0x08, 0x21, 0x0F);  // §06：接听态图标是深绿，不是白
      break;
  }
  if (blocked) {
    background = theme::call::ctlIdle();
    background.setAlpha(kBlockedAlpha * background.alpha() / 255);
    foreground = theme::call::fg();
    foreground.setAlpha(kBlockedAlpha);
  } else if (underMouse()) {
    // 桌面独有：hover 底色 14% → 22%（UX_FLOWS §07 第 6 条）。
    if (kind_ == Kind::Toggle && !on_) background.setAlpha(56);  // 22% → 56/255
  }

  const int size = diameter();
  // 圆在「大按钮高度」这条带里垂直居中，说明字统一画在带下方——
  // 这样 56 的静音与 64 的挂断，说明字在同一条基线上。
  // 紧凑版没有说明字，圆直接在整个控件里居中。
  const int band = compact_ ? height() : theme::metric::kControlLarge;
  const QRect circle((width() - size) / 2, (band - size) / 2, size, size);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  painter.save();
  painter.translate(circle.center());
  painter.scale(scale_, scale_);
  painter.translate(-circle.center());
  painter.setPen(Qt::NoPen);
  painter.setBrush(background);
  painter.drawEllipse(circle);

  const icons::Name symbol = (kind_ == Kind::Toggle && on_) ? onSymbol_ : idleSymbol_;
  const int glyph = iconSize();
  const QPixmap pixmap =
      icons::pixmap(symbol, glyph, foreground, devicePixelRatioF());
  painter.drawPixmap(circle.center().x() - glyph / 2, circle.center().y() - glyph / 2,
                     glyph, glyph, pixmap);
  painter.restore();

  const QString caption = on_ ? onCaption_ : idleCaption_;
  if (!caption.isEmpty() && !compact_) {
    painter.setFont(theme::type::c2());
    painter.setPen(theme::call::fgDim());
    const int top = theme::metric::kControlLarge + theme::metric::kControlCaptionGap;
    const QRect captionRect(0, top, width(), height() - top);
    // 真放不下时省略号收尾，**不许硬切**——切一半的字比省略号更像 bug。
    const QString shown =
        painter.fontMetrics().elidedText(caption, Qt::ElideRight, captionRect.width());
    painter.drawText(captionRect, Qt::AlignHCenter | Qt::AlignTop, shown);
  }
}
