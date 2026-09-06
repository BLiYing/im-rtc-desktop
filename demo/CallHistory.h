#pragma once

/**
 * CallHistory.h —— 通话记录。
 *
 * 这一页存在的意义不是「Demo 要好看」，而是**证明 onCallEnd 一个回调就够拼出记录**：
 * 谁、哪个方向、什么媒体、为什么结束、多久。集成方最常问的问题就是
 * 「我要做通话记录，你们给不给数据」——答案在这一页里，不在文档里。
 *
 * 两条从协议来的硬规矩：
 *   - **时长用 `onCallEnd` 给的 `durationSec`，不许自己拿时间戳减**（不变量 I8）。
 *     本地时钟与服务端不同步，自己算出来的时长会和账单对不上。
 *   - `reason` 是 §6 的**封闭枚举**，陌生值已经被引擎折成 `error`，直接 switch 即可。
 */

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

/** 一条记录。字段全部来自回调，没有一个是界面自己猜的。 */
struct CallRecord {
  QString callId;
  QString peer;          ///< 1v1 的对方 uid；群通话为空
  QStringList members;   ///< 群通话成员（含被叫，用于「N 人」）
  bool isGroup = false;
  QString mediaType;     ///< "audio" / "video"
  bool outgoing = true;  ///< 我拨出的
  bool connected = false;///< onCallBegin 来过才算接通
  QString reason;        ///< §6 封闭枚举
  qint64 durationSec = 0;///< **来自 onCallEnd**，不是自己算的
  QDateTime endedAt;
};

class CallHistory : public QObject {
  Q_OBJECT

public:
  explicit CallHistory(QObject* parent = nullptr);

  const QList<CallRecord>& records() const { return records_; }
  void add(const CallRecord& record);
  void clear();

  /** 从磁盘读 / 写回磁盘。失败只记日志，不打断界面——记录丢了不该拦住打电话。 */
  void load();
  void save() const;

signals:
  void changed();

private:
  static QString storagePath();

  QList<CallRecord> records_;
};
