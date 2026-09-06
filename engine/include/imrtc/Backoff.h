#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace imrtc {

/**
 * 重连退避：**五端同一份档位**（RTC_PROTOCOL.md §1.4）。
 *
 * 档位写死成表而不是算指数，是为了五端一眼能对上；抖动是为了避免
 * 「服务端重启后所有客户端在同一毫秒回来」把它再打挂一次。
 */

/** backoffStepsMs 是退避档位，之后固定用最后一档。 */
const std::vector<std::int64_t>& backoffStepsMs();

/** kJitterRatio 是抖动幅度：每档 ±20%。 */
constexpr double kJitterRatio = 0.2;

/** Random01 返回 [0,1) 的随机数。可注入以便测试。 */
using Random01 = std::function<double()>;

/** defaultRandom01 是运行期的默认实现（线程局部的 mt19937）。 */
double defaultRandom01();

/** backoffDelayMs 返回第 attempt 次重连该等多久（attempt 从 0 开始）。 */
std::int64_t backoffDelayMs(int attempt, const Random01& random);

}  // namespace imrtc
