#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/ActionResult.h"

namespace imrtc {

/*
 * 通话记录查询：`GET /v1/calls`（server 设计文档 §4.5）。
 *
 * **宿主不一定要用它**：很多宿主拿 `onCallEnd` 自己存、或拿 webhook 落自己的库就够了。
 * 想让「换设备、重装之后记录还在」，或者不想自己存，就调 `CallEngine::fetchCallHistory`。
 *
 * 走的是当前登录用的那枚接入票（含 `updateToken` 换过的），服务端据此**只返回本人参与过的通话**——
 * 所以没有 `uid` 参数，也不能查别人。语义与 iOS / Android / Web 四端一致。
 */

/** 通话记录里的一位成员。`state` 是这位成员在这通电话里的结局，原样透传。 */
struct CallHistoryMember {
  std::string uid;
  std::string state;
};

/**
 * 一条通话记录，字段与服务端 `GET /v1/calls` 一一对应。
 *
 * `reason` 是通话的最终结局（`hangup` / `cancel` / `reject` / `no_answer`…），**不分角色**：
 * 要显示「已取消」还是「对方已取消」，用 `caller` 与自己的 uid 比出角色再定文案。
 */
struct CallHistoryRecord {
  std::string callId;
  std::string roomId;
  std::string caller;
  /** "audio" 或 "video"。 */
  std::string mediaType;
  bool isGroup = false;
  std::string reason;
  std::string endedBy;
  std::int64_t durationSec = 0;
  std::int64_t startedAtMs = 0;
  std::int64_t connectedAtMs = 0;
  std::int64_t endedAtMs = 0;
  std::string userData;
  std::string chatGroupId;
  std::vector<CallHistoryMember> members;
};

/** 一页通话记录（按发起时间倒序）。`hasNext == false` 表示已经到底。 */
struct CallHistoryPage {
  std::vector<CallHistoryRecord> records;
  bool hasNext = false;
  /** 下一页的游标，仅 `hasNext` 时有意义。 */
  std::int64_t nextCursor = 0;
};

/**
 * CallHistoryCompletion 接一次查询的结果。**恰好调一次**，在调 `tick()` 的那个线程上
 * （本地就地拒绝时在 `fetchCallHistory` 返回之前）。失败时 `result.code != 0`、`page` 为空。
 * 失败**不经** `onError` 再抛一遍。
 */
using CallHistoryCompletion = std::function<void(const ActionResult&, const CallHistoryPage&)>;

/** 服务端每页上限（`maxCallLimit`）与默认页大小。 */
constexpr std::int64_t kMaxCallHistoryLimit = 200;
constexpr std::int64_t kDefaultCallHistoryLimit = 20;

/**
 * clampCallHistoryLimit 把 limit 夹在 1..200。超过服务端上限它也只回 200 条，
 * 「不满一页就是到底」的判据就不成立了，所以本地先夹住。
 */
std::int64_t clampCallHistoryLimit(std::int64_t limit);

/** restBaseUrl 由信令地址推出 REST 根：`ws→http`、`wss→https`，去掉末尾 `/v1/ws`。推不出返回空串。 */
std::string restBaseUrl(const std::string& signalingUrl);

/** callHistoryUrl 拼出请求地址；`cursor` 只在 >0 时带。信令地址推不出 REST 根时返回空串。 */
std::string callHistoryUrl(const std::string& signalingUrl, std::int64_t limit, std::int64_t cursor);

/** CallHistoryOutcome 是一次应答的解析结果。 */
struct CallHistoryOutcome {
  ActionResult result;
  CallHistoryPage page;
};

/**
 * parseCallHistory 把状态码与应答体解成一页记录。
 *
 * 200 才解；401 → 1101；其它状态码 / 解析失败 → 1501。字段缺失一律解成零值。
 * **服务端只要这页有数据就给 next_cursor，没有「到底」标志**：不满一页（条数 < limit）
 * 一定是最后一页，满页才交出游标（最坏多翻一页空的）。
 */
CallHistoryOutcome parseCallHistory(std::int32_t status, const std::string& body, std::int64_t limit);

}  // namespace imrtc
