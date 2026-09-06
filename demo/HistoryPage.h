#pragma once

/**
 * HistoryPage.h —— 通话记录屏（草图 §02-C / §06-Q 右栏）。
 *
 * 每一行的每一个字都来自 `onCallEnd`：谁、方向、媒体、原因、时长。
 * 这一页是**给集成方看的证据**——「你们给不给通话记录的数据」，答案在这里。
 */

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
  QWidget* makeRow(const struct CallRecord& record);

  CallHistory* history_ = nullptr;
  QLabel* title_ = nullptr;
  QPushButton* clear_ = nullptr;
  QLabel* empty_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  QVBoxLayout* rows_ = nullptr;
};
