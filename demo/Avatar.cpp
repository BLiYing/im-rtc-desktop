#include "Avatar.h"

#include <QLinearGradient>
#include <QPainter>

#include "Theme.h"

namespace {

/**
 * 取首字母。CJK 直接取第一个字符，拉丁字母转大写。
 * 名字为空时退回 uid，再空就画一个「?」——**不许画空白圆**，那看起来像加载失败。
 */
QString initialOf(const QString& uid, const QString& displayName) {
  const QString source = displayName.trimmed().isEmpty() ? uid.trimmed() : displayName.trimmed();
  if (source.isEmpty()) return QStringLiteral("?");
  return source.left(1).toUpper();
}

}  // namespace

void paintAvatar(QPainter* painter, const QRect& rect, const QString& uid,
                 const QString& displayName, int pointSize) {
  const theme::AvatarGradient gradient = theme::avatarGradientFor(uid);
  QLinearGradient brush(rect.topLeft(), rect.bottomRight());
  brush.setColorAt(0.0, gradient.from);
  brush.setColorAt(1.0, gradient.to);

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setPen(Qt::NoPen);
  painter->setBrush(brush);
  painter->drawEllipse(rect);

  QFont font = theme::type::d1();
  font.setPointSize(pointSize);
  painter->setFont(font);
  painter->setPen(theme::call::fg());
  painter->drawText(rect, Qt::AlignCenter, initialOf(uid, displayName));
  painter->restore();
}

AvatarWidget::AvatarWidget(int diameter, QWidget* parent)
    : QWidget(parent), diameter_(diameter) {
  setFixedSize(diameter, diameter);
}

void AvatarWidget::setIdentity(const QString& uid, const QString& displayName) {
  uid_ = uid;
  displayName_ = displayName;
  update();
}

void AvatarWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  // 字号按直径缩放：96 的头像配 32pt（§03 的 D1），小头像等比例缩。
  const int pointSize = qMax(11, diameter_ * 32 / theme::metric::kAvatarLarge);
  paintAvatar(&painter, rect().adjusted(0, 0, -1, -1), uid_, displayName_, pointSize);
}
