#pragma once

/**
 * SettingsPage.h —— 设置屏（草图 §02-D）。
 *
 * iOS/Web 那边这一页放的是「Kit 的可配项」。桌面端没有 Kit，所以这里放
 * 三类东西：界面语言、这次连接的身份与端点、以及**当前构建的真实能力**
 * （哪些是真的、哪些还没有）。最后一块不是自谦，是防止集成方按 Demo 的
 * 外观推断「桌面端已经能通话了」。
 */

#include <QWidget>

#include "Language.h"

class QComboBox;
class QGroupBox;
class QLabel;

class SettingsPage : public QWidget {
  Q_OBJECT

public:
  explicit SettingsPage(QWidget* parent = nullptr);

  void setSession(const QString& uid, const QString& wsUrl, const QString& sessionId);

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();

  QGroupBox* uiGroup_ = nullptr;
  QLabel* languageLabel_ = nullptr;
  QComboBox* language_ = nullptr;

  QGroupBox* sessionGroup_ = nullptr;
  QLabel* uidLabel_ = nullptr;
  QLabel* uidValue_ = nullptr;
  QLabel* endpointLabel_ = nullptr;
  QLabel* endpointValue_ = nullptr;
  QLabel* deviceLabel_ = nullptr;
  QLabel* deviceValue_ = nullptr;
  QLabel* sessionLabel_ = nullptr;
  QLabel* sessionValue_ = nullptr;

  QGroupBox* buildGroup_ = nullptr;
  QLabel* buildText_ = nullptr;
};
