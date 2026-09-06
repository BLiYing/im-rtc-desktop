#pragma once

/**
 * Icons.h —— 26×26 线性图标（UI_SPEC §05）。
 *
 * **路径数据是从设计稿里原样搬过来的**，一个数字都没改。稿子 §01 那张
 * 「谁负责画哪一层」表写死了：图标四端用**同一份 SVG 路径**，iOS 不许换成
 * SF Symbols、Android 不许换成 Material。自己照着画一个「差不多的」话筒，
 * 四端就会各自漂一点点，最后没人说得清哪个才是对的。
 *
 * 所以这里用 QtSvg 渲染原始 markup，而不是拿 QPainterPath 重画。
 * `currentColor` 在渲染时替换成实际颜色——与 Web 端同一个套路。
 */

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace icons {

enum class Name {
  Mic,          ///< 静音按钮常态
  MicSlash,     ///< 已静音（开启态换 slash 版，§06）
  Video,
  VideoSlash,
  Phone,        ///< 接听
  PhoneDown,    ///< 挂断 / 取消 / 离开（phone 旋转 135°）
  Xmark,        ///< 拒绝
  Minimize,     ///< 收起小窗
  Expand,       ///< 全屏
  Speaker,
  SpeakerSlash,
  CameraFlip,
  PersonAdd,    ///< 添加成员，只有主叫可见（协议 1407）
  ChevronDown,
  More,
  ScreenShare,
  NetBars,
  Grid
};

/** 按 `size` 逻辑像素出图，已按 `dpr` 放大（Retina 上不糊）。 */
QPixmap pixmap(Name name, int size, const QColor& color, qreal dpr);

/** 便利版：给 QAbstractButton::setIcon 用。 */
QIcon icon(Name name, int size, const QColor& color);

}  // namespace icons
