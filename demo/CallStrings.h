#pragma once

/**
 * CallStrings.h —— 文案。**唯一来源**是设计稿的「Kit 文案定稿」那张表
 * （RTC_CALL_UX_SKETCH.html §09），两端同一份。
 *
 * 单独一个文件的理由：同一条 reason 会在三处出现——通话页的结束语、
 * 通话记录的摘要、来电 toast。分散写必然漂，漂了就变成「iOS 说『对方忙线中』、
 * 桌面说『对方正忙』」这种没人发现得了的不一致。
 *
 * 这里全部走 `tr()`，所以它们也是翻译提取的入口。
 */

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QTimeZone>
#include <functional>

struct CallRecord;

namespace callstrings {

/** mm:ss，超一小时用 h:mm:ss（文案表「接通 hangup」那一行）。 */
QString duration(qint64 seconds);

/**
 * 主叫侧看到的结束语：已取消 / 对方已拒绝 / 无人接听 / 对方忙线中 / 对方不在线…
 * 未知 reason 由引擎折成 "error"，这里兜底成「通话已结束」。
 */
QString callerEndText(const QString& reason);

/** 记录行第一行：1v1 是对方 uid（不知道就「（未知）」），群通话是「群通话 · N 人」（N 已含主叫）。 */
QString recordTitle(const CallRecord& record);

/** 记录行第二行（不含时间）：呼出 · 03:21 / 来电 · 12:40 / 未接来电 · 视频 / 呼出 · 已取消。群通话同一套。 */
QString recordSummary(const CallRecord& record);

/**
 * 来电邀请语：邀请你视频通话 / 邀请你语音通话 / 邀请你加入群通话。
 * 横幅与来电浮层共用这一句，与 Web 的 `incomingInviteText` 同一张表。
 */
QString incomingInviteText(bool isVideo, bool isGroup);

/**
 * 记录行的时间文案（四端统一）：按**发起时间**、`zone` 时区的自然日判断——
 * 今天 `HH:mm`；昨天 `昨天 HH:mm`；今年更早 `M月d日 HH:mm`；往年 `yyyy年M月d日 HH:mm`。
 * 记录时间不早于 `nowMs`（时钟偏差）按今天处理。时分由 `hourMinute` 出（生产用系统短时间格式，
 * 测试传固定格式），本函数只管日期档位。纯函数。
 */
QString callTime(qint64 startedAtMs, qint64 nowMs, const QTimeZone& zone,
                 const std::function<QString(const QDateTime&)>& hourMinute);

/** 记录行右侧的时间列：`callTime` + 本地时区 + 系统 locale 的短时间格式。 */
QString recordTimestamp(const CallRecord& record);

}  // namespace callstrings
