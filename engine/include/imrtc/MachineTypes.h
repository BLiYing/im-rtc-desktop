#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "imrtc/Json.h"

namespace imrtc {

/**
 * 状态机的公共类型。
 *
 * 状态机是**纯函数 reducer**：`(state, input) -> {state, send, emit}`。
 * 不碰网络、不碰 UI、不碰计时器——所以它能被
 * `im-rtc-server/docs/conformance 下的两份 _fsm.json` 的向量逐条驱动，
 * 与另外四端跑**同一份**用例。
 */

/** OutgoingFrame 是状态机要求发出去的一帧（线路形状，snake_case）。 */
struct OutgoingFrame {
  std::string type;
  Json data;
};

/**
 * EmittedEvent 是状态机要求抛给宿主的一个回调。
 *
 * `args` 的键用**协议的 snake_case 名**，与一致性向量一致；
 * 由门面转成各端惯用形式再交给宿主。
 */
struct EmittedEvent {
  std::string cb;
  Json args;
};

/**
 * LocalReject 是「状态机就地拒掉了这次调用」（一致性向量里 `act` 步骤的 `result`）。
 *
 * **不是事件**：它只回给发起这次调用的人，不经 `onError` 广播——一次失败只从一个出口报
 * （server `docs/design/ACTION_RESULT_DESIGN.md` R3）。`code == 0` 表示没有拒绝。
 */
struct LocalReject {
  std::int32_t code = 0;
  std::string name;

  bool rejected() const { return code != 0; }
};

/** MachineOutput 是一次状态转移的产物。 */
template <typename Ctx>
struct MachineOutput {
  Ctx state;
  std::vector<OutgoingFrame> send;
  std::vector<EmittedEvent> emit;
  /** 只有 `act` 输入会带：这次调用被本地拒掉了。 */
  LocalReject reject;
};

/** MachineInput 是驱动状态机的三种输入之一（与向量的 act / recv / internal 一一对应）。 */
struct MachineInput {
  enum class Kind {
    /** 宿主调用了 Engine 的公开方法。name = op，payload = args。 */
    Act,
    /** 收到一条下行帧。name = type，payload = data。 */
    Recv,
    /** Engine 内部事件，既不来自信令也不来自宿主。name = 事件名。 */
    Internal,
  };

  Kind kind = Kind::Internal;
  std::string name;
  Json payload;

  static MachineInput act(std::string op, Json args = Json::makeObject());
  static MachineInput recv(std::string type, Json data = Json::makeObject());
  /**
   * internal 的 `args` 只有「哪一条被拒了」这类需要带标识的内部事件才用
   * （`publish_failed` 的 cid、`subscribe_failed` 的 track_id）。
   */
  static MachineInput internal(std::string name, Json args = Json::makeObject());
};

/** str 从线路数据里安全取一个字符串字段；缺席或类型不符返回 ""。 */
std::string str(const Json& data, const std::string& key);
/** num 从线路数据里安全取一个整数字段；缺席或类型不符返回 0。 */
std::int64_t num(const Json& data, const std::string& key);
/** boolean 从线路数据里安全取一个布尔字段；只有真布尔 true 才算 true。 */
bool boolean(const Json& data, const std::string& key);
/** strArray 从线路数据里安全取一个字符串数组字段；非数组返回空。 */
std::vector<std::string> strArray(const Json& data, const std::string& key);

/** obj 组装一个线路形状的对象，写起来比一串 set() 短。 */
Json obj(std::vector<Json::Member> members);

/** frameOf / eventOf 是两个构造快捷方式，让状态机代码读起来接近协议表。 */
OutgoingFrame frameOf(const std::string& type, Json data);
EmittedEvent eventOf(const std::string& cb, Json args);

}  // namespace imrtc
