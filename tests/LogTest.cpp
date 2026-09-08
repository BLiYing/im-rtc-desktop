#include <string>
#include <vector>

#include "TestHarness.h"
#include "imrtc/FrameLog.h"
#include "imrtc/Json.h"
#include "imrtc/Log.h"

using imrtc::LogFields;
using imrtc::LogLevel;

/**
 * 日志设施（`im-rtc-server/docs/mechanism/LOGGING.md`，五仓统一）。
 *
 * 这些用例自己调 `setLogLevel`——测试主程序把级别压到了 error（见 TestHarness.cpp），
 * 不调回来就等于在验一个永远不输出的假东西。**每条用例结束前必须还原**，
 * 否则后面的用例会被这一条的副作用带着跑（进程级状态就这一个坑）。
 */
namespace {

/** Captured 是一条被 sink 收到的日志。 */
struct Captured {
  LogLevel level;
  std::string message;
  LogFields fields;
};

/** Recorder 装一个 sink 并在析构时把级别与 sink 都还原。 */
class Recorder {
public:
  explicit Recorder(LogLevel level = LogLevel::Debug) {
    imrtc::setLogLevel(level);
    imrtc::setLogSink([this](LogLevel lv, const std::string& msg, const LogFields& fields) {
      entries.push_back(Captured{lv, msg, fields});
    });
  }
  ~Recorder() {
    imrtc::setLogSink(nullptr);
    imrtc::setLogLevel(LogLevel::Error);
  }
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

  /** valueOf 取某条日志的某个字段；没有返回空串。 */
  std::string valueOf(std::size_t index, const std::string& key) const {
    if (index >= entries.size()) return {};
    for (const auto& field : entries[index].fields) {
      if (field.first == key) return field.second;
    }
    return {};
  }

  std::vector<Captured> entries;
};

}  // namespace

IMRTC_TEST(logLevelGate, "日志 —— 级别不够的一条都不该到 sink（debug 生产默认关）") {
  Recorder recorder(LogLevel::Info);
  imrtc::log(LogLevel::Debug, "不该出现");
  imrtc::log(LogLevel::Info, "该出现");
  imrtc::log(LogLevel::Error, "也该出现");

  CHECK_EQ(recorder.entries.size(), std::size_t{2}, "debug 被闸掉");
  CHECK_EQ(recorder.entries[0].message, std::string("该出现"), "info 过闸");
  CHECK_EQ(recorder.entries[1].message, std::string("也该出现"), "error 过闸");
}

IMRTC_TEST(logSinkIsFanOutNotReplace,
           "日志 —— 装 sink 是 fan-out：卸掉之后内置那一路还在，级别也没被改坏") {
  {
    Recorder recorder;
    imrtc::log(LogLevel::Info, "在 sink 里");
    CHECK_EQ(recorder.entries.size(), std::size_t{1}, "sink 收得到");
  }
  /*
    iOS 上踩过的反例：`IMRTCLog` 原先「装了 sink 就 return」，而 Demo 登录后装的
    正是回传服务端的 sink，于是 Xcode 控制台再也没有 Engine 日志——偏偏那天要查的
    故障**本身就是网络断了**，唯一的出口跟着一起没了（CLIENT_PARITY v1.15）。

    这里能钉住的是「卸掉 sink 之后 log() 照常工作」这一半；内置那一路写的是
    stderr，单测里观测不到，硬造一个只会得到一条抓不住回归的假用例。
  */
  imrtc::setLogLevel(LogLevel::Debug);
  imrtc::log(LogLevel::Info, "没有 sink 也不该崩");
  imrtc::setLogLevel(LogLevel::Error);
}

IMRTC_TEST(logRedactSecret, "脱敏 —— 凭据只留前 6 位 + 长度（够比对，不够伪造）") {
  const std::string token =
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJhbGljZSJ9.c2lnbmF0dXJl";
  const std::string redacted = imrtc::redact(token);

  CHECK_EQ(redacted, std::string("eyJhbG…(len=70)"), "形状与另外四端一致");
  CHECK_EQ(redacted.find("signature") == std::string::npos, true, "签名段不许留");
  CHECK_EQ(redacted.find(token.substr(10, 20)) == std::string::npos, true, "载荷段不许留");
}

IMRTC_TEST(logRedactSdp, "脱敏 —— SDP 只留行数与 m= 类型（它带候选、指纹与密钥材料）") {
  const std::string sdp =
      "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
      "a=fingerprint:sha-256 AB:CD\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\n";
  const std::string redacted = imrtc::redactSdp(sdp);

  CHECK_EQ(redacted, std::string("sdp(lines=6, m=audio,m=video)"), "只剩形状");
  CHECK_EQ(redacted.find("fingerprint") == std::string::npos, true, "DTLS 指纹不许留");
  CHECK_EQ(redacted.find("127.0.0.1") == std::string::npos, true, "地址不许留");
}

IMRTC_TEST(logRedactCandidate, "脱敏 —— 候选只留传输与类型（地址会暴露内网拓扑）") {
  CHECK_EQ(imrtc::redactCandidate("candidate:1 1 udp 2130706431 192.168.1.12 54321 typ host"),
           std::string("candidate(udp/host)"), "常规候选");
  CHECK_EQ(imrtc::redactCandidate("candidate:2 1 tcp 1 203.0.113.7 9 typ srflx raddr 10.0.0.1"),
           std::string("candidate(tcp/srflx)"), "srflx 的 raddr 同样不许留");
  // **脱敏助手绝不能因为输入不合预期就抛**：它被调用的地方全是「正在记录一个异常」。
  CHECK_EQ(imrtc::redactCandidate(""), std::string("candidate(unknown/unknown)"), "空串不许炸");
  CHECK_EQ(imrtc::redactCandidate("garbage"), std::string("candidate(unknown/unknown)"),
           "垃圾输入不许炸");
}

IMRTC_TEST(logFrameFields, "帧日志 —— 必带字段有哪个带哪个，且不带 data 本身") {
  const imrtc::Json data = imrtc::Json::parse(
      "{\"call_id\":\"call-1\",\"room_id\":\"r-1\",\"token\":\"secret-token\","
      "\"sdp\":\"v=0 …\",\"code\":1004}");
  const LogFields fields = imrtc::frameLogFields("call.invite", "req-7", data);

  std::string flat;
  for (const auto& field : fields) flat += field.first + "=" + field.second + " ";

  CHECK_EQ(flat.find("type=call.invite") != std::string::npos, true, "带 type");
  CHECK_EQ(flat.find("request_id=req-7") != std::string::npos, true, "带 req_id");
  CHECK_EQ(flat.find("call_id=call-1") != std::string::npos, true, "带 call_id");
  CHECK_EQ(flat.find("room_id=r-1") != std::string::npos, true, "带 room_id");
  CHECK_EQ(flat.find("code=1004") != std::string::npos, true, "错误码也要带");

  // **这两条是这个用例的重点**：挑字段而不是打整条 data，token 与 SDP 才不会漏出去。
  CHECK_EQ(flat.find("secret-token") == std::string::npos, true, "token 不许出现在帧日志里");
  CHECK_EQ(flat.find("v=0") == std::string::npos, true, "SDP 不许出现在帧日志里");
}

IMRTC_TEST(logFrameFieldsSkipsEmpty, "帧日志 —— 事件帧的空 req_id 不占一格") {
  const LogFields fields = imrtc::frameLogFields("call.incoming", "", imrtc::Json::makeObject());
  CHECK_EQ(fields.size(), std::size_t{1}, "只剩 type");
  CHECK_EQ(fields[0].first, std::string("type"), "就是 type");
}
