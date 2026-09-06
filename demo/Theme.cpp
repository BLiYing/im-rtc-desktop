#include "Theme.h"

#include <QFontDatabase>

namespace theme {
namespace {

/**
 * 系统字，**不打包字体**（§03）。往 SDK 里塞一份 2MB 字体，
 * 宿主的包体要为「通话页的字好看一点」买单——不值。
 */
QFont sized(int pointSize, QFont::Weight weight) {
  QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  font.setPointSize(pointSize);
  font.setWeight(weight);
  return font;
}

}  // namespace

namespace call {

QColor surfaceOverlay() { return QColor(0x12, 0x14, 0x18, 245); }  // .96 → 245/255
QColor callBgTop() { return QColor(0x2A, 0x33, 0x50); }
QColor callBgBottom() { return QColor(0x0F, 0x11, 0x17); }
QColor tile() { return QColor(0x00, 0x00, 0x00); }
QColor avatar() { return QColor(0x2B, 0x30, 0x38); }
QColor ctlIdle() { return QColor(255, 255, 255, 36); }   // 14% → 36/255
QColor ctlOn() { return QColor(0xFF, 0xFF, 0xFF); }
QColor ctlOnFg() { return QColor(0x12, 0x14, 0x18); }
QColor danger() { return QColor(0xE5, 0x48, 0x4D); }
QColor accept() { return QColor(0x3D, 0xDC, 0x84); }
QColor fg() { return QColor(0xFF, 0xFF, 0xFF); }
QColor fgDim() { return QColor(255, 255, 255, 179); }    // 70% → 179/255
QColor warn() { return QColor(0xF5, 0xA6, 0x23); }

}  // namespace call

quint32 fnv1a32(const QString& text) {
  // 对 **UTF-8 字节**做哈希，不是对 UTF-16 码元——四端要算出同一个值，
  // 而 Swift / Kotlin / TS 那三端喂进去的都是 UTF-8。
  const QByteArray bytes = text.toUtf8();
  quint32 hash = 2166136261u;
  for (const char byte : bytes) {
    hash ^= static_cast<quint32>(static_cast<unsigned char>(byte));
    hash *= 16777619u;
  }
  return hash;
}

AvatarGradient avatarGradientFor(const QString& uid) {
  // §02 的九个渐变，顺序即 g1..g9，**不许调换**：换了顺序等于换了所有人的颜色。
  static const AvatarGradient kPalette[9] = {
      {QColor(0x9E, 0x7B, 0xF0), QColor(0x6E, 0x52, 0xD6)},  // g1 紫
      {QColor(0x3A, 0xA0, 0xFF), QColor(0x0A, 0x6B, 0xE0)},  // g2 蓝
      {QColor(0x4C, 0xD2, 0x68), QColor(0x28, 0xB1, 0x4A)},  // g3 绿
      {QColor(0xFB, 0xB0, 0x40), QColor(0xF5, 0x87, 0x2B)},  // g4 橙
      {QColor(0xFF, 0x7A, 0xA8), QColor(0xE0, 0x55, 0x9E)},  // g5 粉
      {QColor(0x5E, 0xD3, 0xD0), QColor(0x2A, 0xA6, 0xA3)},  // g6 青
      {QColor(0xB0, 0xB8, 0xC8), QColor(0x7E, 0x87, 0x97)},  // g7 灰
      {QColor(0xF0, 0x8A, 0x5D), QColor(0xC9, 0x4F, 0x3B)},  // g8 砖红
      {QColor(0x7C, 0x9C, 0xF0), QColor(0x4C, 0x6B, 0xD6)},  // g9 靛
  };
  return kPalette[fnv1a32(uid) % 9u];
}

namespace type {

QFont d1() { return sized(32, QFont::Bold); }
QFont t1() { return sized(22, QFont::Bold); }
QFont t2() { return sized(17, QFont::Normal); }
QFont t3() { return sized(16, QFont::DemiBold); }
QFont b1() { return sized(15, QFont::Normal); }
QFont b2() { return sized(13, QFont::Normal); }
QFont c1() { return sized(12, QFont::Normal); }
QFont c2() { return sized(11, QFont::Normal); }

QFont m1() {
  // 计时器每秒跳动，比例字体会让整行左右抖。等宽是**功能需求**，不是审美。
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setPointSize(13);
  font.setWeight(QFont::Medium);
  return font;
}

}  // namespace type
}  // namespace theme
