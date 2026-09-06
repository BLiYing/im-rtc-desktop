/**
 * NativeSurfaceTest.cpp —— 渲染路径 A 宿主侧的回归测试（设计 §8.3）。
 *
 * 真实画面要等 `WebRTCAdapter`，但**把原生子窗口塞进 Qt 布局**这件事的坑
 * 与视频无关，现在就能钉住：
 *
 *   1. 句柄拿得到，而且是**这个控件自己的**，不是某个祖先的
 *      （拿错了引擎会画到整块面板上，而不是这一格里）。
 *   2. 拉窗口时原生层跟得上——它不参与 Qt 布局，得手动同步。
 *   3. DPI 对得上——`contentsScale` 必须等于窗口的 backing scale，
 *      否则 Retina 上是糊的、外接屏上是错的。
 *   4. **销毁前先 detach**。顺序反了，引擎手里就是一个已销毁的 NSView，
 *      下一帧画上去就崩，而且崩在引擎线程上，栈里看不到界面代码。
 *
 * 这些用例需要真窗口，所以**不能跑 offscreen**（QT_QPA_PLATFORM=offscreen 下
 * 没有 NSWindow，backing scale 无从谈起）。没有窗口系统时整个文件跳过。
 */

#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

#include "CallOverlay.h"
#include "NativeSurface.h"
#include "SoloVideo.h"
#include "Theme.h"
#include "VideoTile.h"

class NativeSurfaceTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void handleIsOwnNotAncestor();
  void patternFollowsResize();
  void patternMatchesBackingScale();
  void tileDetachesBeforeDestroying();
  void tileTogglesCleanly();
  void layerFollowsTileLayout();
  void chromeSitsAboveSurface();
  void soloVideoPutsSelfAboveRemote();
  void soloVideoAttachesBothPipes();

private:
  bool headless_ = false;
};

void NativeSurfaceTest::initTestCase() {
  headless_ = qgetenv("QT_QPA_PLATFORM") == "offscreen";
  if (headless_) {
    QSKIP("offscreen 平台没有真窗口，原生层的几何与 DPI 无从验起");
  }
}

/**
 * 句柄必须是这个控件自己的原生窗口。
 * 判据：父控件**不该**因此也变成原生的（WA_DontCreateNativeAncestors 的作用），
 * 而且子控件的句柄与父控件的不同。
 */
void NativeSurfaceTest::handleIsOwnNotAncestor() {
  QWidget host;
  host.resize(320, 200);
  auto* surface = new NativeSurface(&host);
  surface->setGeometry(10, 10, 200, 120);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));

  void* handle = surface->nativeHandle();
  QVERIFY(handle != nullptr);
  // 父控件不该被顺带提升成原生窗口。
  QVERIFY(!host.testAttribute(Qt::WA_NativeWindow) || host.winId() != surface->winId());
  QVERIFY(reinterpret_cast<void*>(host.winId()) != handle);
}

/** 原生层不参与 Qt 布局：拉大之后它必须自己跟上，否则画面只占左下角一块。 */
void NativeSurfaceTest::patternFollowsResize() {
  if (!NativeSurface::testPatternSupported()) QSKIP("这个平台还没有假渲染器");

  QWidget host;
  host.resize(400, 300);
  auto* surface = new NativeSurface(&host);
  surface->setGeometry(0, 0, 160, 90);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));

  surface->showTestPattern(QStringLiteral("t"));
  QTRY_COMPARE(surface->patternGeometry().size().toSize(), QSize(160, 90));

  surface->setGeometry(0, 0, 320, 180);
  QTRY_COMPARE(surface->patternGeometry().size().toSize(), QSize(320, 180));
  QVERIFY(surface->resizeCount() > 0);
}

/** Retina 上 contentsScale 必须是 2，否则原生层是糊的。 */
void NativeSurfaceTest::patternMatchesBackingScale() {
  if (!NativeSurface::testPatternSupported()) QSKIP("这个平台还没有假渲染器");

  QWidget host;
  host.resize(300, 200);
  auto* surface = new NativeSurface(&host);
  surface->setGeometry(0, 0, 200, 120);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  surface->showTestPattern(QStringLiteral("t"));

  QTRY_VERIFY(surface->patternScale() > 0.0);
  QCOMPARE(surface->patternScale(), surface->backingScale());
}

