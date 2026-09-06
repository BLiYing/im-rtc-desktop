#include "SoloVideo.h"

#include <QResizeEvent>

#include "NativeSurface.h"
#include "Theme.h"

SoloVideo::SoloVideo(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAttribute(Qt::WA_NoSystemBackground, true);

  // **顺序即 z 序**：先远端后本端，小窗才在上面。
  remote_ = new NativeSurface(this);
  remote_->show();
  self_ = new NativeSurface(this);
  self_->show();

  layoutSurfaces();
}

void* SoloVideo::remoteHandle() { return remote_->nativeHandle(); }
void* SoloVideo::selfHandle() { return self_->nativeHandle(); }

void SoloVideo::setTestPattern(bool on, const QString& peerLabel, const QString& selfLabel) {
  if (!on || !NativeSurface::testPatternSupported()) {
    remote_->clearTestPattern();
    self_->clearTestPattern();
    return;
  }
  remote_->showTestPattern(peerLabel);
  self_->showTestPattern(selfLabel);
}

void SoloVideo::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  layoutSurfaces();
}

void SoloVideo::layoutSurfaces() {
  remote_->setGeometry(rect());

  // 本端小窗：桌面是 160×90（16:9），离边 12（UI_SPEC §04）。
  // 窗口太小就按比例缩，否则小窗会盖掉大半个画面。
  const int maxWidth = qMax(80, width() / 3);
  const int pipWidth = qMin(theme::metric::kSelfPipWidth, maxWidth);
  const int pipHeight = pipWidth * theme::metric::kSelfPipHeight / theme::metric::kSelfPipWidth;
  const int margin = theme::metric::kPipMargin;
  self_->setGeometry(width() - pipWidth - margin, height() - pipHeight - margin, pipWidth,
                     pipHeight);
}
