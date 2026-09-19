#pragma once

/**
 * CallHistory.h —— 通话记录（**从服务端拉**，SDK 的 `fetchCallHistory`，`GET /v1/calls`）。
 *
 * 这一页是「宿主会拿 SDK 做什么」的示范，不是要求宿主照抄：想自己存，就拿 `onCallEnd`
 * 落自己的库；想让换设备、重装之后记录还在，就查这里。两者不必并存——这里没有本地那一份。
 *
 * 一条记录的每个字段都来自服务端，界面不猜：
 *   - 时长用服务端给的 `duration_sec`，**不许自己拿时间戳减**（不变量 I8）；
 *   - `reason` 是通话的最终结局，**不分角色**，方向与「是否未接」由 `caller == 我` 推出；
 *   - 游标翻页：`nextCursor == 0` 就是到底，不要再请求。
 */

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

#include "imrtc/CallEngine.hpp"

/** 一条记录（界面用的行模型）。字段全部由服务端的记录换算而来。 */
struct CallRecord {
  QString callId;
  QString peer;          ///< 1v1 的对方 uid；群通话为空
  QStringList members;   ///< 群通话成员（含主叫，用于「N 人」）
  bool isGroup = false;
  QString mediaType;     ///< "audio" / "video"
  bool outgoing = true;  ///< 我拨出的（caller == 我）
  bool connected = false;///< 接通过（connected_at_ms > 0）
  QString reason;        ///< §6 封闭枚举
  qint64 durationSec = 0;///< **来自服务端**，不是自己算的
  QDateTime startedAt;   ///< 记录页的时间按**发起时间**显示（四端统一）
  QDateTime endedAt;
};

class CallHistory : public QObject {
  Q_OBJECT

public:
  using Page = imrtc::capi::CallHistoryPage;
  using Done = std::function<void(imrtc::capi::Result<Page>)>;
  /** Fetcher 发起一次查询：返回错误码（0 = 受理，结果经 done 回来；非 0 = 根本没受理，不会再调 done）。 */
  using Fetcher = std::function<qint32(int limit, qint64 cursor, Done done)>;

  static constexpr int kPageSize = 20;

  explicit CallHistory(QObject* parent = nullptr);

  void setFetcher(Fetcher fetcher) { fetcher_ = std::move(fetcher); }
  /** 登录后设一下自己的 uid：方向与「未接」要拿它跟 `caller` 比。 */
  void setSelfUid(const QString& uid) { selfUid_ = uid; }

  const QList<CallRecord>& records() const { return records_; }
  bool hasMore() const { return hasMore_; }
  bool loading() const { return loading_; }
  /** 最近一次失败的说明；成功后清空。 */
  const QString& error() const { return error_; }

  /** 重拉首页。进页、每次通话结束、点「刷新」都走它；还在路上的旧请求作废。 */
  void refresh();
  /** 拉下一页。没有更多、或已经在拉就是空操作。 */
  void loadMore();
  /** 登出时清空并作废在路上的请求。 */
  void reset();
  /** 只给离线截图工具（Shots.cpp）喂假数据用：直接摆上一批记录，不发请求。 */
  void showSample(const QList<CallRecord>& records);

  /** 服务端的一条记录 → 界面行模型。`me` 是自己的 uid。纯函数，单测直接验。 */
  static CallRecord toRecord(const imrtc::capi::CallHistoryRecord& source, const QString& me);

signals:
  void changed();

private:
  void load(bool first);

  Fetcher fetcher_;
  QString selfUid_;
  QList<CallRecord> records_;
  qint64 nextCursor_ = 0;
  bool hasMore_ = false;
  bool loading_ = false;
  QString error_;
  /** 刷新会让还在路上的旧请求作废：应答回来时代数对不上就丢掉。 */
  quint64 generation_ = 0;
};
