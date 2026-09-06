#pragma once

/**
 * Avatar.h —— 首字母头像（UI_SPEC §02「头像色板」）。
 *
 * 没有头像图时用首字母 + 九个渐变之一。取哪一个**由 uid 决定，不能随机**：
 * 同一个人在四端、在每一次通话里必须是同一个颜色，否则同一个人在你手机上是紫的、
 * 在对方电脑上是绿的。
 */

#include <QString>
#include <QWidget>

class QPainter;

/** 画一个圆形头像到 `rect`。给格子、来电横幅这些不需要独立控件的地方复用。 */
void paintAvatar(QPainter* painter, const QRect& rect, const QString& uid,
                 const QString& displayName, int pointSize);

/** 独立控件版，尺寸由 `diameter` 定（1v1 大头像是 96）。 */
class AvatarWidget : public QWidget {
  Q_OBJECT

public:
  explicit AvatarWidget(int diameter, QWidget* parent = nullptr);

  void setIdentity(const QString& uid, const QString& displayName);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QString uid_;
  QString displayName_;
  int diameter_;
};
