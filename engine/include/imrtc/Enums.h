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

/** kMaxFrameBytes 是单帧上限（§2.6）。超限对应 WS 关闭码 4400。 */
constexpr std::size_t kMaxFrameBytes = 64 * 1024;

/** kMaxSafeProtocolInt = 2^53-1（§2.4 规则 7）。超出会在 JS 端静默丢精度。 */
constexpr std::int64_t kMaxSafeProtocolInt = 9007199254740991LL;

/** kMaxObjectDepth：data 本身算第 1 层，再嵌一层算第 2 层，第 3 层非法（规则 5）。 */
constexpr int kMaxObjectDepth = 2;

}  // namespace imrtc
