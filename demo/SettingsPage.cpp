#include "SettingsPage.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

#include "EngineBridge.h"
#include "Theme.h"

namespace {

/** 值那一列都可以选中复制——联调时要把 session id 贴给服务端同学。 */
QLabel* valueLabel(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setFont(theme::type::b2());
  return label;
}

}  // namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
  // ---- 界面 ----
  uiGroup_ = new QGroupBox(this);
  languageLabel_ = new QLabel(uiGroup_);
  language_ = new QComboBox(uiGroup_);
  language_->addItem(QString(), static_cast<int>(language::Choice::System));
  language_->addItem(QString(), static_cast<int>(language::Choice::ZhCN));
  language_->addItem(QString(), static_cast<int>(language::Choice::English));
  language_->setCurrentIndex(language_->findData(static_cast<int>(language::current())));

  auto* uiForm = new QFormLayout(uiGroup_);
  uiForm->addRow(languageLabel_, language_);

  connect(language_, &QComboBox::currentIndexChanged, this, [this](int index) {
    const auto choice = static_cast<language::Choice>(language_->itemData(index).toInt());
    language::apply(choice);  // 会给所有 widget 发 LanguageChange
  });

  // ---- 本次会话 ----
  sessionGroup_ = new QGroupBox(this);
  uidLabel_ = new QLabel(sessionGroup_);
  uidValue_ = valueLabel(sessionGroup_);
  endpointLabel_ = new QLabel(sessionGroup_);
  endpointValue_ = valueLabel(sessionGroup_);
  deviceLabel_ = new QLabel(sessionGroup_);
  deviceValue_ = valueLabel(sessionGroup_);
  deviceValue_->setText(EngineBridge::deviceId());
  sessionLabel_ = new QLabel(sessionGroup_);
  sessionValue_ = valueLabel(sessionGroup_);

  auto* sessionForm = new QFormLayout(sessionGroup_);
  sessionForm->addRow(uidLabel_, uidValue_);
  sessionForm->addRow(endpointLabel_, endpointValue_);
  sessionForm->addRow(deviceLabel_, deviceValue_);
  sessionForm->addRow(sessionLabel_, sessionValue_);

  // ---- 这个构建能做什么 ----
  buildGroup_ = new QGroupBox(this);
  buildText_ = new QLabel(buildGroup_);
  buildText_->setWordWrap(true);
  buildText_->setFont(theme::type::b2());
  auto* buildLayout = new QVBoxLayout(buildGroup_);
  buildLayout->addWidget(buildText_);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(12);
  layout->addWidget(uiGroup_);
  layout->addWidget(sessionGroup_);
  layout->addWidget(buildGroup_);
  layout->addStretch();

  retranslateUi();
}

void SettingsPage::setSession(const QString& uid, const QString& wsUrl,
                              const QString& sessionId) {
  uidValue_->setText(uid);
  endpointValue_->setText(wsUrl);
  sessionValue_->setText(sessionId.isEmpty() ? tr("（未连接）") : sessionId);
}

void SettingsPage::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslateUi();
  QWidget::changeEvent(event);
}

void SettingsPage::retranslateUi() {
  uiGroup_->setTitle(tr("界面"));
  languageLabel_->setText(tr("语言"));
  for (int i = 0; i < language_->count(); ++i) {
    language_->setItemText(
        i, language::displayName(static_cast<language::Choice>(language_->itemData(i).toInt())));
  }

  sessionGroup_->setTitle(tr("本次会话"));
  uidLabel_->setText(tr("用户 ID"));
  endpointLabel_->setText(tr("信令端点"));
  deviceLabel_->setText(tr("设备 ID"));
  sessionLabel_->setText(tr("会话 ID"));

  buildGroup_->setTitle(tr("这个构建能做什么"));
  buildText_->setText(
      tr("SDK %1，经 C ABI 调用（与集成方拿到的是同一个 .dylib / .dll + 一个 C 头）。\n\n"
         "✅ 已经是真的：登录、心跳、断线重连、拨号、来电、接听/拒接/取消/挂断、"
         "群通话成员事件、加入与离开房间、通话记录。\n\n"
         "⬜ 还没有：声音与画面。媒体面（MediaAdapter / MediaPlane）已经接好并测全，"
         "但真正干活的 WebRTCAdapter 还没写——libwebrtc 没有 macOS x86_64 的预编译包，"
         "已决定等 Apple Silicon 或 Windows 机器。\n\n"
         "⬜ 也还没有：设备枚举与热插拔、共享屏幕、Windows 侧的任何验证。")
          .arg(EngineBridge::versionString()));
}
