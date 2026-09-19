#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "imrtc/HttpClient.h"

namespace ix {
class HttpClient;
}

namespace imrtc {

/**
 * IxHttpClient 是 `HttpClient` 的真实实现，底层用 IXWebSocket 自带的 `ix::HttpClient`（异步模式）。
 *
 * 与 `IxTransport` 同一套做法：IX 在**自己的后台线程**上回调，而 `CallEngine` 不是线程安全的，
 * 所以应答先排进队列，由 `poll()`（`CallEngine::tick()` 调用）在宿主线程上放出来。
 *
 * - **不阻塞引擎线程**：`get()` 只是把请求交给 IX 的工作线程。
 * - **析构不再有回调**：先停 IX 的工作线程（join），再丢掉队列里没放出去的应答。
 * - 队列有界（CONVENTIONS §6）：宿主一直不 tick 时，满了丢最旧的应答——通话记录查询是低频的，
 *   正常使用碰不到这条线。
 */
class IxHttpClient : public HttpClient {
public:
  IxHttpClient();
  ~IxHttpClient() override;

  IxHttpClient(const IxHttpClient&) = delete;
  IxHttpClient& operator=(const IxHttpClient&) = delete;

  void get(const std::string& url, const std::string& bearerToken, std::int64_t timeoutMs,
           HttpCompletion done) override;
  void poll() override;

private:
  static constexpr std::size_t kMaxQueuedResults = 64;

  void enqueue(HttpCompletion done, HttpResponse response);

  std::unique_ptr<ix::HttpClient> client_;
  std::mutex mutex_;
  std::deque<std::pair<HttpCompletion, HttpResponse>> queue_;
};

}  // namespace imrtc
