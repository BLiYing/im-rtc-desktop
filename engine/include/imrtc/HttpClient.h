#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace imrtc {

/**
 * HttpResponse 是一次 HTTP 请求的结果。
 *
 * `error` 非空表示**根本没拿到应答**（DNS / 连接 / TLS / 超时），此时 `status` 无意义；
 * 拿到了应答（哪怕是 401 / 500）`error` 为空，`status` 是 HTTP 状态码。
 */
struct HttpResponse {
  std::int32_t status = 0;
  std::string body;
  std::string error;

  bool failed() const { return !error.empty(); }
};

using HttpCompletion = std::function<void(const HttpResponse&)>;

/**
 * HttpClient 是 HTTP GET 的最小抽象——通话记录查询（`GET /v1/calls`）要用。
 *
 * 与 `Transport` 同一个道理：engine **零第三方依赖**，不该把某个具体的 HTTP 库焊死在
 * 业务逻辑里。URL 怎么拼、应答怎么解、「到底」怎么判都在 engine 里（可单测），
 * 这里只管把一个 GET 发出去、把应答拿回来。
 *
 * **线程**：真实实现在自己的线程上收发，但 `HttpCompletion` **只在 `poll()` 里、在宿主线程上**
 * 被调用（`CallEngine::tick()` 驱动），与 `Transport::poll()` 同一个约定——`CallEngine`
 * 不是线程安全的。实现析构时不许再有回调打进来。
 */
class HttpClient {
public:
  virtual ~HttpClient() = default;

  /**
   * get 发一个 GET，带 `Authorization: Bearer <bearerToken>`（空串则不带）。
   * 结果**恰好一次**经 `done` 回来，且只在 `poll()` 里。
   */
  virtual void get(const std::string& url, const std::string& bearerToken,
                   std::int64_t timeoutMs, HttpCompletion done) = 0;

  /** poll 把攒下的结果投递给 `done`，由 `CallEngine::tick()` 调用。同步实现（测试用假的）什么都不用做。 */
  virtual void poll() {}
};

/** HttpClientFactory 造 HttpClient。测试注入假的，运行期注入真的。 */
using HttpClientFactory = std::function<std::unique_ptr<HttpClient>()>;

}  // namespace imrtc
