#pragma once

/**
 * SoloVideo.h —— 1v1 视频通话的画面区（草图 §06-R，渲染路径 A）。
 *
 * 两层原生子窗口：
 *   - **远端**铺满整块
 *   - **本端**小窗 160×90，右下角，离边 12（UI_SPEC §04）
 *
 * 顺序不能反：macOS 上原生子窗口按 NSView 的兄弟顺序合成，**后建的在上面**。
 * 先建远端再建本端，小窗才压得住大画面；反过来小窗会被整块盖掉。
 *
 * # 本端预览走的是另一条口子
 *
 * 远端用 `attachView(uid, handle)`，本端用 **`attachLocalView(handle)`**。
 * 不是同一个函数：引擎不知道自己的 uid，而且本端画面来自采集侧，没有远端轨道 id。
 * 这条口子是做这一屏时才发现缺的，2026-09-07 加进 C ABI（追加式，不动已有符号）。
 *
 * # 名字与状态去哪了
 *
 * 没画在这里。原生子窗口会盖住同一个窗口里所有 Qt 绘制，而这一屏的名字与计时
 * 本来就在**顶栏**（草图 §06-R 就是这么画的），不必再叠一层外壳。
 * 九宫格那边不一样——标签必须贴在格子里，所以那边有 `TileChrome`。
 */

#include <QWidget>

class NativeSurface;

class SoloVideo : public QWidget {
  Q_OBJECT

public:
  explicit SoloVideo(QWidget* parent = nullptr);

  /** 远端句柄，交给 `attachView(peerUid, …)`。 */
  void* remoteHandle();
  /** 本端句柄，交给 `attachLocalView(…)`。 */
  void* selfHandle();

  /** 联调用：两块都贴测试图案，验层级与缩放。 */
  void setTestPattern(bool on, const QString& peerLabel, const QString& selfLabel);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void layoutSurfaces();

  NativeSurface* remote_ = nullptr;
  NativeSurface* self_ = nullptr;
};
