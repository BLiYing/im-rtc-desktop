#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "imrtc/CallEngine.h"
#include "imrtc/IxTransport.h"

/**
 * imrtc_smoke —— **对着真服务端跑一遍**的联调小工具。
 *
 *   ./scripts/smoke.sh                 # 自动取 demo 票并跑
 *   ./build/<preset>/tools/imrtc_smoke <ws-url> <token> <device-id> [callee]
 *
 * 它不是单元测试（那些不需要网络，见 tests/）。它回答的是另一个问题：
 * **这套东西接上真的服务端还成立吗**——握手、心跳、请求应答、事件分发、
 * 以及「呼一个不在线的人」这条完整的失败路径。
 *
 * 这里用 printf 是刻意的：CONVENTIONS §8 禁的是**业务代码**里直接打印，
 * 而一个 CLI 的全部产出就是它打在终端上的东西。
 */
namespace {

/** Printer 把回调原样打出来，好和服务端日志对着看。 */
class Printer : public imrtc::CallEngineObserver {
public:
  bool finished = false;

  void onConnected(const std::string& sessionId, bool resumed) override {
    std::printf("  ✓ onConnected      session=%s resumed=%s\n", sessionId.c_str(),
                resumed ? "true" : "false");
    connected = true;
  }
  void onDisconnected() override { std::printf("  · onDisconnected\n"); }
  void onKickedOut() override {
    std::printf("  ✗ onKickedOut（被踢或鉴权用尽）\n");
    finished = true;
    kicked = true;
  }
  void onError(std::int32_t code, const std::string& name, const std::string& forType) override {
    std::printf("  ! onError          %d/%s for=%s\n", code, name.c_str(), forType.c_str());
  }
  void onCallBegin(const imrtc::CallBegin& begin) override {
    std::printf("  ✓ onCallBegin      call=%s role=%s room=%s\n", begin.callId.c_str(),
                begin.role.c_str(), begin.roomId.c_str());
  }
  void onCallEnd(const imrtc::CallEnd& end) override {
    std::printf("  ✓ onCallEnd        reason=%s duration=%lld endedBy=%s\n", end.reason.c_str(),
                static_cast<long long>(end.durationSec), end.endedBy.c_str());
    finished = true;
  }
  void onCallReceived(const imrtc::CallInvite& invite) override {
    std::printf("  ✓ onCallReceived   call=%s caller=%s\n", invite.callId.c_str(),
                invite.caller.c_str());
  }
  void onUserAccept(const std::string& uid) override {
    std::printf("  ✓ onUserAccept     uid=%s\n", uid.c_str());
  }
  void onRoomJoined(const std::string& roomId) override {
    std::printf("  ✓ onRoomJoined     room=%s\n", roomId.c_str());
  }
  void onRoomLeft(const std::string& roomId) override {
    std::printf("  · onRoomLeft       room=%s\n", roomId.c_str());
  }

  bool connected = false;
  bool kicked = false;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf("用法：imrtc_smoke <ws-url> <token> <device-id> [callee]\n");
    return 2;
  }
  const std::string url = argv[1];
  const std::string token = argv[2];
  const std::string deviceId = argv[3];
  // 默认呼一个必然不在线的 uid：服务端立刻回 call.ended{offline}，
  // 一趟就把「请求 → 应答 → 事件 → 终局」全走完，不必等 30 秒振铃超时。
  const std::string callee = argc > 4 ? argv[4] : "nobody-offline";

  imrtc::CallEngineOptions options;
  options.url = url;
  options.deviceId = deviceId;
  options.sdk = "desktop-smoke/0.0.1";
  options.transportFactory = []() -> std::unique_ptr<imrtc::Transport> {
    return std::unique_ptr<imrtc::Transport>(new imrtc::IxTransport());
  };

  const auto printer = std::make_shared<Printer>();
  imrtc::CallEngine engine(options);
  engine.setObserver(printer);

  std::printf("→ login %s（device=%s）\n", url.c_str(), deviceId.c_str());
  engine.login(token);

  bool called = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline && !printer->finished) {
    engine.tick();
    if (printer->connected && !called) {
      called = true;
      std::printf("→ call [%s] audio 1v1\n", callee.c_str());
      engine.call({callee}, "audio", false);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  engine.logout();
  if (!printer->connected) {
    std::printf("✗ 没连上。服务端起来了吗？（./scripts/dev.sh status）\n");
    return 1;
  }
  if (printer->kicked) {
    std::printf("✗ 被踢下线了——这不算走通。\n");
    return 1;
  }
  if (!printer->finished) {
    std::printf("✗ 连上了，但 20 秒内没等到通话的终局。\n"
                "   呼的人如果在线，服务端会振铃到超时（默认 30 秒），"
                "换一个不在线的 uid 再试。\n");
    return 1;
  }
  std::printf("✓ 走通：握手 → 拨号 → 终局。\n");
  return 0;
}
