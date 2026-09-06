#include "CallOverlay.h"

/**
 * CallOverlayMembers.cpp —— 成员事件与画面层。
 *
 * 从 CallOverlay.cpp 拆出来的：那个文件到 500 行，摸到了体量门禁的警戒线
 * （CONVENTIONS §3：非测试文件 > 600 行打回，480 起告警）。
 *
 * 这一半的职责是**「谁在房间里」以及「他的画面挂在哪」**：成员进出与状态翻转、
 * 九宫格的格子、1v1 那一屏的画面层。另一半（构造、阶段切换、绘制、文案）
 * 留在 CallOverlay.cpp。
 */

#include <QGridLayout>
#include <QLabel>
#include <QStackedWidget>

#include "SoloVideo.h"
#include "VideoTile.h"

namespace {

/** 九宫格：3×3（桌面与手机同值，草图 §05 / §06-S）。 */
constexpr int kGridColumns = 3;

}  // namespace

/* ---- 成员事件 ---- */

VideoTile* CallOverlay::tileFor(const QString& uid) {
  auto it = tiles_.find(uid);
  return it == tiles_.end() ? nullptr : it.value();
}

void CallOverlay::onMemberEntered(const QString& uid) {
  if (!members_.contains(uid)) {
    members_ << uid;
    rebuildTiles();
  }
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Present);
}

void CallOverlay::onMemberLeft(const QString& uid) {
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Left);
}

void CallOverlay::onMemberAccepted(const QString& uid) {
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Present);
}

void CallOverlay::onMemberRejected(const QString& uid) {
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Left);
}

void CallOverlay::onMemberNoResponse(const QString& uid) {
  if (VideoTile* tile = tileFor(uid)) tile->setState(VideoTile::State::Left);
}

void CallOverlay::onMemberAudio(const QString& uid, bool available) {
  if (VideoTile* tile = tileFor(uid)) tile->setMuted(!available);
}

void CallOverlay::onMemberVideo(const QString& uid, bool available) {
  // 这一条就是渲染路径 A 的触发点：对端一发布视频轨，格子就建原生子窗口
  // 并把句柄递给引擎。对端关摄像头就摘掉。
  if (VideoTile* tile = tileFor(uid)) tile->setVideoAvailable(available);
}

void CallOverlay::setFakeVideo(bool on) {
  fakeVideo_ = on;
  for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
    it.value()->setTestPatternEnabled(on);
    it.value()->setVideoAvailable(on);
  }
  syncSoloVideo();
}

/**
 * 1v1 那一屏的画面层。**只在视频通话接通后才建**：
 * 语音通话页是渐变 + 大头像（草图 §03），接通前是「正在呼叫…」，都不该有画面。
 */
void CallOverlay::syncSoloVideo() {
  const bool group = isGroup_ || isRoomMode();
  // 没有真媒体时，只有 --fake-video 才建——否则会盖出一块永远黑的方块。
  const bool wanted = !group && isVideo_ && phase_ == Phase::Connected && fakeVideo_;

  if (!wanted) {
    if (soloVideo_ == nullptr) return;
    // **先摘后拆**，两条口子都要摘：远端一条、本端一条。
    emit detachViewRequested(peer_);
    emit detachLocalViewRequested();
    auto* stack = static_cast<QStackedWidget*>(soloPane_);
    stack->removeWidget(soloVideo_);
    delete soloVideo_;
    soloVideo_ = nullptr;
    stack->setCurrentIndex(0);  // 回到头像那一页
    return;
  }
  if (soloVideo_ != nullptr) return;

  // 画面铺满时切到画面那一页——名字与计时本来就在顶栏（草图 §06-R）。
  auto* stack = static_cast<QStackedWidget*>(soloPane_);
  soloVideo_ = new SoloVideo(stack);
  stack->addWidget(soloVideo_);
  stack->setCurrentWidget(soloVideo_);
  soloVideo_->setTestPattern(fakeVideo_, peer_, tr("我"));

  emit attachViewRequested(peer_, soloVideo_->remoteHandle());
  emit attachLocalViewRequested(soloVideo_->selfHandle());
}

void CallOverlay::onSpeakers(const QList<SpeakerInfo>& speakers) {
  QStringList talking;
  for (const SpeakerInfo& speaker : speakers) talking << speaker.uid;
  for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
    it.value()->setSpeaking(talking.contains(it.key()));
  }
}

void CallOverlay::onQuality(const QList<QualityInfo>& entries) {
  for (const QualityInfo& entry : entries) {
    if (VideoTile* tile = tileFor(entry.uid)) tile->setQualityLevel(entry.level);
  }
}

void CallOverlay::onReconnecting(bool reconnecting) {
  if (phase_ != Phase::Connected) return;
  status_->setVisible(reconnecting);
  status_->setText(reconnecting ? tr("正在重连…") : QString());
}

/* ---- 布局与外观 ---- */

void CallOverlay::rebuildTiles() {
  qDeleteAll(tiles_);
  tiles_.clear();
  while (QLayoutItem* item = grid_->takeAt(0)) delete item;

  if (!isGroup_ && !isRoomMode()) return;

  QStringList everyone = members_;
  if (!selfUid_.isEmpty() && !everyone.contains(selfUid_)) everyone.prepend(selfUid_);

  int index = 0;
  for (const QString& uid : everyone) {
    auto* tile = new VideoTile(gridPane_);
    // **先接线再设状态。**顺序反了的话 setVideoAvailable(true) 发出的
    // attachRequested 落在没人监听的地方，引擎永远拿不到句柄——
    // 而界面看起来一切正常，这类 bug 只能靠日志里"少了一行"发现。
    connect(tile, &VideoTile::attachRequested, this, &CallOverlay::attachViewRequested);
    connect(tile, &VideoTile::detachRequested, this, &CallOverlay::detachViewRequested);

    tile->setIdentity(uid, uid);
    tile->setSelf(uid == selfUid_);
    // 拨出中时除自己外都还在振铃——这一格的「呼叫中…」是真的，由回调翻转。
    tile->setState(uid == selfUid_ || phase_ == Phase::Connected ? VideoTile::State::Present
                                                                 : VideoTile::State::Ringing);
    if (fakeVideo_) {
      tile->setTestPatternEnabled(true);
      tile->setVideoAvailable(true);
    }
    grid_->addWidget(tile, index / kGridColumns, index % kGridColumns);
    tiles_.insert(uid, tile);
    ++index;
  }
}
