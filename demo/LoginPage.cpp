#include "LoginPage.h"

#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "EngineBridge.h"
#include "Theme.h"

namespace {

/** 上次填过的服务器与用户名，下次直接带出来——联调时每天要登好几十次。 */
constexpr char kServerKey[] = "login/server";
constexpr char kUserKey[] = "login/username";
constexpr char kNicknameKey[] = "login/nickname";

}  // namespace

LoginPage::LoginPage(QWidget* parent) : QWidget(parent) {
  QSettings settings;

  title_ = new QLabel(this);
  title_->setFont(theme::type::t1());

  subtitle_ = new QLabel(this);
  subtitle_->setFont(theme::type::b2());
  subtitle_->setEnabled(false);

  server_ = new QLineEdit(
      settings.value(QLatin1String(kServerKey), QStringLiteral("http://127.0.0.1:8787")).toString(),
      this);
  user_ = new QLineEdit(settings.value(QLatin1String(kUserKey)).toString(), this);
  nickname_ = new QLineEdit(settings.value(QLatin1String(kNicknameKey)).toString(), this);
  for (QLineEdit* field : {server_, user_, nickname_}) field->setMinimumWidth(280);

  serverLabel_ = new QLabel(this);
  userLabel_ = new QLabel(this);
  nicknameLabel_ = new QLabel(this);

  auto* form = new QFormLayout;
  form->setLabelAlignment(Qt::AlignLeft);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  form->addRow(serverLabel_, server_);
  form->addRow(userLabel_, user_);
  form->addRow(nicknameLabel_, nickname_);

  login_ = new QPushButton(this);
  login_->setDefault(true);
  login_->setMinimumHeight(36);

  error_ = new QLabel(this);
  error_->setWordWrap(true);
  error_->setVisible(false);
  {
    QPalette palette = error_->palette();
    palette.setColor(QPalette::WindowText, theme::call::danger());
    error_->setPalette(palette);
  }

  // ---- 集成方式分段 ----
  integrationLabel_ = new QLabel(this);
  integrationLabel_->setFont(theme::type::b2());
  kitOption_ = new QPushButton(this);
  kitOption_->setCheckable(true);
  kitOption_->setEnabled(false);  // 桌面端没有 Kit，见文件头注释
  engineOption_ = new QPushButton(this);
  engineOption_->setCheckable(true);
  engineOption_->setChecked(true);
  engineOption_->setEnabled(false);  // 只有一条路，禁用以免看起来还能切
  auto* segmented = new QHBoxLayout;
  segmented->setSpacing(0);
  segmented->addWidget(kitOption_);
  segmented->addWidget(engineOption_);
  segmented->addStretch();

  footnote_ = new QLabel(this);
  footnote_->setFont(theme::type::c1());
  footnote_->setWordWrap(true);
  footnote_->setEnabled(false);

  mediaNotice_ = new QLabel(this);
  mediaNotice_->setFont(theme::type::c1());
  mediaNotice_->setWordWrap(true);
  {
    QPalette palette = mediaNotice_->palette();
    palette.setColor(QPalette::WindowText, theme::call::warn());
    mediaNotice_->setPalette(palette);
  }

  auto* column = new QVBoxLayout;
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(10);
  column->addWidget(title_);
  column->addWidget(subtitle_);
  column->addSpacing(12);
  column->addLayout(form);
  column->addWidget(error_);
  column->addSpacing(4);
  column->addWidget(login_);
  column->addSpacing(12);
  column->addWidget(integrationLabel_);
  column->addLayout(segmented);
  column->addSpacing(8);
  column->addWidget(footnote_);
  column->addWidget(mediaNotice_);

  auto* centered = new QVBoxLayout(this);
  centered->addStretch();
  auto* row = new QHBoxLayout;
  row->addStretch();
  auto* card = new QWidget(this);
  card->setLayout(column);
  // 420 而不是 360：服务器那一栏要放得下 `http://127.0.0.1:8787`，
  // 360 时会被截成 `1ttp://…`。
  card->setFixedWidth(420);
  row->addWidget(card);
  row->addStretch();
  centered->addLayout(row);
  centered->addStretch();

  connect(login_, &QPushButton::clicked, this, [this] {
    QSettings settings;
    settings.setValue(QLatin1String(kServerKey), server_->text().trimmed());
    settings.setValue(QLatin1String(kUserKey), user_->text().trimmed());
    settings.setValue(QLatin1String(kNicknameKey), nickname_->text().trimmed());
    emit loginRequested();
  });
  connect(user_, &QLineEdit::returnPressed, login_, &QPushButton::click);

  retranslateUi();
}

void LoginPage::prefill(const QString& httpBase, const QString& username) {
  if (!httpBase.isEmpty()) server_->setText(httpBase);
  if (!username.isEmpty()) user_->setText(username);
}

void LoginPage::submit() { login_->click(); }

QString LoginPage::httpBase() const { return server_->text().trimmed(); }
QString LoginPage::username() const { return user_->text().trimmed(); }
QString LoginPage::nickname() const { return nickname_->text().trimmed(); }

void LoginPage::setBusy(bool busy) {
  login_->setEnabled(!busy);
  server_->setEnabled(!busy);
  user_->setEnabled(!busy);
  nickname_->setEnabled(!busy);
  login_->setText(busy ? tr("正在登录…") : tr("登录"));
}

void LoginPage::showError(const QString& message) {
  error_->setText(message);
  error_->setVisible(!message.isEmpty());
}

void LoginPage::changeEvent(QEvent* event) {
  // 切语言时 Qt 会给每个 widget 发这个事件，在这里重刷文案。
  if (event->type() == QEvent::LanguageChange) retranslateUi();
  QWidget::changeEvent(event);
}

void LoginPage::retranslateUi() {
  title_->setText(tr("im-rtc Demo"));
  subtitle_->setText(tr("SDK %1 · 经 C ABI 调引擎").arg(EngineBridge::versionString()));
  serverLabel_->setText(tr("服务器"));
  userLabel_->setText(tr("用户 ID"));
  nicknameLabel_->setText(tr("昵称"));
  nickname_->setPlaceholderText(tr("选填"));
  user_->setPlaceholderText(tr("例如 alice"));
  server_->setPlaceholderText(tr("http://127.0.0.1:8787"));
  login_->setText(tr("登录"));
  integrationLabel_->setText(tr("集成方式"));
  kitOption_->setText(tr("Kit（整套 UI）"));
  kitOption_->setToolTip(tr("桌面端不提供 UI Kit：交付的是引擎 + C ABI + C++ 包装头。"));
  engineOption_->setText(tr("Engine（自画 UI）"));
  footnote_->setText(tr("Demo 使用服务端内置的免密登录（仅开发构建可用）。\n"
                        "生产请改用你自己的 /v1/tokens 换票。\n"
                        "桌面端只有「自画 UI」一条路——本 Demo 就是那条路的参考实现。"));
  mediaNotice_->setText(tr("当前为纯信令模式：能拨号、能进房、能收到全部状态回调，"
                           "但没有声音和画面（媒体已决定推迟，等 Apple Silicon 或 Windows 机器）。"));
}
