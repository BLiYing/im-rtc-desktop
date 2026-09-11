#include "CallStrings.h"

#include <QDate>
#include <QDateTime>
#include <QObject>

#include "CallHistory.h"

namespace callstrings {
namespace {

/** 让 tr() 有个统一的上下文名，翻译文件里好归类。 */
QString tr(const char* text) {
  return QCoreApplication::translate("callstrings", text);
}

}  // namespace

QString duration(qint64 seconds) {
  if (seconds < 0) seconds = 0;
  const qint64 hours = seconds / 3600;
  const qint64 minutes = (seconds % 3600) / 60;
  const qint64 rest = seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(rest, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2")
      .arg(minutes, 2, 10, QLatin1Char('0'))
      .arg(rest, 2, 10, QLatin1Char('0'));
}

QString callerEndText(const QString& reason) {
  // 与文案表「主叫侧」那一列逐行对应。
  if (reason == QLatin1String("cancel")) return tr("已取消");
  if (reason == QLatin1String("reject")) return tr("对方已拒绝");
  if (reason == QLatin1String("no_answer")) return tr("无人接听");
  if (reason == QLatin1String("busy")) return tr("对方忙线中");
  if (reason == QLatin1String("offline")) return tr("对方不在线");
  if (reason == QLatin1String("answered_elsewhere")) return tr("已在其他设备接听");
  if (reason == QLatin1String("rejected_elsewhere")) return tr("已在其他设备拒绝");
  if (reason == QLatin1String("kicked")) return tr("你已被移出通话");
  if (reason == QLatin1String("room_closed")) return tr("通话已结束");
  if (reason == QLatin1String("network")) return tr("连接已断开");
  // hangup 与 error 都落到这里：正常挂断不需要解释，error 也不该把内部细节甩给用户。
  return tr("通话已结束");
}

QString recordSummary(const CallRecord& record) {
  if (record.isGroup) {
    // 文案表最后一组：群通话 · 6 人 · 12:40
    const QString people = tr("%1 人").arg(record.members.size());
    if (record.connected && record.durationSec > 0) {
      return tr("群通话 · %1 · %2").arg(people, duration(record.durationSec));
    }
    return tr("群通话 · %1").arg(people);
  }

  // 接通过就报时长——network 断线也算接通，时长按已接通部分计（文案表「断线」行）。
  if (record.connected) {
    const QString head = record.outgoing ? tr("呼出") : tr("来电");
    return QStringLiteral("%1 · %2").arg(head, duration(record.durationSec));
  }

  // 没接通：主叫侧报原因，被叫侧一律是「未接来电」（文案表「通话记录」列的两侧写法）。
  if (!record.outgoing) {
    if (record.reason == QLatin1String("reject")) return tr("已拒绝");
    const QString kind = record.mediaType == QLatin1String("video") ? tr("视频") : tr("语音");
    return tr("未接来电 · %1").arg(kind);
  }
  if (record.reason == QLatin1String("cancel")) return tr("呼出 · 已取消");
  if (record.reason == QLatin1String("reject")) return tr("呼出 · 对方已拒绝");
  if (record.reason == QLatin1String("no_answer")) return tr("呼出 · 无人接听");
  if (record.reason == QLatin1String("busy")) return tr("呼出 · 对方忙线");
  if (record.reason == QLatin1String("offline")) return tr("呼出 · 对方不在线");
  return tr("呼出 · 未接通");
}

QString incomingInviteText(bool isVideo, bool isGroup) {
  if (isGroup) return tr("邀请你加入群通话");
  return isVideo ? tr("邀请你视频通话") : tr("邀请你语音通话");
}

QString recordTimestamp(const CallRecord& record) {
  if (!record.endedAt.isValid()) return QString();
  const QDate today = QDate::currentDate();
  const QDate day = record.endedAt.date();
  if (day == today) return record.endedAt.toString(QStringLiteral("HH:mm"));
  if (day == today.addDays(-1)) return tr("昨天");
  return record.endedAt.toString(tr("M月d日"));
}

}  // namespace callstrings
