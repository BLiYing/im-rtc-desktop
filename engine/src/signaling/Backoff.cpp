#include "imrtc/Backoff.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "imrtc/Transport.h"

namespace imrtc {

bool shouldReconnect(int code) {
  return code != closecode::kNormal && code != closecode::kBadProtocol &&
         code != closecode::kKickedOut;
}

const std::vector<std::int64_t>& backoffStepsMs() {
  static const std::vector<std::int64_t> kSteps = {1000, 2000, 4000, 8000, 15000, 30000};
  return kSteps;
}

double defaultRandom01() {
  // thread_local：Connection 的定时器可能落在别的线程上，共享一个引擎会有数据竞争。
  static thread_local std::mt19937 engine{std::random_device{}()};
  static thread_local std::uniform_real_distribution<double> distribution(0.0, 1.0);
  return distribution(engine);
}

std::int64_t backoffDelayMs(int attempt, const Random01& random) {
  const std::vector<std::int64_t>& steps = backoffStepsMs();
  const int lastIndex = static_cast<int>(steps.size()) - 1;
  const std::size_t index = static_cast<std::size_t>(std::min(std::max(attempt, 0), lastIndex));
  const double base = static_cast<double>(steps[index]);
  const double roll = random ? random() : defaultRandom01();
  const double jitter = base * kJitterRatio * (roll * 2.0 - 1.0);
  return std::max<std::int64_t>(0, static_cast<std::int64_t>(std::llround(base + jitter)));
}

}  // namespace imrtc
