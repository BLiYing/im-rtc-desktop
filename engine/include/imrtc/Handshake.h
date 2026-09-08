#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace imrtc {

/**
 * 握手这一关的两件事：**入参校验**与**被拒之后的分流**。
 *
 * 它们是同一个 bug 的两半（CLIENT_PARITY v1.18）——校验让不合规的 `device_id`
 * **根本发不出去**，分流让服务端拒绝时宿主**知道该做什么**。真机上那次
 * （Android `Build.MODEL` == `Pixel 2 XL`，带空格）两半都缺：服务端一律回 1004，
 * 客户端无限退避重连，界面只写「登录失败」，日志里刷满同一条错误。
 *
 * 抽成独立模块而不是塞进 `Connection`：这两条判据要能被直接钉住，
 * 不必每次都摆一套假服务端；`Connection.cpp` 也不用为它继续长胖。
 */

/**
 * KickedReason 是「被踢」的原因。**三类，不是一类**。
 *
 * 合并的后果是给宿主一条错的建议：把 `token_invalid`（换张票就能好）
 * 报成「去改配置」，宿主只能把可以静默恢复的场景也弹成「请重新登录」。
 * **这是五端契约**（Android `IMKickedOutReason.*`，Web/iOS 的
 * `authExpired` / `takenOver` / `configRejected`）。
 */
enum class KickedReason : std::int32_t {
  /** 同账号同设备号在别处登录，或宿主主动吊销。**回登录页**。 */
  TakenOver = 0,
  /** 票的问题（过期、被吊销、签名密钥轮换）。**换一枚票再来**。 */
  AuthExpired = 1,
  /** 参数不合规。**去改配置**——换票和重试都救不了 `device_id` 里的空格。 */
  ConfigRejected = 2,
};

/** kickedReasonName 取 snake_case 名，与状态机 args 的风格一致。 */
const char* kickedReasonName(KickedReason reason);

/** parseKickedReason 反查；不认识的值兜底成 TakenOver（4403 是唯一不带分类的来源）。 */
KickedReason parseKickedReason(const std::string& name);

/**
 * handshakeGiveUpReason 判「这次握手失败该不该一次就放弃」，该放弃则写出原因。
 *
 * 返回 false = 照常退避重连。
 *
 * # 为什么不像 4401 那样给三次机会
 *
 * 4401 给三次是因为「票刚好过期」换一枚新票就能好，重连时宿主可能已经
 * `updateToken` 了。这里不一样：**`device_id` 里有个空格这件事，重连一万次
 * 它还是有空格**。给三次只是把同一条错误在日志里刷三遍，把真正的原因埋掉。
 *
 * # 判据是错误码表里的 `retryable`，不许另立名单
 *
 * 那张表是五仓共用的一致性向量（`docs/conformance/error_codes.json`）的一部分，
 * 另立一张名单等于给它开后门。
 *
 * # 两条边界
 *
 * 1. **local 组的码直接放行。** `Connection::close()` 会拿 `2005 invalid_state`
 *    （它 `retryable == false`）把在飞的握手结掉，那是宿主自己按的 logout，
 *    不是服务端的裁决。不挡掉的话**一次正常的 logout 会报成「服务端拒了你的参数」**——
 *    而静默续期正是先 logout 再换票，会当场变成把人踹回登录页。
 *    超时（2004）与断线（2003）同在这一组，照常走重连。
 * 2. **本端不认识的码只能信帧上那一位**（`wireRetryable`）。不认识的码在本仓会被
 *    折算成 `1501 internal`，而 1501 是 `retryable == true`，于是**服务端每加一个新的
 *    终局码，客户端就多一种无限重连**——本仓漏过 1106 一次，症状正是这个。
 *
 * @param wireRetryable 错误帧上自带的 `retryable`。缺席、以及本地结算的失败，
 *   一律传 true（「不认识就先退避着」）。认识的码不看这个参数。
 */
bool handshakeGiveUpReason(std::int32_t code, bool wireRetryable, KickedReason& out);

/** device_id 的长度上界（协议 §2.5）。**公开出去**，否则宿主会把 64 抄进自己代码里。 */
constexpr std::size_t kDeviceIdMaxBytes = 64;

/**
 * deviceIdValid 按协议 §2.5 校验：非空、≤64 字节、charset `[A-Za-z0-9_-]`。
 *
 * **只校验，不改写。** `device_id` 要求跨重启稳定，SDK 悄悄替宿主改掉，
 * 宿主自己那套设备管理（设备列表、注销设备、顶号）就跟服务端对不上账了。
 * 清洗是宿主的事——而且**别用「删掉非法字符」那种做法**：`MI 8` 与 `MI8`
 * 是两款不同的机器，删完撞成同一个 id，后果是两台设备互相顶号、轮流把对方踢下线。
 */
bool deviceIdValid(const std::string& deviceId);

}  // namespace imrtc
