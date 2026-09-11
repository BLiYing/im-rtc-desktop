#pragma once

/**
 * ControlButton.h —— 通话页的圆形控制按钮（UI_SPEC §06「控制按钮的五个态」）。
 *
 * 五个态：常态 / 开启 / 危险 / 接听 / 禁用。规格里两条容易漏的：
 *
 *   1. **开启态是反白，不是变蓝**（白底 + 深色图标），图标同时换成 slash 版。
 *   2. **禁用态点了要出提示，不能静默**。所以这里不用 QWidget::setEnabled——
 *      那会让点击事件根本不到达。用 `setBlocked(reason)`：外观照样是禁用样子，
 *      但点击会发 `blockedClicked(reason)` 让上层弹提示。
 *
 * 文案跟着态走，也是稿子定死的：常态写「静音」（点了会发生什么），
 * 开启态写「已静音」（现在是什么状态）。所以 caption 有两份。
 */

#include <QAbstractButton>
#include <QString>

#include "Icons.h"

class QVariantAnimation;

class ControlButton : public QAbstractButton {
  Q_OBJECT

public:
  enum class Kind {
    Toggle,   ///< 常态 ↔ 开启（静音 / 摄像头），56
    Danger,   ///< 挂断 / 拒绝 / 取消 / 离开，恒 64、恒红
    Accept    ///< 接听，恒 64、恒绿
  };

  ControlButton(Kind kind, QWidget* parent = nullptr);

  /** Toggle 用：两个字形 + 两份文案。Danger / Accept 只用第一组。 */
  void setSymbols(icons::Name idle, icons::Name on);
  void setCaptions(const QString& idle, const QString& on);

  /** 开启态（已静音 / 已关摄像头）。 */
  void setOn(bool on);
  bool isOn() const { return on_; }

  /**
   * 禁用外观 + 点击出提示。`reason` 为空表示解除禁用。
   * 用在「权限被拒的摄像头」「满员时的加人」这类地方。
   */
  void setBlocked(const QString& reason);

  /**
   * 紧凑版：38 圆、不画说明字（来电横幅，UI_SPEC §06）。
   * 说明字照样要设——看不见了，它就是悬停提示与无障碍标签的唯一来源。
   */
  void setCompact(bool compact);

  QSize sizeHint() const override;

signals:
  void blockedClicked(const QString& reason);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  int diameter() const;
  int iconSize() const;
  void syncCompactLabels();

  Kind kind_;
  bool compact_ = false;
  icons::Name idleSymbol_ = icons::Name::Mic;
  icons::Name onSymbol_ = icons::Name::MicSlash;
  QString idleCaption_;
  QString onCaption_;
  QString blockedReason_;
  bool on_ = false;
  qreal scale_ = 1.0;
  QVariantAnimation* pressAnimation_ = nullptr;
};
