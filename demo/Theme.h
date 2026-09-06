#pragma once

/**
 * Theme.h —— 设计令牌，**唯一来源**是
 * `im-rtc-server/docs/design/sketches/RTC_CALL_UI_SPEC.html` §02–§04。
 *
 * 组件里**禁止写字面色值**（那份稿子的铁律，四端同守）。要改色只改这里一行。
 * 稿子里那张「四端变量名对照」表点名了 Qt 这一列的名字（@ctlIdle / @danger …），
 * 本文件的常量名与之一一对应，改稿时能逐行对账。
 *
 * # 深色只属于通话页
 *
 * 稿子 §03 明确写了：**通话页固定深色、不随宿主主题；Demo 页面跟系统**。
 * 所以下面 `call` 命名空间里的颜色是写死的，而登录/拨号/记录/设置四屏
 * 一律用 QPalette 的系统色——别拿 call:: 的颜色去刷 Demo 页面。
 */

#include <QColor>
#include <QFont>
#include <QString>

namespace theme {

/** 通话页令牌（§02）。**固定深色**，不跟随系统主题。 */
namespace call {

QColor surfaceOverlay();   ///< @surfaceOverlay  #121418 / .96  全屏遮罩底色
QColor callBgTop();        ///< @callBg 起点     #2A3350        语音页径向渐变
QColor callBgBottom();     ///< @callBg 终点     #0F1117
QColor tile();             ///< @tileBg          #000000        格子底 / 信箱边
QColor avatar();           ///< @avatarBg        #2B3038        首字母头像兜底底
QColor ctlIdle();          ///< @ctlIdle         白 14%         控制按钮常态
QColor ctlOn();            ///< @ctlOn           #FFFFFF        开启态是**反白**，不是变蓝
QColor ctlOnFg();          ///< @ctlOn 前景      #121418
QColor danger();           ///< @danger          #E5484D        挂断 / 拒绝 / 取消，恒红
QColor accept();           ///< @accept          #3DDC84        接听 + 正在说话描边
QColor fg();               ///< @fg              #FFFFFF
QColor fgDim();            ///< @fgDim           白 70%
QColor warn();             ///< @warn            #F5A623        网络不佳 / 正在重连

}  // namespace call

/**
 * 头像渐变（§02）。**按 uid 取模，不能随机**——
 * 同一个人在四端、在每一次通话里必须是同一个颜色。
 */
struct AvatarGradient {
  QColor from;
  QColor to;
};

/** fnv1a32(uid) % 9，四端共用这一个哈希。 */
quint32 fnv1a32(const QString& text);
AvatarGradient avatarGradientFor(const QString& uid);

/** 字号（§03）。九级，名字与稿子一致。 */
namespace type {

QFont d1();   ///< 32 Bold        头像内首字母
QFont t1();   ///< 22 Bold        通话页对方名字
QFont t2();   ///< 17 Regular     「通话已结束」
QFont t3();   ///< 16 Semibold    顶部标题 / 来电横幅主标题
QFont b1();   ///< 15 Regular     来电副标题 / 弹窗正文
QFont b2();   ///< 13 Regular     状态副标题 / 小窗时长
QFont c1();   ///< 12 Regular     格子名字标签
QFont c2();   ///< 11 Regular     按钮下的说明字
QFont m1();   ///< 13 Medium 等宽数字，**计时器专用**（不等宽会整行左右抖）

}  // namespace type

/** 尺寸 · 间距 · 圆角（§04）。四端同值。 */
namespace metric {

constexpr int kControl = 56;          ///< 圆控制按钮
constexpr int kControlLarge = 64;     ///< 挂断 / 接听
constexpr int kIcon = 26;             ///< 按钮内图标
constexpr int kIconLarge = 30;        ///< 大按钮内图标
constexpr int kControlGap = 12;       ///< 按钮间距
constexpr int kControlCaptionGap = 7; ///< 圆 → 说明字
constexpr int kAvatarLarge = 96;      ///< 1v1 大头像
constexpr int kTileRadius = 10;       ///< 格子圆角
constexpr int kTileGap = 8;           ///< 九宫格 gap
constexpr int kSelfPipWidth = 160;    ///< 本端小窗（桌面 / Web），16:9
constexpr int kSelfPipHeight = 90;
constexpr int kPipMargin = 12;        ///< 小窗离边距
constexpr int kMiniWindowWidth = 240; ///< 桌面独立小窗
constexpr int kMiniWindowHeight = 168;
constexpr int kOverlayWidth = 520;    ///< 通话浮窗（桌面稿 R）
constexpr int kOverlayHeight = 360;
constexpr int kBannerHeight = 62;     ///< 来电横幅
constexpr int kBannerRadius = 16;
constexpr int kTileLabelHeight = 18;  ///< 格子名字标签
constexpr int kTileLabelRadius = 6;
constexpr int kMuteBadge = 24;        ///< 格子静音角标
constexpr int kTopSmallButton = 32;   ///< 收起小窗 / 添加成员

}  // namespace metric
}  // namespace theme
