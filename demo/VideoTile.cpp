#include "VideoTile.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include "Avatar.h"
#include "Icons.h"
#include "NativeSurface.h"
#include "Theme.h"

namespace {

constexpr qreal kSpeakingBorder = 2.5;  ///< §04：2.5 内描边，**不撑大格子**
constexpr int kInset = 8;               ///< 标签与角标离边 8

}  // namespace

/**
 * TileChrome —— 压在画面之上的那层外壳。
 *
 * 它必须是**在画面之后创建的原生子窗口**：macOS 上原生子窗口按 NSView 的兄弟
 * 顺序合成，后来的在上面，而 Qt 自己画的东西一律在原生子窗口**之下**。
 * 所以名字标签、静音角标、发言描边不能由格子直接画——有画面时会被完全吃掉。
 */
class TileChrome : public QWidget {
public:
  explicit TileChrome(VideoTile* tile) : QWidget(tile), tile_(tile) {
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    // 外壳只是画，不吃鼠标——否则格子上的点击全被它挡住。
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    tile_->paintChrome(&painter, rect());
  }

private:
  VideoTile* tile_;
};

VideoTile::VideoTile(QWidget* parent) : QWidget(parent) {
  setMinimumSize(120, 90);
}

void VideoTile::refresh() {
  update();
  // 外壳是压在画面之上的另一个原生子窗口，格子的 update() 传不到它那里。
  if (chrome_ != nullptr) chrome_->update();
}

void VideoTile::setIdentity(const QString& uid, const QString& displayName) {
  uid_ = uid;
  displayName_ = displayName;
  refresh();
}

void VideoTile::setState(State state) {
  if (state_ == state) return;
  state_ = state;
  refresh();
}

void VideoTile::setSpeaking(bool speaking) {
  if (speaking_ == speaking) return;
  speaking_ = speaking;
  refresh();
}

void VideoTile::setMuted(bool muted) {
  if (muted_ == muted) return;
  muted_ = muted;
  refresh();
}

void VideoTile::setQualityLevel(int level) {
  if (quality_ == level) return;
  quality_ = level;
  refresh();
}

void VideoTile::setSelf(bool self) {
  self_ = self;
  refresh();
}

void VideoTile::setVideoAvailable(bool available) {
  if (available == (surface_ != nullptr)) return;

  if (!available) {
    // **先摘后拆**。反过来的话引擎手里会剩一个已经销毁的 NSView / HWND，
    // 下一帧画上去就是野指针——这类崩溃发生在引擎线程上，栈里看不到界面代码。
    emit detachRequested(uid_);
    delete chrome_;
    chrome_ = nullptr;
    delete surface_;
    surface_ = nullptr;
    update();
    return;
  }

  surface_ = new NativeSurface(this);
  // 格子有 10 的圆角，原生子窗口是方的：留一圈内边距，免得直角戳出圆角。
  surface_->setGeometry(rect().adjusted(2, 2, -2, -2));
  surface_->show();
  if (testPattern_ && NativeSurface::testPatternSupported()) {
    surface_->showTestPattern(self_ ? tr("我") : uid_);
  }

  // **外壳必须在画面之后创建**，否则会被画面盖住（NSView 兄弟顺序即 z 序）。
  chrome_ = new TileChrome(this);
  chrome_->setGeometry(rect());
  chrome_->show();
  chrome_->raise();

  emit attachRequested(uid_, surface_->nativeHandle());
  refresh();
}

void VideoTile::setTestPatternEnabled(bool enabled) {
  testPattern_ = enabled;
  if (surface_ == nullptr) return;
  if (enabled && NativeSurface::testPatternSupported()) {
    surface_->showTestPattern(self_ ? tr("我") : uid_);
  } else {
    surface_->clearTestPattern();
  }
}

