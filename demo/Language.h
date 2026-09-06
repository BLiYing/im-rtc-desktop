#pragma once

/**
 * Language.h —— 界面语言切换。
 *
 * 两层翻译，缺一不可：
 *   1. **我们自己的文案**——`tr()` 提取出来的，装进 :/i18n/imrtc_demo_en.qm。
 *   2. **Qt 内置控件的文案**——QMessageBox 的 OK/Cancel、输入框右键菜单的
 *      剪切/复制/粘贴、QFileDialog 的按钮。这些是 Qt 源码里硬编码的英文，
 *      `tr()` 覆盖不到，只能装 Qt 自带的 qtbase_<locale>.qm（qttranslations 包）。
 *
 * 只做第 1 层的话，中文界面上的右键菜单会是英文的——这个漏洞很常见，
 * 因为开发机通常是英文系统，压根看不出来。
 *
 * 源语言是**中文**：代码里 tr() 写的就是中文，所以「中文」= 不装任何翻译器。
 */

#include <QString>

class QTranslator;

namespace language {

enum class Choice {
  System = 0,   ///< 跟随系统 locale
  ZhCN = 1,     ///< 简体中文（源语言，不装翻译器）
  English = 2
};

/** 从 QSettings 读上次的选择，默认跟随系统。 */
Choice current();

/**
 * 应用并持久化。会给所有 widget 发 LanguageChange 事件，
 * 各页在 `changeEvent` 里重刷文案。
 */
void apply(Choice choice);

/** 给设置页显示用的名字。 */
QString displayName(Choice choice);

}  // namespace language
