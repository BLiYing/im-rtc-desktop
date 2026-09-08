#pragma once

#include <string>

#include "imrtc/Json.h"
#include "imrtc/Log.h"

namespace imrtc {

/**
 * frameLogFields 从一帧里挑出**必带字段**（LOGGING.md §3），拼成一条日志的字段表。
 *
 * # 为什么只挑字段，不打整条 data
 *
 * 两个理由，缺一条都不成立：
 * - **脱敏**：`sys.hello` 带 token、`room.offer` 带整条 SDP、`room.ice_candidate`
 *   带候选（暴露内网拓扑）。整条打出去等于把一次通话的传输面摊开（LOGGING.md §4）。
 * - **可检索**：排查时问的是「那通电话的帧都长什么样」，靠的是按 `call_id` 过滤，
 *   而不是读一大段 JSON。
 *
 * 挑的是 `call_id` / `room_id` / `track_id` / `code`——**有哪个带哪个**（CONVENTIONS §8）。
 */
LogFields frameLogFields(const std::string& type, const std::string& reqId, const Json& data);

}  // namespace imrtc