void VideoTile::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  // 原生子窗口不参与 Qt 布局，得手动跟着走。
  if (surface_ != nullptr) surface_->setGeometry(rect().adjusted(2, 2, -2, -2));
  if (chrome_ != nullptr) chrome_->setGeometry(rect());
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

  // 有画面时：头像与外壳都由上面那两层原生子窗口负责，这里只保留圆角底，
  // 那一圈 2px 的内边距正好把方角的画面收进圆角里。
  if (surface_ != nullptr) return;

  // ---- 没有画面时露头像。 ----
  const int avatarSize = qMin(qMin(width(), height()) / 2, theme::metric::kAvatarLarge);
  const QRect avatarBox(rect().center().x() - avatarSize / 2,
                        rect().center().y() - avatarSize / 2 - 6, avatarSize, avatarSize);
  painter.save();
  if (state_ == State::Ringing || state_ == State::Left) painter.setOpacity(0.45);
  paintAvatar(&painter, avatarBox, uid_, displayName_, qMax(11, avatarSize * 32 / 96));
  painter.restore();


  paintChrome(&painter, rect());
}

/**
 * 外壳：名字标签 / 静音角标 / 弱网 / 发言描边。
 *
 * 抽出来是因为它要画两遍——没有画面时格子自己画，有画面时由压在画面之上的
 * `chrome_` 那层画。两处必须是同一份代码，否则「开了摄像头之后标签位置变了」
 * 这种事没人查得出来。
 */
void VideoTile::paintChrome(QPainter* painter, const QRect& box) const {
  // ---- 左下名字标签：高 18、圆角 6、内边距 2/8 ----
  const QString label = self_ ? tr("我") : (displayName_.isEmpty() ? uid_ : displayName_);
  painter->setFont(theme::type::c1());
  const int labelWidth = painter->fontMetrics().horizontalAdvance(label) + 16;
  const QRect labelBox(box.left() + kInset,
                       box.bottom() - kInset - theme::metric::kTileLabelHeight + 1, labelWidth,
                       theme::metric::kTileLabelHeight);
  painter->setPen(Qt::NoPen);
  painter->setBrush(QColor(0, 0, 0, 140));
  painter->drawRoundedRect(labelBox, theme::metric::kTileLabelRadius,
                          theme::metric::kTileLabelRadius);
  painter->setPen(theme::call::fg());
  painter->drawText(labelBox, Qt::AlignCenter, label);

  // 状态说明与名字标签同一条带、右对齐——格子很矮，另起一行必然和标签打架。
  if (state_ != State::Present) {
    const QString note = state_ == State::Ringing ? tr("呼叫中…") : tr("已离开");
    painter->setPen(theme::call::fgDim());
    painter->drawText(QRect(labelBox.right() + 6, labelBox.top(),
                            box.right() - labelBox.right() - 6 - kInset, labelBox.height()),
                     Qt::AlignRight | Qt::AlignVCenter, note);
  }

  // ---- 右上静音角标：24 圆、黑 55% 底 ----
  if (muted_) {
    const QRect badge(box.right() - kInset - theme::metric::kMuteBadge + 1, box.top() + kInset,
                      theme::metric::kMuteBadge, theme::metric::kMuteBadge);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 140));
    painter->drawEllipse(badge);
    const int glyph = 14;
    painter->drawPixmap(badge.center().x() - glyph / 2 + 1, badge.center().y() - glyph / 2 + 1,
                       glyph, glyph,
                       icons::pixmap(icons::Name::MicSlash, glyph, theme::call::fg(),
                                     devicePixelRatioF()));
  }

  // ---- 弱网：右下 net-bars，用 warn 色 ----
  if (quality_ > 0 && quality_ <= 2) {
    const int glyph = 16;
    painter->drawPixmap(box.right() - kInset - glyph + 1, box.bottom() - kInset - glyph + 1,
                        glyph, glyph,
                       icons::pixmap(icons::Name::NetBars, glyph, theme::call::warn(),
                                     devicePixelRatioF()));
  }

  // ---- 正在说话：2.5 内描边，画在最后免得被别的盖住 ----
  if (speaking_) {
    QPen pen(theme::call::accept());
    pen.setWidthF(kSpeakingBorder);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    const qreal offset = kSpeakingBorder / 2.0;
    painter->drawRoundedRect(QRectF(box).adjusted(offset, offset, -offset, -offset),
                            theme::metric::kTileRadius - offset,
                            theme::metric::kTileRadius - offset);
  }
}

