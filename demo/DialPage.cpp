#include "DialPage.h"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "Avatar.h"
#include "Theme.h"

namespace {

/** 群通话上限，与协议一致（超了服务端回 1406）。 */
constexpr int kMaxGroupMembers = 8;

QFrame* separator(QWidget* parent) {
  auto* line = new QFrame(parent);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  return line;
}

}  // namespace

DialPage::DialPage(QWidget* parent) : QWidget(parent) {
  // ---- 身份卡 ----
  avatar_ = new AvatarWidget(40, this);
  name_ = new QLabel(this);
  name_->setFont(theme::type::t3());
  connection_ = new QLabel(this);
  connection_->setFont(theme::type::b2());
  logout_ = new QPushButton(this);
  settings_ = new QPushButton(this);

  auto* identityText = new QVBoxLayout;
  identityText->setSpacing(2);
  identityText->addWidget(name_);
  identityText->addWidget(connection_);

  auto* identity = new QHBoxLayout;
  identity->setSpacing(10);
  identity->addWidget(avatar_);
  identity->addLayout(identityText);
  identity->addStretch();
  identity->addWidget(settings_);
  identity->addWidget(logout_);

  // ---- 单人通话 ----
  soloTitle_ = new QLabel(this);
  soloTitle_->setFont(theme::type::t3());
  peerLabel_ = new QLabel(this);
  peer_ = new QLineEdit(this);
  audio_ = new QPushButton(this);
  video_ = new QPushButton(this);

  auto* soloButtons = new QHBoxLayout;
  soloButtons->addWidget(audio_);
  soloButtons->addWidget(video_);
  soloButtons->addStretch();

  // ---- 多人通话 ----
  groupTitle_ = new QLabel(this);
  groupTitle_->setFont(theme::type::t3());
  membersLabel_ = new QLabel(this);
  members_ = new QLineEdit(this);
  groupAudio_ = new QPushButton(this);
  groupVideo_ = new QPushButton(this);

  auto* groupButtons = new QHBoxLayout;
  groupButtons->addWidget(groupAudio_);
  groupButtons->addWidget(groupVideo_);
  groupButtons->addStretch();

  // ---- 会议房间 ----
  roomTitle_ = new QLabel(this);
  roomTitle_->setFont(theme::type::t3());
  roomLabel_ = new QLabel(this);
  room_ = new QLineEdit(this);
  join_ = new QPushButton(this);
  createRoom_ = new QPushButton(this);

  auto* roomButtons = new QHBoxLayout;
  roomButtons->addWidget(join_);
  roomButtons->addWidget(createRoom_);
  roomButtons->addStretch();

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(8);
  layout->addLayout(identity);
  layout->addWidget(separator(this));
  layout->addSpacing(6);
  layout->addWidget(soloTitle_);
  layout->addWidget(peerLabel_);
  layout->addWidget(peer_);
  layout->addLayout(soloButtons);
  layout->addSpacing(10);
  layout->addWidget(groupTitle_);
  layout->addWidget(membersLabel_);
  layout->addWidget(members_);
  layout->addLayout(groupButtons);
  layout->addSpacing(10);
  layout->addWidget(roomTitle_);
  layout->addWidget(roomLabel_);
  layout->addWidget(room_);
  layout->addLayout(roomButtons);
  layout->addStretch();

  connect(audio_, &QPushButton::clicked, this,
          [this] { emit audioCallRequested(peer_->text().trimmed()); });
  connect(video_, &QPushButton::clicked, this,
          [this] { emit videoCallRequested(peer_->text().trimmed()); });
  connect(peer_, &QLineEdit::returnPressed, audio_, &QPushButton::click);

  const auto startGroup = [this](const QString& mediaType) {
    const QStringList ids = members();
    if (ids.size() > kMaxGroupMembers) {
      // 上限拦在本地：服务端也会拒（1406），但让人对着输入框改比看错误码强。
      QMessageBox::information(this, tr("人数超了"),
                               tr("群通话最多 %1 人，现在填了 %2 个。")
                                   .arg(kMaxGroupMembers)
                                   .arg(ids.size()));
      return;
    }
    emit groupCallRequested(ids, mediaType);
  };
  connect(groupAudio_, &QPushButton::clicked, this,
          [startGroup] { startGroup(QStringLiteral("audio")); });
  connect(groupVideo_, &QPushButton::clicked, this,
          [startGroup] { startGroup(QStringLiteral("video")); });

  connect(join_, &QPushButton::clicked, this,
          [this] { emit joinRoomRequested(room_->text().trimmed()); });
  connect(room_, &QLineEdit::returnPressed, join_, &QPushButton::click);
  connect(createRoom_, &QPushButton::clicked, this, &DialPage::createRoomRequested);
  connect(logout_, &QPushButton::clicked, this, &DialPage::logoutRequested);
  connect(settings_, &QPushButton::clicked, this, &DialPage::settingsRequested);

  retranslateUi();
}

void DialPage::setRoomId(const QString& roomId) { room_->setText(roomId); }

QStringList DialPage::members() const {
  // 逗号、顿号、空格都当分隔符——联调时手输，别为难人。
  const QStringList raw =
      members_->text().split(QRegularExpression(QStringLiteral("[,，、\\s]+")), Qt::SkipEmptyParts);
  QStringList ids;
  for (const QString& item : raw) {
    const QString trimmed = item.trimmed();
    if (!trimmed.isEmpty() && !ids.contains(trimmed)) ids << trimmed;
  }
  return ids;
}

void DialPage::setIdentity(const QString& uid, const QString& nickname) {
  avatar_->setIdentity(uid, nickname.isEmpty() ? uid : nickname);
  name_->setText(nickname.isEmpty() ? uid : QStringLiteral("%1（%2）").arg(nickname, uid));
}

void DialPage::setConnectionText(const QString& text, bool healthy) {
  connection_->setText(text);
  QPalette palette = connection_->palette();
  palette.setColor(QPalette::WindowText,
                   healthy ? palette.color(QPalette::PlaceholderText) : theme::call::warn());
  connection_->setPalette(palette);
}

void DialPage::setDialingEnabled(bool enabled) {
  for (QPushButton* button : {audio_, video_, groupAudio_, groupVideo_, join_, createRoom_}) {
    button->setEnabled(enabled);
  }
}

void DialPage::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslateUi();
  QWidget::changeEvent(event);
}

void DialPage::retranslateUi() {
  logout_->setText(tr("退出"));
  settings_->setText(tr("设置"));
  soloTitle_->setText(tr("单人通话"));
  peerLabel_->setText(tr("对方 ID"));
  peer_->setPlaceholderText(tr("例如 bob"));
  audio_->setText(tr("语音通话"));
  video_->setText(tr("视频通话"));
  groupTitle_->setText(tr("多人通话（最多 %1 人）").arg(kMaxGroupMembers));
  membersLabel_->setText(tr("成员"));
  members_->setPlaceholderText(tr("bob、carol、dave"));
  groupAudio_->setText(tr("发起群语音"));
  groupVideo_->setText(tr("发起群视频"));
  roomTitle_->setText(tr("会议房间"));
  roomLabel_->setText(tr("房间号"));
  room_->setPlaceholderText(tr("8827-1190"));
  join_->setText(tr("加入房间"));
  createRoom_->setText(tr("新建会议房"));
}
