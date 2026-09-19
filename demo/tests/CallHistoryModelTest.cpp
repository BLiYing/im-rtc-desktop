/**
 * CallHistoryModelTest.cpp —— 通话记录页的数据模型（从服务端拉，游标翻页）。
 *
 * 钉四件事：
 *   - 服务端的一条记录怎么换算成界面行：方向由 `caller == 我` 推、对方是谁、未接判据、群「N 人」；
 *   - 分页：首页 → 下一页追加 → `nextCursor == 0` 就停，不会再请求；
 *   - 刷新会让还在路上的旧请求作废（否则先发后到的旧页会盖掉新数据）；
 *   - 失败要留下说明，首页失败清空列表，下一页失败保留已有的。
 *
 * 用同步 / 手动放行的假 Fetcher，不碰网络。
 */

#include <QtTest>
#include <deque>

#include "CallHistory.h"

namespace {

using imrtc::capi::CallHistoryMember;
using imrtc::capi::CallHistoryPage;
using imrtc::capi::CallHistoryRecord;
using imrtc::capi::Result;

CallHistoryRecord makeRecord(const std::string& id, const std::string& caller,
                             std::vector<std::string> callees) {
  CallHistoryRecord record;
  record.callId = id;
  record.caller = caller;
  record.mediaType = "audio";
  record.reason = "hangup";
  record.durationSec = 12;
  record.startedAtMs = 1756876800000;
  record.endedAtMs = 1756876812000;
  for (const std::string& uid : callees) record.members.push_back(CallHistoryMember{uid, "joined"});
  return record;
}

/** ScriptedFetcher 记下每次请求，结果由测试手动放行。 */
struct ScriptedFetcher {
  struct Call {
    int limit;
    qint64 cursor;
    CallHistory::Done done;
  };
  std::deque<Call> calls;

  CallHistory::Fetcher fetcher() {
    return [this](int limit, qint64 cursor, CallHistory::Done done) {
      calls.push_back(Call{limit, cursor, std::move(done)});
      return qint32{0};
    };
  }

  static Result<CallHistoryPage> page(int count, qint64 next, const std::string& prefix) {
    Result<CallHistoryPage> result;
    for (int i = 0; i < count; ++i) {
      result.value.records.push_back(makeRecord(prefix + std::to_string(i), "alice", {"bob"}));
    }
    result.value.nextCursor = next;
    return result;
  }
};

}  // namespace

class CallHistoryModelTest : public QObject {
  Q_OBJECT

private slots:
  void outgoingOneToOne();
  void incomingMissed();
  void groupCountIncludesCaller();
  void pagesAppendAndStopAtEnd();
  void refreshInvalidatesInFlight();
  void failureKeepsMessage();
};

void CallHistoryModelTest::outgoingOneToOne() {
  CallHistoryRecord source = makeRecord("c1", "alice", {"bob"});
  source.connectedAtMs = source.startedAtMs + 3000;
  const CallRecord record = CallHistory::toRecord(source, QStringLiteral("alice"));
  QVERIFY(record.outgoing);
  QVERIFY(record.connected);
  QCOMPARE(record.peer, QStringLiteral("bob"));
  QVERIFY(!record.isGroup);
  QCOMPARE(record.durationSec, qint64{12});
  QCOMPARE(record.endedAt, QDateTime::fromMSecsSinceEpoch(1756876812000));
}

void CallHistoryModelTest::incomingMissed() {
  CallHistoryRecord source = makeRecord("c2", "bob", {"alice"});
  source.reason = "no_answer";
  source.durationSec = 0;
  const CallRecord record = CallHistory::toRecord(source, QStringLiteral("alice"));
  QVERIFY(!record.outgoing);
  QVERIFY(!record.connected);
  QCOMPARE(record.peer, QStringLiteral("bob"));
}

void CallHistoryModelTest::groupCountIncludesCaller() {
  CallHistoryRecord source = makeRecord("c3", "alice", {"carol", "dave"});
  source.isGroup = true;
  const CallRecord record = CallHistory::toRecord(source, QStringLiteral("alice"));
  QVERIFY(record.isGroup);
  QCOMPARE(record.members.size(), 3);  // 主叫 + 两个被叫
  QVERIFY(record.peer.isEmpty());
}

void CallHistoryModelTest::pagesAppendAndStopAtEnd() {
  ScriptedFetcher script;
  CallHistory history;
  history.setFetcher(script.fetcher());
  history.setSelfUid(QStringLiteral("alice"));

  history.refresh();
  QCOMPARE(script.calls.size(), std::size_t{1});
  QCOMPARE(script.calls[0].cursor, qint64{0});
  QCOMPARE(script.calls[0].limit, CallHistory::kPageSize);
  QVERIFY(history.loading());
  script.calls[0].done(ScriptedFetcher::page(2, 999, "a"));
  QVERIFY(!history.loading());
  QCOMPARE(history.records().size(), 2);
  QVERIFY(history.hasMore());

  history.loadMore();
  QCOMPARE(script.calls.size(), std::size_t{2});
  QCOMPARE(script.calls[1].cursor, qint64{999});
  history.loadMore();  // 还在路上：不许重复发
  QCOMPARE(script.calls.size(), std::size_t{2});
  script.calls[1].done(ScriptedFetcher::page(1, 0, "b"));
  QCOMPARE(history.records().size(), 3);
  QVERIFY(!history.hasMore());

  history.loadMore();  // 到底了：不再请求
  QCOMPARE(script.calls.size(), std::size_t{2});
}

void CallHistoryModelTest::refreshInvalidatesInFlight() {
  ScriptedFetcher script;
  CallHistory history;
  history.setFetcher(script.fetcher());

  history.refresh();
  history.refresh();  // 第一个还没回来，第二个顶掉它
  QCOMPARE(script.calls.size(), std::size_t{2});
  script.calls[1].done(ScriptedFetcher::page(1, 0, "new"));
  script.calls[0].done(ScriptedFetcher::page(3, 0, "old"));  // 迟到的旧页
  QCOMPARE(history.records().size(), 1);
  QCOMPARE(history.records()[0].callId, QStringLiteral("new0"));
}

void CallHistoryModelTest::failureKeepsMessage() {
  ScriptedFetcher script;
  CallHistory history;
  history.setFetcher(script.fetcher());

  history.refresh();
  script.calls[0].done(ScriptedFetcher::page(2, 999, "a"));
  history.loadMore();
  Result<CallHistoryPage> failed;
  failed.code = 2003;
  failed.name = "network_unreachable";
  script.calls[1].done(failed);
  QCOMPARE(history.records().size(), 2);  // 下一页失败保留已有的
  QVERIFY(history.error().contains(QStringLiteral("network_unreachable")));

  history.refresh();
  script.calls[2].done(failed);
  QCOMPARE(history.records().size(), 0);  // 首页失败清空
  QVERIFY(!history.hasMore());

  history.reset();
  QVERIFY(history.error().isEmpty());
}

QTEST_MAIN(CallHistoryModelTest)
#include "CallHistoryModelTest.moc"
