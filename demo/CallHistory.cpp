#include "CallHistory.h"

#include <utility>

CallHistory::CallHistory(QObject* parent) : QObject(parent) {}

CallRecord CallHistory::toRecord(const imrtc::capi::CallHistoryRecord& source, const QString& me) {
  CallRecord record;
  record.callId = QString::fromStdString(source.callId);
  record.isGroup = source.isGroup;
  record.mediaType = QString::fromStdString(source.mediaType);
  record.reason = QString::fromStdString(source.reason);
  record.durationSec = source.durationSec;
  record.connected = source.connectedAtMs > 0;

  const QString caller = QString::fromStdString(source.caller);
  record.outgoing = caller == me;
  for (const imrtc::capi::CallHistoryMember& member : source.members) {
    record.members << QString::fromStdString(member.uid);
  }
  // 「N 人」要算上主叫：服务端的成员表是被叫名单，主叫不一定在里面。
  if (!caller.isEmpty() && !record.members.contains(caller)) record.members.prepend(caller);

  if (!record.isGroup) {
    // 对方：被叫看主叫；主叫看第一个不是自己的成员。
    if (!record.outgoing) {
      record.peer = caller;
    } else {
      for (const QString& uid : std::as_const(record.members)) {
        if (uid != me) {
          record.peer = uid;
          break;
        }
      }
    }
  }
  const qint64 endedAt = source.endedAtMs > 0 ? source.endedAtMs : source.startedAtMs;
  record.endedAt = QDateTime::fromMSecsSinceEpoch(endedAt);
  record.startedAt = QDateTime::fromMSecsSinceEpoch(source.startedAtMs);
  return record;
}

void CallHistory::refresh() {
  ++generation_;
  loading_ = false;
  nextCursor_ = 0;
  load(true);
}

void CallHistory::loadMore() {
  if (hasMore_ && !loading_) load(false);
}

void CallHistory::reset() {
  ++generation_;
  loading_ = false;
  hasMore_ = false;
  nextCursor_ = 0;
  error_.clear();
  records_.clear();
  emit changed();
}

void CallHistory::showSample(const QList<CallRecord>& records) {
  records_ = records;
  hasMore_ = false;
  error_.clear();
  emit changed();
}

void CallHistory::load(bool first) {
  if (!fetcher_) return;
  loading_ = true;
  const quint64 ticket = generation_;
  const qint64 cursor = first ? 0 : nextCursor_;
  const qint32 code = fetcher_(kPageSize, cursor, [this, ticket, first](imrtc::capi::Result<Page> result) {
    if (ticket != generation_) return;  // 已经被新的刷新作废
    loading_ = false;
    if (result.ok()) {
      error_.clear();
      QList<CallRecord> page;
      for (const imrtc::capi::CallHistoryRecord& source : result.value.records) {
        page << toRecord(source, selfUid_);
      }
      if (first) {
        records_ = page;
      } else {
        records_ += page;
      }
      hasMore_ = result.value.hasNext();
      nextCursor_ = result.value.nextCursor;
    } else {
      error_ = QStringLiteral("%1 (%2)").arg(QString::fromStdString(result.name)).arg(result.code);
      if (first) {
        records_.clear();
        hasMore_ = false;
      }
    }
    emit changed();
  });
  if (code != 0) {
    loading_ = false;
    error_ = QStringLiteral("%1").arg(code);
    emit changed();
    return;
  }
  emit changed();
}
