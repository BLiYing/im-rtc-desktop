#include "Language.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QSettings>
#include <QTranslator>

namespace language {
namespace {

Q_LOGGING_CATEGORY(lcLang, "imrtc.demo.language")

constexpr char kSettingsKey[] = "ui/language";

/** 两个翻译器全局各一份：装上去要能摘下来，所以得留着指针。 */
QTranslator* appTranslator = nullptr;
QTranslator* qtTranslator = nullptr;

QLocale localeFor(Choice choice) {
  switch (choice) {
    case Choice::ZhCN:
      return QLocale(QLocale::Chinese, QLocale::China);
    case Choice::English:
      return QLocale(QLocale::English, QLocale::UnitedStates);
    case Choice::System:
      break;
  }
  return QLocale::system();
}

void removeTranslator(QTranslator*& translator) {
  if (translator == nullptr) return;
  QCoreApplication::removeTranslator(translator);
  delete translator;
  translator = nullptr;
}

}  // namespace

Choice current() {
  QSettings settings;
  const int stored = settings.value(QLatin1String(kSettingsKey),
                                    static_cast<int>(Choice::System)).toInt();
  switch (stored) {
    case 1: return Choice::ZhCN;
    case 2: return Choice::English;
    default: return Choice::System;
  }
}

void apply(Choice choice) {
  QSettings settings;
  settings.setValue(QLatin1String(kSettingsKey), static_cast<int>(choice));

  removeTranslator(appTranslator);
  removeTranslator(qtTranslator);

  const QLocale locale = localeFor(choice);

  // 第 1 层：我们自己的文案。中文是源语言，没有 .qm，也不需要。
  if (locale.language() != QLocale::Chinese) {
    auto* translator = new QTranslator;
    if (translator->load(locale, QStringLiteral("imrtc_demo"), QStringLiteral("_"),
                         QStringLiteral(":/i18n"))) {
      QCoreApplication::installTranslator(translator);
      appTranslator = translator;
    } else {
      // 没有对应语言的 .qm 就退回源语言（中文），不是错误。
      qCInfo(lcLang, "没有 %s 的界面翻译，退回中文", qUtf8Printable(locale.name()));
      delete translator;
    }
  }

  // 第 2 层：Qt 内置控件。**这一层用系统 locale 也要装**——
  // 中文系统上不装它，输入框右键菜单会是 Cut / Copy / Paste。
  auto* builtin = new QTranslator;
  if (builtin->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                    QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
    QCoreApplication::installTranslator(builtin);
    qtTranslator = builtin;
  } else {
    // 英文本来就没有 qtbase_en.qm（英文是 Qt 的源语言），这不是故障。
    qCDebug(lcLang, "没有 qtbase 的 %s 翻译（英文属正常）", qUtf8Printable(locale.name()));
    delete builtin;
  }
}

QString displayName(Choice choice) {
  switch (choice) {
    case Choice::ZhCN: return QStringLiteral("简体中文");
    case Choice::English: return QStringLiteral("English");
    case Choice::System: break;
  }
  return QCoreApplication::translate("language", "跟随系统");
}

}  // namespace language
