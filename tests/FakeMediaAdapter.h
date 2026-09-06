#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "imrtc/MediaAdapter.h"

namespace imtest {

/**
 * 一个可编排的假媒体适配器。
 *
 * 默认**同步完成**：调用即回调，测试读起来是一条直线。
 * 打开 `deferCompletions` 之后完成回调排进队列，`poll()` 才放出来——
 * 那才是真适配器的样子（libwebrtc 的 createOffer 是异步的），
 * 至少要有一条用例跑在那个模式下，否则「异步」这件事等于没测。
 */
class FakeMediaAdapter : public imrtc::MediaAdapter {
public:
  // ---- 编排 ----
  bool deferCompletions = false;
  bool micAllowed = true;
  bool cameraAllowed = true;
  std::int32_t micErrorCode = 2001;     // device_permission_denied
  std::int32_t cameraErrorCode = 2001;

  // ---- 记录 ----
  int openCount = 0;
  int resetCount = 0;
  int closeCount = 0;
  std::vector<std::string> calls;
  std::vector<std::string> appliedPubAnswers;
  std::vector<std::string> answeredSubOffers;
  std::vector<std::string> remoteCandidates;
  std::vector<std::string> muted;
  std::vector<std::string> attachedViews;

  imrtc::MediaAdapterEvents events;

  void open(imrtc::MediaAdapterEvents e) override {
    ++openCount;
    events = std::move(e);
    calls.push_back("open");
  }

  void probeMicrophone(imrtc::VoidCompletion done) override {
    calls.push_back("probeMicrophone");
    complete([this, done]() {
      if (done) done(micAllowed, micAllowed ? 0 : micErrorCode);
    });
  }

  void startLocalPreview(imrtc::TrackCompletion done) override {
    calls.push_back("startLocalPreview");
    completeTrack(done, cameraAllowed, imrtc::MediaKind::Video, "camera", "local-cam-1",
                  cameraErrorCode);
  }

  void acquireMicrophone(imrtc::TrackCompletion done) override {
    calls.push_back("acquireMicrophone");
    completeTrack(done, micAllowed, imrtc::MediaKind::Audio, "microphone", "local-mic-1",
                  micErrorCode);
  }

  void acquireCamera(imrtc::TrackCompletion done) override {
    calls.push_back("acquireCamera");
    completeTrack(done, cameraAllowed, imrtc::MediaKind::Video, "camera", "local-cam-1",
                  cameraErrorCode);
  }

  void createPubOffer(imrtc::SdpCompletion done) override {
    calls.push_back("createPubOffer");
    complete([done]() {
      if (done) done(true, "v=0\r\npub-offer", 0);
    });
  }

  void applyPubAnswer(const std::string& sdp, imrtc::VoidCompletion done) override {
    calls.push_back("applyPubAnswer");
    appliedPubAnswers.push_back(sdp);
    complete([done]() {
      if (done) done(true, 0);
    });
  }

  void answerSubOffer(const std::string& sdp, imrtc::SdpCompletion done) override {
    calls.push_back("answerSubOffer");
    answeredSubOffers.push_back(sdp);
    complete([done]() {
      if (done) done(true, "v=0\r\nsub-answer", 0);
    });
  }

  void addRemoteCandidate(imrtc::PcRole pc, const imrtc::IceCandidate& candidate) override {
    remoteCandidates.push_back(std::string(imrtc::pcRoleName(pc)) + ":" + candidate.candidate);
  }

  void setMuted(const std::string& cid, bool isMuted) override {
    muted.push_back(cid + (isMuted ? ":muted" : ":live"));
  }

  void attachView(const std::string& trackId, void* nativeHandle) override {
    attachedViews.push_back(trackId + (nativeHandle == nullptr ? ":detach" : ":attach"));
  }

  void attachLocalView(void* nativeHandle) override {
    // 记成 "local:attach" / "local:detach"，与远端那条用同一个列表，
    // 顺序也就一并测到了。
    attachedViews.push_back(std::string("local") +
                            (nativeHandle == nullptr ? ":detach" : ":attach"));
  }

  void reset() override {
    ++resetCount;
    calls.push_back("reset");
  }

  void close() override {
    ++closeCount;
    calls.push_back("close");
  }

  void poll() override {
    while (!pending_.empty()) {
      std::function<void()> job = pending_.front();
      pending_.pop_front();
      job();
    }
  }

  /** emitLocalCandidate 模拟本端收集到一个候选。 */
  void emitLocalCandidate(imrtc::PcRole pc, const std::string& candidate) {
    if (events.onLocalCandidate) {
      events.onLocalCandidate(pc, imrtc::IceCandidate{candidate, "0", 0});
    }
  }
  /** emitPcState 模拟某条 PC 的状态变化。 */
  void emitPcState(imrtc::PcRole pc, imrtc::PcState state) {
    if (events.onPcState) events.onPcState(pc, state);
  }

  /** callCount 数某个方法被调了几次。 */
  int callCount(const std::string& name) const {
    int count = 0;
    for (const std::string& call : calls) {
      if (call == name) ++count;
    }
    return count;
  }

private:
  void complete(std::function<void()> job) {
    if (deferCompletions) {
      pending_.push_back(std::move(job));
      return;
    }
    job();
  }

  void completeTrack(const imrtc::TrackCompletion& done, bool allowed, imrtc::MediaKind kind,
                     const std::string& source, const std::string& cid, std::int32_t errorCode) {
    complete([done, allowed, kind, source, cid, errorCode]() {
      if (!done) return;
      if (!allowed) {
        done(false, imrtc::LocalTrack{}, errorCode);
        return;
      }
      done(true, imrtc::LocalTrack{cid, kind, source}, 0);
    });
  }

  std::deque<std::function<void()>> pending_;
};

}  // namespace imtest
