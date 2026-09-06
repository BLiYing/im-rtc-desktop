#!/usr/bin/env bash
# demo-shots.sh —— 把 Demo 的各个界面态离线渲染成 PNG。
#
#   ./scripts/demo-shots.sh [输出目录]
#
# **不需要服务端**：喂的是假数据，只验证版式与主题令牌，不验证任何协议行为
# （那些由 scripts/test.sh 与 scripts/smoke.sh 负责）。
# 中英两套都出，正好把语言切换也验了。
set -euo pipefail

cd "$(dirname "$0")/.."

PRESET="${IMRTC_PRESET:-macos-clang}"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/6.8.3/macos}"
OUT="${1:-build/demo-shots}"

BIN="./build/${PRESET}/demo/imrtc_demo_shots"
if [ ! -x "$BIN" ]; then
  cmake --preset "$PRESET" -DIMRTC_BUILD_DEMO=ON -DCMAKE_PREFIX_PATH="$QT_PREFIX"
  cmake --build --preset "$PRESET" --target imrtc_demo_shots
fi

rm -rf "$OUT"
"$BIN" "$OUT" zh
"$BIN" "$OUT" en
echo ""
echo "共 $(ls "$OUT" | wc -l | tr -d ' ') 张，在 $OUT"
