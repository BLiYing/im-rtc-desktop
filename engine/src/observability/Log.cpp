#include "imrtc/Log.h"

#include <cstdio>
#include <mutex>
#include <sstream>
#include <utility>

namespace imrtc {
namespace {

/**
 * State 是进程级的日志状态。
 *
 * 用函数内静态而不是全局变量：全局变量的构造顺序在跨翻译单元时是未定义的，
 * 而 `log()` 可能在别的静态对象的构造函数里被调到（宿主把 Engine 放成全局就会）。
 */
struct State {
  std::mutex mutex;
  LogSink sink;
  LogLevel level = LogLevel::Info;
};

State& state() {
  static State instance;
  return instance;
}

/**
 * writeConsole 是内置那一路。**这里是 CONVENTIONS §8 「禁止直接打印」的唯一豁免**——
 * 与服务端的 `internal/observability`、Web 的 `logger.ts` 同一个位置。
 *
 * 走 stderr 不走 stdout：宿主的 stdout 可能是它自己的产出（CLI 管道），
 * 把日志混进去会毁掉它。
 */
void writeConsole(LogLevel level, const std::string& message, const LogFields& fields) {
  std::ostringstream line;
  line << "[imrtc] " << logLevelName(level) << ' ' << message;
  for (const auto& field : fields) line << ' ' << field.first << '=' << field.second;
  line << '\n';
  const std::string text = line.str();
  std::fputs(text.c_str(), stderr);
}

}  // namespace

const char* logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Warn: return "warn";
    case LogLevel::Error: return "error";
    case LogLevel::Info: break;
  }
  return "info";
}

void setLogSink(LogSink sink) {
  State& current = state();
  std::lock_guard<std::mutex> guard(current.mutex);
  current.sink = std::move(sink);
}

void setLogLevel(LogLevel level) {
  State& current = state();
  std::lock_guard<std::mutex> guard(current.mutex);
  current.level = level;
}

LogLevel logLevel() {
  State& current = state();
  std::lock_guard<std::mutex> guard(current.mutex);
  return current.level;
}

void log(LogLevel level, const std::string& message, LogFields fields) {
  State& current = state();
  LogSink sink;
  {
    std::lock_guard<std::mutex> guard(current.mutex);
    if (static_cast<std::int32_t>(level) < static_cast<std::int32_t>(current.level)) return;
    // **拷一份再出锁**：sink 是宿主的代码，可能回调进 engine、也可能自己再打日志。
    // 持锁调它就是一个自死锁，而且是那种「只在宿主特定写法下才复现」的。
    sink = current.sink;
  }

  writeConsole(level, message, fields);
  // fan-out：内置那一路照旧，宿主 sink 只是多一个出口（见 setLogSink 的注释）。
  if (sink) sink(level, message, fields);
}

std::string redact(const std::string& secret) {
  const std::string head = secret.substr(0, 6);
  return head + "…(len=" + std::to_string(secret.size()) + ")";
}

std::string redactSdp(const std::string& sdp) {
  std::size_t lines = 0;
  std::string media;
  std::istringstream stream(sdp);
  std::string line;
  while (std::getline(stream, line)) {
    ++lines;
    // 只留 m= 行的媒体类型：够判断「协商的是音频还是音视频」，而不摊开传输面。
    if (line.rfind("m=", 0) != 0) continue;
    const std::size_t space = line.find(' ');
    if (!media.empty()) media += ",";
    media += line.substr(0, space == std::string::npos ? line.size() : space);
  }
  return "sdp(lines=" + std::to_string(lines) + ", " + media + ")";
}

std::string redactCandidate(const std::string& candidate) {
  /*
    只留传输协议与候选类型（`udp/host`），地址与端口一概不留。

    格式是 `candidate:<foundation> <component> <transport> <priority> <ip> <port> typ <type> …`
    （RFC 5245 §15.1）。取不到就写 unknown——**脱敏助手绝不能因为输入不合预期就抛**，
    它被调用的地方全是「正在记录一个异常」。
  */
  std::istringstream stream(candidate);
  std::string token;
  std::vector<std::string> parts;
  while (stream >> token) parts.push_back(token);

  const std::string transport = parts.size() > 2 ? parts[2] : "unknown";
  std::string type = "unknown";
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    if (parts[i] == "typ") {
      type = parts[i + 1];
      break;
    }
  }
  return "candidate(" + transport + "/" + type + ")";
}

}  // namespace imrtc
