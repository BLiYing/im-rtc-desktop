#include <string>

#include "TestHarness.h"
#include "imrtc/Errors.h"
#include "imrtc/Handshake.h"

using imrtc::deviceIdValid;
using imrtc::handshakeGiveUpReason;
using imrtc::KickedReason;

/**
 * 握手这一关的两条纯判据（CLIENT_PARITY v1.21 的「谁救得了」+ v1.18 的入参校验）。
 *
 * 它们是纯函数，所以这里一套假服务端都不用摆——摆一套只会把判据本身埋进时序里。
 * 端到端那半在 ReconnectTest 里，那里才需要「服务端拒了并且关掉连接」这套载重。
 */
namespace {

/** giveUp 跑一次分流，返回原因名；不放弃则返回 "retry"。 */
std::string giveUp(std::int32_t code, bool wireRetryable = true) {
  KickedReason reason = KickedReason::TakenOver;
  if (!handshakeGiveUpReason(code, wireRetryable, reason)) return "retry";
  return imrtc::kickedReasonName(reason);
}

}  // namespace

IMRTC_TEST(handshakeGiveUpThreeWay, "握手分流 —— 三类，判据是「谁救得了」而不是「retryable 真假」") {
  // 换一枚票就能好：签名密钥轮换、票被吊销都长这样。
  CHECK_EQ(giveUp(1101), std::string("auth_expired"), "1101 token_invalid 是换票能救的");
  // 回登录页：服务端的吊销名单走的就是 sys.error{1104} + 4403 这一对。
  CHECK_EQ(giveUp(1104), std::string("taken_over"), "1104 kicked_out 该回登录页");
  // 去改配置：换票和重试都救不了 device_id 里的空格。
  CHECK_EQ(giveUp(1004), std::string("config_rejected"), "1004 bad_params 是配置问题");
  CHECK_EQ(giveUp(1006), std::string("config_rejected"), "1006 协议版本不支持也是配置问题");
  CHECK_EQ(giveUp(1106), std::string("config_rejected"), "1106 app_disabled 同样重试无用");

  // 可重试的照常退避重连——**这是防止「一次就放弃」误伤的那一半**。
  CHECK_EQ(giveUp(1102), std::string("retry"), "1102 token_expired 重连时可能已换到新票");
  CHECK_EQ(giveUp(1501), std::string("retry"), "1501 internal 是服务端的临时故障");
}

IMRTC_TEST(handshakeGiveUpLocalCodes,
           "握手分流 —— local 组的码直接放行（那是宿主自己按的 logout，不是服务端的裁决）") {
  /*
    这一条守的是**静默续期**：续期正是「先 logout 再换票」，而 close() 会拿
    2005 invalid_state（它 retryable == false）把在飞的握手结掉。不挡的话
    一次正常的 logout 会被报成「服务端拒了你的参数」，把人踹回登录页。
  */
  CHECK_EQ(giveUp(codeValue(imrtc::ErrorCode::InvalidState), false), std::string("retry"),
           "2005 是 logout 结掉在途握手，不是被拒");
  CHECK_EQ(giveUp(codeValue(imrtc::ErrorCode::SignalingTimeout), false), std::string("retry"),
           "2004 超时照常重连");
  CHECK_EQ(giveUp(codeValue(imrtc::ErrorCode::NetworkUnreachable), false), std::string("retry"),
           "2003 断线照常重连");
  CHECK_EQ(giveUp(codeValue(imrtc::ErrorCode::NotLoggedIn), false), std::string("retry"),
           "2007 也在 local 组");
}

IMRTC_TEST(handshakeGiveUpUnknownCode,
           "握手分流 —— 本端不认识的码信帧上那一位，不信折算后的 1501") {
  /*
    不认识的码在本仓会被折算成 1501 internal，而 1501 是 retryable == true。
    照着折算值判的话，**服务端每加一个新的终局码，客户端就多一种无限重连**——
    本仓漏过 1106 一次，症状正是这个。
  */
  CHECK_EQ(giveUp(1199, false), std::string("config_rejected"),
           "帧上说不可重试，就该一次放弃");
  CHECK_EQ(giveUp(1199, true), std::string("retry"), "帧上说可重试，照常退避");
  // 连帧上都没写：维持「不认识就先退避着」的老行为（wireRetryable 默认 true）。
  CHECK_EQ(giveUp(1199), std::string("retry"), "缺席按可重试处理");
}

IMRTC_TEST(deviceIdCharset, "device_id 校验 —— 协议 §2.5：非空 / ≤64 字节 / [A-Za-z0-9_-]") {
  CHECK_EQ(deviceIdValid("mac-8f3a"), true, "常规的合法值");
  CHECK_EQ(deviceIdValid("Pixel_2_XL"), true, "下划线合法");
  CHECK_EQ(deviceIdValid(std::string(64, 'a')), true, "正好 64 字节");

  CHECK_EQ(deviceIdValid(""), false, "空串不行");
  CHECK_EQ(deviceIdValid(std::string(65, 'a')), false, "65 字节超界");
  // 真机上就是这个：Build.MODEL == "Pixel 2 XL"。
  CHECK_EQ(deviceIdValid("Pixel 2 XL"), false, "空格违反 charset");
  CHECK_EQ(deviceIdValid("mac.8f3a"), false, "点号不在 charset 里");
  CHECK_EQ(deviceIdValid("mac/8f3a"), false, "斜杠不在 charset 里");
  CHECK_EQ(deviceIdValid("设备-1"), false, "非 ASCII 不行");
}
