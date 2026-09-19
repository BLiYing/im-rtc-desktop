#include "HistoryPage.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include "CallHistory.h"
#include "CallStrings.h"
#include "Theme.h"

HistoryPage::HistoryPage(CallHistory* history, QWidget* parent)
    : QWidget(parent), history_(history) {
  title_ = new QLabel(this);
  title_->setFont(theme::type::t3());
  refresh_ = new QPushButton(this);

  auto* header = new QHBoxLayout;
  header->addWidget(title_);
  header->addStretch();
  header->addWidget(refresh_);

  empty_ = new QLabel(this);
  empty_->setAlignment(Qt::AlignCenter);
  empty_->setWordWrap(true);
  empty_->setEnabled(false);

  // 一张圆角卡片，行与行之间细分隔线（与 Android / iOS / Web 的记录页同一个样式）。
  auto* card = new QFrame;
  card->setObjectName(QStringLiteral("historyCard"));
  card->setStyleSheet(QStringLiteral("#historyCard { background: palette(base); border-radius: 12px; }"));
  rows_ = new QVBoxLayout(card);
  rows_->setContentsMargins(0, 0, 0, 0);
  rows_->setSpacing(0);
  rows_->addStretch();

  auto* container = new QWidget;
  auto* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->addWidget(card);
  containerLayout->addStretch();

  scroll_ = new QScrollArea(this);
  scroll_->setWidget(container);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);

  more_ = new QPushButton(this);
  error_ = new QLabel(this);
  error_->setWordWrap(true);
  error_->setEnabled(false);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(8);
  layout->addLayout(header);
  layout->addWidget(empty_);
  layout->addWidget(scroll_, 1);
  layout->addWidget(error_);
  layout->addWidget(more_);

  connect(refresh_, &QPushButton::clicked, history_, &CallHistory::refresh);
  connect(more_, &QPushButton::clicked, history_, &CallHistory::loadMore);
  // 滚到底自动加下一页；没有更多 / 已经在拉时 loadMore 自己是空操作。
  connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
    if (value >= scroll_->verticalScrollBar()->maximum() - 40) history_->loadMore();
  });
  connect(history_, &CallHistory::changed, this, &HistoryPage::rebuild);

  retranslateUi();
  rebuild();
}

QWidget* HistoryPage::makeRow(const CallRecord& record) {
  auto* row = new QWidget;
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(0);

  // 左：媒体图标（👥 群 / 📹 视频 / 📞 语音），36px 宽、20px 字——与 Android 同一个版式。
  auto* icon = new QLabel(record.isGroup ? QStringLiteral("👥")
                                         : (record.mediaType == QLatin1String("video")
                                                ? QStringLiteral("📹")
                                                : QStringLiteral("📞")),
                          row);
  QFont iconFont = icon->font();
  iconFont.setPixelSize(20);
  icon->setFont(iconFont);
  icon->setFixedWidth(36);
  layout->addWidget(icon);

  // 中：第一行对方（16px 粗体，未接来电红字），第二行「来电/呼出 · 结果」（13px 灰）。
  auto* name = new QLabel(callstrings::recordTitle(record), row);
  QFont nameFont = name->font();
  nameFont.setPixelSize(16);
  nameFont.setBold(true);
  name->setFont(nameFont);
  auto* summary = new QLabel(callstrings::recordSummary(record), row);
  QFont smallFont = summary->font();
  smallFont.setPixelSize(13);
  summary->setFont(smallFont);
  summary->setEnabled(false);

  // 未接来电用 danger 色标出来——这是记录页唯一需要「一眼看到」的东西。
  if (!record.connected && !record.outgoing) {
    QPalette palette = name->palette();
    palette.setColor(QPalette::WindowText, theme::call::danger());
    name->setPalette(palette);
  }

  auto* text = new QVBoxLayout;
  text->setSpacing(2);
  text->addWidget(name);
  text->addWidget(summary);
  layout->addLayout(text, 1);

  // 右：时间（13px 灰、靠右、单行）。
  auto* when = new QLabel(callstrings::recordTimestamp(record), row);
  when->setFont(smallFont);
  when->setEnabled(false);
  when->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
  updateFooter();

  int index = 0;
  for (const CallRecord& record : records) {
    if (index > 0) {
      auto* line = new QFrame;
      line->setFixedHeight(1);
      line->setStyleSheet(QStringLiteral("background: palette(midlight);"));
      rows_->insertWidget(index++, line);
    }
    rows_->insertWidget(index++, makeRow(record));
  }
}

void HistoryPage::updateFooter() {
  more_->setVisible(history_->hasMore());
  more_->setEnabled(!history_->loading());
  more_->setText(history_->loading() ? tr("加载中…") : tr("加载更多"));
  refresh_->setEnabled(!history_->loading());
  error_->setVisible(!history_->error().isEmpty());
  error_->setText(tr("加载失败：%1，点「刷新」重试").arg(history_->error()));
  empty_->setText(history_->loading() && history_->records().isEmpty() ? tr("加载中…") : emptyText());
}

QString HistoryPage::emptyText() {
  return tr("还没有通话记录。\n\n这一页从服务端拉（SDK 的 fetchCallHistory）：\n"
            "对方、方向、媒体类型、结束原因、时长。");
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
  refresh_->setText(tr("刷新"));
  updateFooter();
}
