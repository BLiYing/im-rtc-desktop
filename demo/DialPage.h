#pragma once

/**
 * DialPage.h —— 拨号屏（草图 §02-B / §06-Q 左栏）。
 *
 * 三块对应三种玩法：1v1 / 群通话（≤8）/ 加入会议房间。顶部身份卡显示
 * `onConnected` 之后的连接态——那是「引擎活着」的唯一可见证据。
 */

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class AvatarWidget;

class DialPage : public QWidget {
  Q_OBJECT

public:
  explicit DialPage(QWidget* parent = nullptr);

  void setIdentity(const QString& uid, const QString& nickname);

  /** 建好会议房后把房号填回输入框。 */
  void setRoomId(const QString& roomId);

  /** 连接态那一行：已连接 / 正在重连… / 连接已断开。 */
  void setConnectionText(const QString& text, bool healthy);

  /** 通话中把拨号入口禁掉——引擎会拒（2005），但让按钮先灰更好懂。 */
  void setDialingEnabled(bool enabled);

signals:
  void audioCallRequested(const QString& calleeId);
  void videoCallRequested(const QString& calleeId);
  void groupCallRequested(const QStringList& calleeIds, const QString& mediaType);
  void joinRoomRequested(const QString& roomId);
  void createRoomRequested();
  void logoutRequested();
  void settingsRequested();

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();
  QStringList members() const;

  AvatarWidget* avatar_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* connection_ = nullptr;
  QPushButton* logout_ = nullptr;
  QPushButton* settings_ = nullptr;

  QLabel* soloTitle_ = nullptr;
  QLabel* peerLabel_ = nullptr;
  QLineEdit* peer_ = nullptr;
  QPushButton* audio_ = nullptr;
  QPushButton* video_ = nullptr;

  QLabel* groupTitle_ = nullptr;
  QLabel* membersLabel_ = nullptr;
  QLineEdit* members_ = nullptr;
  QPushButton* groupAudio_ = nullptr;
  QPushButton* groupVideo_ = nullptr;

  QLabel* roomTitle_ = nullptr;
  QLabel* roomLabel_ = nullptr;
  QLineEdit* room_ = nullptr;
  QPushButton* join_ = nullptr;
  QPushButton* createRoom_ = nullptr;
};
