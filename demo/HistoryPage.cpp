#include "HistoryPage.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "Avatar.h"
#include "CallHistory.h"
#include "CallStrings.h"
#include "Icons.h"
#include "Theme.h"

HistoryPage::HistoryPage(CallHistory* history, QWidget* parent)
    : QWidget(parent), history_(history) {
  title_ = new QLabel(this);
  title_->setFont(theme::type::t3());
  clear_ = new QPushButton(this);

  auto* header = new QHBoxLayout;
  header->addWidget(title_);
  header->addStretch();
  header->addWidget(clear_);

  empty_ = new QLabel(this);
  empty_->setAlignment(Qt::AlignCenter);
  empty_->setWordWrap(true);
  empty_->setEnabled(false);

  auto* container = new QWidget;
  rows_ = new QVBoxLayout(container);
  rows_->setContentsMargins(0, 0, 0, 0);
  rows_->setSpacing(0);
  rows_->addStretch();

  scroll_ = new QScrollArea(this);
  scroll_->setWidget(container);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(8);
  layout->addLayout(header);
  layout->addWidget(empty_);
  layout->addWidget(scroll_, 1);

  connect(clear_, &QPushButton::clicked, this, [this] {
    const auto answer = QMessageBox::question(this, tr("清空通话记录"),
                                              tr("清空之后不可恢复，确定吗？"));
    if (answer == QMessageBox::Yes) history_->clear();
  });
  connect(history_, &CallHistory::changed, this, &HistoryPage::rebuild);

  retranslateUi();
  rebuild();
}

QWidget* HistoryPage::makeRow(const CallRecord& record) {
  auto* row = new QWidget;
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 8, 0, 8);
  layout->setSpacing(10);

  const QString who = record.isGroup ? tr("群通话") : record.peer;
  auto* avatar = new AvatarWidget(34, row);
  avatar->setIdentity(record.isGroup ? record.callId : record.peer, who);
  layout->addWidget(avatar);

  auto* name = new QLabel(who, row);
  name->setFont(theme::type::b1());
  auto* summary = new QLabel(callstrings::recordSummary(record), row);
  summary->setFont(theme::type::b2());
  summary->setEnabled(false);

  auto* text = new QVBoxLayout;
  text->setSpacing(2);
  text->addWidget(name);
  text->addWidget(summary);
  layout->addLayout(text, 1);

  // 未接来电用 danger 色标出来——这是记录页唯一需要「一眼看到」的东西。
  if (!record.connected && !record.outgoing) {
    QPalette palette = name->palette();
    palette.setColor(QPalette::WindowText, theme::call::danger());
    name->setPalette(palette);
  }

  auto* icon = new QLabel(row);
  icon->setPixmap(icons::pixmap(
      record.mediaType == QLatin1String("video") ? icons::Name::Video : icons::Name::Phone, 16,
      palette().color(QPalette::PlaceholderText), devicePixelRatioF()));
  layout->addWidget(icon);

  auto* when = new QLabel(callstrings::recordTimestamp(record), row);
  when->setFont(theme::type::c1());
  when->setEnabled(false);
  layout->addWidget(when);

  return row;
}

void HistoryPage::rebuild() {
  // 先把旧行拆掉（最后一项是 stretch，留着）。
  while (rows_->count() > 1) {
    QLayoutItem* item = rows_->takeAt(0);
    if (item->widget() != nullptr) item->widget()->deleteLater();
    delete item;
  }

  const QList<CallRecord>& records = history_->records();
  empty_->setVisible(records.isEmpty());
  scroll_->setVisible(!records.isEmpty());

  int index = 0;
  for (const CallRecord& record : records) {
    rows_->insertWidget(index++, makeRow(record));
  }
}

void HistoryPage::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
    rebuild();  // 每一行的摘要也是翻译过的，得重建
  }
  QWidget::changeEvent(event);
}

void HistoryPage::retranslateUi() {
  title_->setText(tr("通话记录"));
  clear_->setText(tr("清空"));
  empty_->setText(tr("还没有通话记录。\n\n这一页的每一行都由 onCallEnd 一个回调拼出来：\n"
                     "对方、方向、媒体类型、结束原因、时长。"));
}
