#pragma once

/**
 * HistoryPage.h —— 通话记录屏（草图 §02-C / §06-Q 右栏）。
 *
 * 数据来自 SDK 的 `fetchCallHistory`（服务端 `GET /v1/calls`），游标翻页：谁、方向、媒体、原因、时长。
 * 滚到底自动加载下一页，也可以点「加载更多」；点「刷新」重拉首页。
 * 这一页是**给集成方看的证据**——「你们给不给通话记录的数据」，答案在这里。
 */

#include <QString>
#include <QWidget>

class CallHistory;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class HistoryPage : public QWidget {
  Q_OBJECT

public:
  explicit HistoryPage(CallHistory* history, QWidget* parent = nullptr);

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();
  void rebuild();
  void updateFooter();
  static QString emptyText();
  QWidget* makeRow(const struct CallRecord& record);

  CallHistory* history_ = nullptr;
  QLabel* title_ = nullptr;
  QPushButton* refresh_ = nullptr;
  QPushButton* more_ = nullptr;
  QLabel* error_ = nullptr;
  QLabel* empty_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  QVBoxLayout* rows_ = nullptr;
};
