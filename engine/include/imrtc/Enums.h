#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imrtc {

/**
 * 协议枚举：**封闭的小写字符串集合**（RTC_PROTOCOL.md §2.4 规则 6）。
 *
 * 每个枚举都配一个兜底值，收到集合外的值折成它——这是「新增枚举值不算破坏兼容」
 * （§10）成立的前提：服务端发一个老客户端不认识的值时，老客户端不能崩、
 * 也不许把原始字符串透传给 UI。
 *
 * 用字符串集合而不是 C++ `enum`：线路上就是字符串，转成枚举再转回来只会多两次
 * 映射和两处漂移点。**类型安全由 FieldSpec 的归一化保证**。
 */
using EnumValues = std::vector<std::string>;

/** mediaTypes：通话媒体类型。兜底 "audio"——宁可当语音，也不去开一个不存在的摄像头。 */
const EnumValues& mediaTypes();
/** reasonValues：通话结束原因，与 Reasons.h 同一份。兜底 "error"。 */
const EnumValues& reasonValues();
/** layers：simulcast 层。"none" = 暂停下发但保留订阅。兜底 "l"——宁可给小图。 */
const EnumValues& layers();
/**
 * autoSubscribeModes：`room.join.auto_subscribe` 的三档（协议 2 起，之前是布尔）。
 *
 * 兜底 "all"——认不出的档位按「全订」处理。反过来兜成 "none" 的话，
 * 一个字母写错就是「人进了房，谁都看不见也听不见」，而且没有任何一处报错。
 *
 * - "all"   音频 + 视频都由服务端自动订阅（通话房）
 * - "audio" 只自动订音频，视频由客户端按当前页 `room.subscribe`（会议分页画廊，桌面端未做）
 * - "none"  一条都不自动订
 */
const EnumValues& autoSubscribeModes();
/** autoSubscribeCovers 报告这一档要不要让服务端自动订阅某种 kind 的 Track。 */
bool autoSubscribeCovers(const std::string& mode, const std::string& kind);
/** coerceAutoSubscribe 把线路上的档位归一化，认不出的一律按 "all"（§2.4 规则 6）。 */
std::string coerceAutoSubscribe(const std::string& mode);
/** trackKinds：Track 类型。 */
const EnumValues& trackKinds();
/** trackSources：Track 来源。同一 participant 同一 source 最多一条 Track。 */
const EnumValues& trackSources();
/**
 * pcRoles：两条 PeerConnection。**每条的 offerer 是固定的**（§3.3）——
 * pub 由客户端 offer、sub 由服务端 offer。固定 offerer 就没有 glare，
 * 所以 engine 里不需要 perfect negotiation / rollback。
 */
const EnumValues& pcRoles();
/** roomKinds：房间策略预设。 */
const EnumValues& roomKinds();
/** handledActions：另一台设备做了什么。 */
const EnumValues& handledActions();

/** kQualityUnknown 是网络质量的 unknown，也是越界值的兜底（§3.4）。 */
constexpr std::int64_t kQualityUnknown = 0;
/** kQualityDisconnected 是 level 的上界。 */
constexpr std::int64_t kQualityDisconnected = 6;

/** 振铃超时的默认值与范围（§2.6）。越界钳到边界，不报错。 */
constexpr std::int64_t kDefaultTimeoutSec = 30;
/** kMinTimeoutSec 是振铃超时下界。 */
constexpr std::int64_t kMinTimeoutSec = 5;
/** kMaxTimeoutSec 是振铃超时上界。 */
constexpr std::int64_t kMaxTimeoutSec = 120;

/** kMaxFrameBytes 是单帧**上行**上限（§2.6）。超限对应 WS 关闭码 4400。 */
constexpr std::size_t kMaxFrameBytes = 64 * 1024;

/**
 * kMaxReceivedFrameBytes 是**收帧**的容忍上限（§2.6，2.0.0 起）。
 *
 * **发帧与收帧不是同一个数**：发仍卡 64 KiB，收放宽到 256 KiB。
 * 服务端今天发的下行 offer 都远小于 64 KiB；放宽的是**以后**——会议到 100 人时
 * 每人一条音频 m-line，整帧约 80 KB（MEETING_ROOM_DESIGN §9 ③）。那时只要改服务端，
 * 不必让已经发出去的 2.0.0 客户端跟着升一次版本。
 *
 * 放宽收不放宽发，是因为收帧上限是「愿意为对端花多少内存」，
 * 发帧上限是「允许对端为我花多少内存」——后者松不得。
 */
constexpr std::size_t kMaxReceivedFrameBytes = 256 * 1024;

/**
 * kChatGroupIdMaxBytes 是 `chat_group_id` 的长度上界（§2.6）。**公开出去**，
 * 否则宿主会把 64 抄进自己代码里。禁止空白与换行由 `CallMachine.cpp` 的
 * `chatGroupIdValid` 另外判。
 */
constexpr std::size_t kChatGroupIdMaxBytes = 64;
/** kUserDataMaxBytes 是 `user_data` 的长度上界（§2.6）。 */
constexpr std::size_t kUserDataMaxBytes = 4096;

/** kMaxSafeProtocolInt = 2^53-1（§2.4 规则 7）。超出会在 JS 端静默丢精度。 */
constexpr std::int64_t kMaxSafeProtocolInt = 9007199254740991LL;

/** kMaxObjectDepth：data 本身算第 1 层，再嵌一层算第 2 层，第 3 层非法（规则 5）。 */
constexpr int kMaxObjectDepth = 2;

}  // namespace imrtc
