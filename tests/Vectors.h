#pragma once

#include <string>

#include "imrtc/Json.h"

namespace imtest {

/**
 * 一致性向量的定位：**只读 `im-rtc-server/docs/conformance/` 那一份**。
 *
 * 禁止把向量拷进本仓（conformance/README.md 明说）——一拷贝就会漏同步，
 * 而向量存在的全部意义就是防止五端漂移。
 *
 * 找不到时**抛错而不是跳过**：一个被静默跳过的一致性测试比没有测试更糟，
 * 它会让人以为五端是对齐的。
 */
imrtc::Json loadVector(const std::string& name);

/**
 * expectSubset 做**子集比对**：向量写了哪些键就只比哪些键。
 * 数组是**有序全等**（长度也要一样）——`send` / `emit` 的顺序就是契约的一部分。
 */
void expectSubset(const imrtc::Json& actual, const imrtc::Json& want, const std::string& path);

}  // namespace imtest
