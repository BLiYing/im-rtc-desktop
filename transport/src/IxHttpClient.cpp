#include "imrtc/IxHttpClient.h"

#include <utility>

#include "ixwebsocket/IXHttpClient.h"
#include "ixwebsocket/IXNetSystem.h"

namespace imrtc {
namespace {

/** 与 IxTransport.cpp 里那份同一个道理：进程内初始化一次网络子系统（Windows 的 WSAStartup）。 */
void ensureNetSystem() {
  static const bool kInitialized = []() {
    ix::initNetSystem();
    return true;
  }();
  (void)kInitialized;
}

int toSeconds(std::int64_t timeoutMs) {
  const std::int64_t seconds = (timeoutMs + 999) / 1000;
  return static_cast<int>(seconds < 1 ? 1 : seconds);
}

std::string describe(const ix::HttpResponse& response) {
  std::string text = response.errorMsg.empty() ? "http error" : response.errorMsg;
  return text + " (code " + std::to_string(static_cast<int>(response.errorCode)) + ")";
}

}  // namespace

IxHttpClient::IxHttpClient() {
  ensureNetSystem();
  client_.reset(new ix::HttpClient(true));  // async：IX 自己起一条工作线程
}

IxHttpClient::~IxHttpClient() {
  // ~ix::HttpClient 会 join 工作线程；返回之后不会再有回调打进来，队列里剩下的一起丢掉。
  client_.reset();
}

void IxHttpClient::get(const std::string& url, const std::string& bearerToken, std::int64_t timeoutMs,
                       HttpCompletion done) {
  ix::HttpRequestArgsPtr args = client_->createRequest(url, ix::HttpClient::kGet);
  if (!bearerToken.empty()) args->extraHeaders["Authorization"] = "Bearer " + bearerToken;
  args->extraHeaders["Accept"] = "application/json";
  args->connectTimeout = toSeconds(timeoutMs);
  args->transferTimeout = toSeconds(timeoutMs);
  // 没编 zlib（USE_ZLIB=OFF），发 Accept-Encoding: gzip 只会收回解不开的应答。
  args->compress = false;
  args->followRedirects = false;

  auto completion = std::make_shared<HttpCompletion>(std::move(done));
  const bool accepted = client_->performRequest(args, [this, completion](const ix::HttpResponsePtr& reply) {
    HttpResponse response;
    if (!reply || reply->errorCode != ix::HttpErrorCode::Ok) {
      response.error = reply ? describe(*reply) : "no response";
    } else {
      response.status = reply->statusCode;
      response.body = reply->body;
    }
    enqueue(*completion, std::move(response));
  });
  if (!accepted) {
    HttpResponse response;
    response.error = "request not accepted";
    enqueue(*completion, std::move(response));
  }
}

void IxHttpClient::enqueue(HttpCompletion done, HttpResponse response) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (queue_.size() >= kMaxQueuedResults) queue_.pop_front();
  queue_.emplace_back(std::move(done), std::move(response));
}

void IxHttpClient::poll() {
  std::deque<std::pair<HttpCompletion, HttpResponse>> ready;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    ready.swap(queue_);
  }
  for (auto& item : ready) {
    if (item.first) item.first(item.second);
  }
}

}  // namespace imrtc
