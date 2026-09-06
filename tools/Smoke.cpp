#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "imrtc/CallEngine.hpp"

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
 * # 它刻意走 C ABI
 *
 * 用的是 `imrtc/CallEngine.hpp`——那层 header-only 包装**自己也走 C ABI**，
 * 跟 Qt 宿主、C# 宿主走的是同一条路。直接调 `imrtc::CallEngine` 会更省事，
 * 但那样这个工具就证明不了「宿主接得通」，只能证明「我们自己调得通」。
 *
 * 这里用 printf 是刻意的：CONVENTIONS §8 禁的是**业务代码**里直接打印，
 * 而一个 CLI 的全部产出就是它打在终端上的东西。
 */
namespace {

class Printer : public imrtc::capi::Observer {
public:
  bool connected = false;
  bool finished = false;
  bool kicked = false;

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
  void onCallBegin(const std::string& callId, const std::string& roomId,
                   const std::string& role) override {
    std::printf("  ✓ onCallBegin      call=%s role=%s room=%s\n", callId.c_str(), role.c_str(),
                roomId.c_str());
  }
  void onCallEnd(const std::string& callId, const std::string& reason, std::int64_t durationSec,
                 const std::string& endedBy) override {
    std::printf("  ✓ onCallEnd        reason=%s duration=%lld endedBy=%s（call=%s）\n",
                reason.c_str(), static_cast<long long>(durationSec), endedBy.c_str(),
                callId.c_str());
    finished = true;
  }
  void onCallReceived(const std::string& callId, const std::string& caller,
                      const std::vector<std::string>&, const std::string& mediaType,
                      bool) override {
    std::printf("  ✓ onCallReceived   call=%s caller=%s media=%s\n", callId.c_str(),
                caller.c_str(), mediaType.c_str());
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

  std::printf("→ SDK %s（经 C ABI，与 Qt / C# 宿主同一条路）\n", imrtc_v1_version());

  Printer printer;
  imrtc::capi::Engine engine(url, deviceId, "desktop-smoke/0.1.0");
  if (!engine.valid()) {
    std::printf("✗ 造不出 Engine：%s\n", engine.lastError().name());
    return 1;
  }
  engine.setObserver(&printer);

  std::printf("→ login %s（device=%s）\n", url.c_str(), deviceId.c_str());
  const imrtc::capi::Error loginError = engine.login(token);
  if (!loginError.ok()) {
    std::printf("✗ login 失败：%s\n", loginError.name());
    return 1;
  }

  bool called = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline && !printer.finished) {
    engine.tick();
    if (printer.connected && !called) {
      called = true;
      std::printf("→ call [%s] audio 1v1\n", callee.c_str());
      engine.call({callee}, "audio", false);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  engine.setObserver(nullptr);
  engine.logout();

  if (!printer.connected) {
    std::printf("✗ 没连上。服务端起来了吗？（./scripts/dev.sh status）\n");
    return 1;
  }
  if (printer.kicked) {
    std::printf("✗ 被踢下线了——这不算走通。\n");
    return 1;
  }
  if (!printer.finished) {
    std::printf("✗ 连上了，但 20 秒内没等到通话的终局。\n"
                "   呼的人如果在线，服务端会振铃到超时（默认 30 秒），换一个不在线的 uid 再试。\n");
    return 1;
  }
  std::printf("✓ 走通：握手 → 拨号 → 终局（**全程经 C ABI**）。\n");
  return 0;
}