/**
 * 生命周期那一条：格子把画面关掉时，**detachRequested 必须先于窗口销毁**。
 * 这里断言的是「信号发过了」，顺序由 VideoTile::setVideoAvailable 的实现保证
 * （它先 emit 再 delete）。
 */
void NativeSurfaceTest::tileDetachesBeforeDestroying() {
  QWidget host;
  host.resize(300, 200);
  auto* tile = new VideoTile(&host);
  tile->setIdentity(QStringLiteral("bob"), QStringLiteral("bob"));
  tile->setGeometry(0, 0, 200, 120);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));

  QSignalSpy attach(tile, &VideoTile::attachRequested);
  QSignalSpy detach(tile, &VideoTile::detachRequested);

  tile->setVideoAvailable(true);
  QCOMPARE(attach.count(), 1);
  QVERIFY(tile->videoAvailable());
  // 递上去的必须是个真句柄，不是 nullptr。
  QVERIFY(attach.at(0).at(1).value<void*>() != nullptr);

  tile->setVideoAvailable(false);
  QCOMPARE(detach.count(), 1);
  QVERIFY(!tile->videoAvailable());
}

/** 反复开关不该重复 attach，也不该漏 detach——对端频繁开关摄像头就是这个场景。 */
void NativeSurfaceTest::tileTogglesCleanly() {
  QWidget host;
  host.resize(300, 200);
  auto* tile = new VideoTile(&host);
  tile->setIdentity(QStringLiteral("carol"), QStringLiteral("carol"));
  tile->setGeometry(0, 0, 200, 120);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));

  QSignalSpy attach(tile, &VideoTile::attachRequested);
  QSignalSpy detach(tile, &VideoTile::detachRequested);

  for (int i = 0; i < 3; ++i) {
    tile->setVideoAvailable(true);
    tile->setVideoAvailable(true);  // 幂等：重复调用不该再 attach 一次
    tile->setVideoAvailable(false);
    tile->setVideoAvailable(false);
  }
  QCOMPARE(attach.count(), 3);
  QCOMPARE(detach.count(), 3);
}

/**
 * 真正踩到的那个 bug 的回归：**格子先按最小尺寸建好画面层，随后才被布局撑大**。
 *
 * 原实现从 `view.layer.bounds` 读几何，而 Qt 的 `resizeEvent` 触发时底层 NSView
 * 还没跟上，于是层永远停在初始的 116×86，格子右边露出一条底色，而且**不会再有
 * 第二次 resizeEvent 来纠正**。实测数据就是这么发现的：
 *   tile 157×92 / widget 153×88 / layer 116×86
 * 现在几何以 Qt 控件尺寸为准。
 */
void NativeSurfaceTest::layerFollowsTileLayout() {
  if (!NativeSurface::testPatternSupported()) QSKIP("这个平台还没有假渲染器");

  QWidget host;
  auto* layout = new QVBoxLayout(&host);
  auto* tile = new VideoTile;
  layout->addWidget(tile);

  // **顺序就是 bug 的现场**：先在还没显示、还没布局的时候把画面层建起来
  // （真实路径里 rebuildTiles 就是这么干的），再 show，再被布局撑大。
  tile->setTestPatternEnabled(true);
  tile->setVideoAvailable(true);

  host.resize(420, 300);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  QApplication::processEvents();

  auto* surface = tile->findChild<NativeSurface*>();
  QVERIFY(surface != nullptr);
  QVERIFY2(surface->size().width() > 200, "格子应当被布局撑大，否则这条用例没测到东西");
  QTRY_COMPARE(surface->patternGeometry().size().toSize(), surface->size());
}

/**
 * 外壳（名字标签 / 静音角标 / 发言描边）必须是**画面之后**创建的另一个原生子窗口。
 * macOS 上原生子窗口按 NSView 兄弟顺序合成，而 Qt 自己画的东西一律在它们之下——
 * 外壳若由格子直接 paintEvent 画，开了画面就会被整块吃掉。
 */
