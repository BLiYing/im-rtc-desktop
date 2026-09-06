#pragma once

/**
 * VideoTile.h —— 九宫格里的一格（UI_SPEC §06「格子（VideoTile）」）。
 *
 * 现在**永远画不出画面**：媒体那一刀还没落地（见 current_task 已知坑第一条）。
 * 但格子的全部语义都在这里并且是真的——谁在说话（绿色内描边）、谁静音了
 * （右上角标）、谁还在振铃（「呼叫中…」）、谁网络差。这些都由回调驱动，
 * 不是摆设。等 WebRTCAdapter 接上，只需要给这个类加一个「贴画面」的入口。
 *
 * 绿色（#3DDC84）在通话页只有两个含义：接听、正在说话。
 * **不许拿它当「已连接」的状态点**——九宫格里全靠它辨认谁在说话。
 */

#include <QString>
#include <QWidget>

class VideoTile : public QWidget {
  Q_OBJECT

public:
  enum class State {
    Ringing,   ///< 还在振铃：「呼叫中…」
    Present,   ///< 已在通话里
    Left       ///< 已离开（保留一格灰着，比让格子跳掉好读）
  };

  explicit VideoTile(QWidget* parent = nullptr);

  void setIdentity(const QString& uid, const QString& displayName);
  void setState(State state);
  void setSpeaking(bool speaking);
  void setMuted(bool muted);
  /** 0 = unknown，1~2 视为弱网（§05 net-bars 三档）。 */
  void setQualityLevel(int level);
  void setSelf(bool self);

  QString uid() const { return uid_; }

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QString uid_;
  QString displayName_;
  State state_ = State::Present;
  bool speaking_ = false;
  bool muted_ = false;
  bool self_ = false;
  int quality_ = 0;
};
