#pragma once

/**
 * LoginPage.h —— 登录屏（草图 §02-A / §06-P）。
 *
 * 三个字段就能跑起来：服务器 / 用户 ID / 昵称。默认走服务端内置的免密登录
 * （`-demo-login`，只在开发构建可用），生产要换成宿主自己的 `/v1/tokens`。
 *
 * 稿子底部那个「Kit / Engine」分段在**桌面端只有一半**：桌面交付的是
 * engine + C ABI + C++ 包装头，**没有 UI Kit**。所以 Kit 那一段是禁用的，
 * 并明说原因——比画一个点不动的假开关诚实。
 */

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class LoginPage : public QWidget {
  Q_OBJECT

public:
  explicit LoginPage(QWidget* parent = nullptr);

  QString httpBase() const;
  QString username() const;
  QString nickname() const;

  /** 联调用：预填并直接提交，见 MainWindow::autoLogin。 */
  void prefill(const QString& httpBase, const QString& username);
  void submit();

  /** 登录进行中时禁用输入，失败后恢复。 */
  void setBusy(bool busy);
  void showError(const QString& message);

signals:
  void loginRequested();

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();

  QLabel* title_ = nullptr;
  QLabel* subtitle_ = nullptr;
  QLabel* serverLabel_ = nullptr;
  QLabel* userLabel_ = nullptr;
  QLabel* nicknameLabel_ = nullptr;
  QLabel* integrationLabel_ = nullptr;
  QLabel* footnote_ = nullptr;
  QLabel* mediaNotice_ = nullptr;
  QLabel* error_ = nullptr;
  QLineEdit* server_ = nullptr;
  QLineEdit* user_ = nullptr;
  QLineEdit* nickname_ = nullptr;
  QPushButton* kitOption_ = nullptr;
  QPushButton* engineOption_ = nullptr;
  QPushButton* login_ = nullptr;
};