void NativeSurfaceTest::chromeSitsAboveSurface() {
  QWidget host;
  host.resize(300, 200);
  auto* tile = new VideoTile(&host);
  tile->setIdentity(QStringLiteral("bob"), QStringLiteral("bob"));
  tile->setGeometry(0, 0, 200, 120);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));

  tile->setVideoAvailable(true);

  const QList<QWidget*> children = tile->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
  QCOMPARE(children.size(), 2);
  // 顺序即 z 序：画面在前，外壳在后。
  QVERIFY(qobject_cast<NativeSurface*>(children.at(0)) != nullptr);
  QVERIFY(qobject_cast<NativeSurface*>(children.at(1)) == nullptr);
  // 外壳必须也是原生窗口，否则它照样会被画面盖住。
  QVERIFY(children.at(1)->testAttribute(Qt::WA_NativeWindow));

  // 关掉画面时两层都要收走，不能剩一个空外壳挡着头像。
  tile->setVideoAvailable(false);
  QCOMPARE(tile->findChildren<QWidget*>(Qt::FindDirectChildrenOnly).size(), 0);
}

/**
 * 1v1 那一屏：远端铺满、本端小窗 160×90 压在右下角。
 * **顺序即 z 序**——先建远端后建本端，反过来小窗会被大画面整块盖掉。
 */
void NativeSurfaceTest::soloVideoPutsSelfAboveRemote() {
  QWidget host;
  host.resize(520, 300);
  auto* video = new SoloVideo(&host);
  video->setGeometry(0, 0, 520, 300);
  host.show();
  QVERIFY(QTest::qWaitForWindowExposed(&host));
  QApplication::processEvents();

  const QList<QWidget*> layers = video->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
  QCOMPARE(layers.size(), 2);
  QVERIFY(reinterpret_cast<void*>(layers.at(0)->winId()) == video->remoteHandle());
  QVERIFY(reinterpret_cast<void*>(layers.at(1)->winId()) == video->selfHandle());

  // 远端铺满
  QCOMPARE(layers.at(0)->size(), video->size());
  // 本端 160×90，离右下角各 12（UI_SPEC §04）
  const QRect pip = layers.at(1)->geometry();
  QCOMPARE(pip.size(), QSize(theme::metric::kSelfPipWidth, theme::metric::kSelfPipHeight));
  QCOMPARE(video->width() - pip.right() - 1, theme::metric::kPipMargin);
  QCOMPARE(video->height() - pip.bottom() - 1, theme::metric::kPipMargin);
}

/**
 * 1v1 视频接通时要挂**两条**口子：远端走 attachView(uid)，本端走 attachLocalView()。
 * 结束时两条都要摘，而且要在 peer_ 还在的时候摘——否则 detach 发不对人。
 */
void NativeSurfaceTest::soloVideoAttachesBothPipes() {
  CallOverlay overlay;
  overlay.setSelfUid(QStringLiteral("alice"));
  overlay.setFakeVideo(true);

  QSignalSpy attach(&overlay, &CallOverlay::attachViewRequested);
  QSignalSpy attachLocal(&overlay, &CallOverlay::attachLocalViewRequested);
  QSignalSpy detach(&overlay, &CallOverlay::detachViewRequested);
  QSignalSpy detachLocal(&overlay, &CallOverlay::detachLocalViewRequested);

  overlay.beginOutgoing({QStringLiteral("bob")}, QStringLiteral("video"), false);
  QCOMPARE(attach.count(), 0);          // 接通前不该有画面
  overlay.markConnected(QStringLiteral("caller"));

  QCOMPARE(attach.count(), 1);
  QCOMPARE(attach.at(0).at(0).toString(), QStringLiteral("bob"));
  QVERIFY(attach.at(0).at(1).value<void*>() != nullptr);
  QCOMPARE(attachLocal.count(), 1);
  QVERIFY(attachLocal.at(0).at(0).value<void*>() != nullptr);

  overlay.markEnded(QStringLiteral("hangup"), 5);
  QCOMPARE(detach.count(), 1);
  QCOMPARE(detach.at(0).at(0).toString(), QStringLiteral("bob"));
  QCOMPARE(detachLocal.count(), 1);
}

QTEST_MAIN(NativeSurfaceTest)
#include "NativeSurfaceTest.moc"
