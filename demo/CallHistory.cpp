#include "CallHistory.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace {

Q_LOGGING_CATEGORY(lcHistory, "imrtc.demo.history")

/** 记录只是 Demo 的展示物，留 200 条足够翻，不必无限长。 */
constexpr int kMaxRecords = 200;

QJsonObject toJson(const CallRecord& record) {
  QJsonObject object;
  object[QStringLiteral("callId")] = record.callId;
  object[QStringLiteral("peer")] = record.peer;
  object[QStringLiteral("members")] = QJsonArray::fromStringList(record.members);
  object[QStringLiteral("isGroup")] = record.isGroup;
  object[QStringLiteral("mediaType")] = record.mediaType;
  object[QStringLiteral("outgoing")] = record.outgoing;
  object[QStringLiteral("connected")] = record.connected;
  object[QStringLiteral("reason")] = record.reason;
  object[QStringLiteral("durationSec")] = static_cast<double>(record.durationSec);
  object[QStringLiteral("endedAt")] = record.endedAt.toString(Qt::ISODate);
  return object;
}

CallRecord fromJson(const QJsonObject& object) {
  CallRecord record;
  record.callId = object[QStringLiteral("callId")].toString();
  record.peer = object[QStringLiteral("peer")].toString();
  const QJsonArray members = object[QStringLiteral("members")].toArray();
  for (const QJsonValue& value : members) record.members << value.toString();
  record.isGroup = object[QStringLiteral("isGroup")].toBool();
  record.mediaType = object[QStringLiteral("mediaType")].toString();
  record.outgoing = object[QStringLiteral("outgoing")].toBool(true);
  record.connected = object[QStringLiteral("connected")].toBool();
  record.reason = object[QStringLiteral("reason")].toString();
  record.durationSec = static_cast<qint64>(object[QStringLiteral("durationSec")].toDouble());
  record.endedAt = QDateTime::fromString(object[QStringLiteral("endedAt")].toString(), Qt::ISODate);
  return record;
}

}  // namespace

CallHistory::CallHistory(QObject* parent) : QObject(parent) {}

QString CallHistory::storagePath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  return dir + QStringLiteral("/call-history.json");
}

void CallHistory::add(const CallRecord& record) {
  records_.prepend(record);
  while (records_.size() > kMaxRecords) records_.removeLast();
  save();
  emit changed();
}

void CallHistory::clear() {
  records_.clear();
  save();
  emit changed();
}

void CallHistory::load() {
  QFile file(storagePath());
  if (!file.exists()) return;
  if (!file.open(QIODevice::ReadOnly)) {
    // 读不出来就当没有记录——**不能因为读不了历史就不让人打电话**。
    qCWarning(lcHistory, "打不开通话记录：%s", qUtf8Printable(file.errorString()));
    return;
  }
  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError) {
    qCWarning(lcHistory, "通话记录解析失败：%s", qUtf8Printable(error.errorString()));
    return;
  }
  records_.clear();
  const QJsonArray array = document.array();
  for (const QJsonValue& value : array) records_.append(fromJson(value.toObject()));
  emit changed();
}

void CallHistory::save() const {
  const QString path = storagePath();
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    qCWarning(lcHistory, "建不了目录：%s", qUtf8Printable(QFileInfo(path).absolutePath()));
    return;
  }
  QJsonArray array;
  for (const CallRecord& record : records_) array.append(toJson(record));

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qCWarning(lcHistory, "写不了通话记录：%s", qUtf8Printable(file.errorString()));
    return;
  }
  file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}
