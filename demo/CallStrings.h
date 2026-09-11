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
#include <QString>

struct CallRecord;

namespace callstrings {

/** mm:ss，超一小时用 h:mm:ss（文案表「接通 hangup」那一行）。 */
QString duration(qint64 seconds);

/**
 * 主叫侧看到的结束语：已取消 / 对方已拒绝 / 无人接听 / 对方忙线中 / 对方不在线…
 * 未知 reason 由引擎折成 "error"，这里兜底成「通话已结束」。
 */
QString callerEndText(const QString& reason);

/** 通话记录里那一行摘要：呼出 · 03:21 / 未接来电 · 视频 / 群通话 · 6 人。 */
QString recordSummary(const CallRecord& record);

/**
 * 来电邀请语：邀请你视频通话 / 邀请你语音通话 / 邀请你加入群通话。
 * 横幅与来电浮层共用这一句，与 Web 的 `incomingInviteText` 同一张表。
 */
QString incomingInviteText(bool isVideo, bool isGroup);

/** 记录行的时间列：今天显示 hh:mm，昨天显示「昨天」，更早显示 M月d日。 */
QString recordTimestamp(const CallRecord& record);

}  // namespace callstrings
