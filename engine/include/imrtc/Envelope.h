#pragma once

#include <cstdint>
#include <string>

#include "imrtc/Json.h"

namespace imrtc {

/**
 * 信封：**每一帧都是一个 JSON 对象，四个字段，一个不多一个不少**
 * （RTC_PROTOCOL.md §2.1）。
 */
struct Envelope {
  std::string type;
  /** 服务端主动推送的事件恒为 ""（§2.2）。 */
  std::string reqId;
  /** 发送方的 Unix 毫秒时间戳。**只用于日志，禁止参与逻辑判断**（时钟偏移）。 */
  std::int64_t ts = 0;
  Json data;
};

/** kOkSuffix 是成功应答的唯一后缀（§2.2）。 */
extern const char* const kOkSuffix;

/** okType 返回某个请求帧对应的成功应答类型。 */
std::string okType(const std::string& requestType);

/**
 * decodeEnvelope 把一帧原始文本解成 Envelope，并施加 §2.4 的编码硬规则。
 *
 * 它**不做**帧级解析——那是 decodeFrame 的事。分成两步是因为路由阶段只需要
 * type 与 req_id，没必要为一个马上要丢掉的帧去解 data。
 *
 * 失败抛 RtcError：语法/信封问题是 `bad_envelope`，data 内容违反编码硬规则
 * 是 `bad_params`（哪条对应哪个码见 conformance/envelope.json）。
 */
Envelope decodeEnvelope(const std::string& raw);

/** encodeEnvelope 序列化一帧。data 不是对象时写成 {}。 */
std::string encodeEnvelope(const std::string& type, const std::string& reqId, const Json& data,
                           std::int64_t ts);

/** isEvent 报告这一帧是不是服务端主动推送的事件。 */
inline bool isEvent(const Envelope& envelope) { return envelope.reqId.empty(); }

/**
 * checkDiscipline 检查已解析的 data 是否满足 §2.4 里**能脱离帧定义判**的四条：
 * 不放 null、没有浮点数、整数不超过 2^53-1、数组同构、对象不超过两层。
 *
 * 剩下两条（字段类型恒定、枚举封闭带兜底）没法脱离帧定义判，在 FieldSpec 里。
 */
void checkDiscipline(const Json& data);

}  // namespace imrtc
