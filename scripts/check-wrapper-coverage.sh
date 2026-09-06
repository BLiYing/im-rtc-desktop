#!/usr/bin/env bash
# check-wrapper-coverage.sh —— C++ 包装头必须接满 C 回调表。
#
# 为什么要有这个脚本：包装头（capi/include/imrtc/CallEngine.hpp）曾经悄悄地
# 只接了 25 个回调里的 14 个。少接一个不会编译失败、不会跑挂，只会让宿主
# **永远收不到那个事件**——而它看起来像是「引擎没发」。这种缺口靠人眼看不出来，
# 只能靠数。
#
# 规则：imrtc_c.h 里每一个 `void (*on_xxx)(...)`，
# 在 CallEngine.hpp 里都必须有一行 `table.on_xxx = ...`。
set -euo pipefail

cd "$(dirname "$0")/.."

C_HEADER="capi/include/imrtc/imrtc_c.h"
CXX_HEADER="capi/include/imrtc/CallEngine.hpp"

# 从 C 头里抠出回调名：`  void (*on_call_begin)(void* user_data, ...);` -> on_call_begin
declare -a MISSING=()
COUNT=0
while IFS= read -r name; do
  COUNT=$((COUNT + 1))
  if ! grep -q "table\.${name}[[:space:]]*=" "$CXX_HEADER"; then
    MISSING+=("$name")
  fi
done < <(grep -o 'void (\*on_[a-z_]*)' "$C_HEADER" | sed 's/void (\*//; s/)//')

if [ "$COUNT" -eq 0 ]; then
  echo "  ✗ 在 ${C_HEADER} 里一个回调都没找到——脚本的正则该修了，不是真的没有"
  exit 1
fi

if [ "${#MISSING[@]}" -ne 0 ]; then
  echo "  ✗ 包装头漏接了 ${#MISSING[@]} 个回调（共 ${COUNT} 个）："
  for name in "${MISSING[@]}"; do
    echo "      ${name}"
  done
  echo ""
  echo "  宿主用 imrtc::capi::Observer 时收不到这些事件。"
  echo "  在 ${CXX_HEADER} 里补上：虚函数 + table.<name> 赋值 + 静态跳板。"
  exit 1
fi

echo "  ✓ ${COUNT} 个回调全部接线。"
