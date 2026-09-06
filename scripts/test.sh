#!/usr/bin/env bash
# test.sh —— 本仓**唯一**的测试入口（CLAUDE.md「完成的定义」第 2 条）。
#
#   ./scripts/test.sh              配置 + 编译 + 跑测试（默认 macos-clang / windows-msvc）
#   IMRTC_PRESET=macos-clang-release ./scripts/test.sh
#
# 四步，任一步失败即整体失败：
#   1) 体量门禁（防「上帝类」，CONVENTIONS §3）
#   2) CMake 配置
#   3) 编译（-Wall -Wextra -Wconversion，警告当错看待）
#   4) 单测 + 一致性向量
set -euo pipefail

cd "$(dirname "$0")/.."

case "$(uname -s)" in
  Darwin) DEFAULT_PRESET="macos-clang" ;;
  MINGW*|MSYS*|CYGWIN*) DEFAULT_PRESET="windows-msvc" ;;
  *) DEFAULT_PRESET="macos-clang" ;;
esac
PRESET="${IMRTC_PRESET:-$DEFAULT_PRESET}"

echo "== 1/4 体量门禁 =="
./scripts/check-file-size.sh

echo ""
echo "== 2/4 CMake 配置（preset: ${PRESET}）=="
cmake --preset "$PRESET"

echo ""
echo "== 3/4 编译 =="
cmake --build --preset "$PRESET"

echo ""
echo "== 4/4 单测 + 一致性向量 =="
# 直接跑可执行文件而不是 ctest：一致性向量的失败信息（哪个用例第几步、期望什么）
# 才是排障时真正要看的东西，ctest 的摘要会把它折叠掉。
"./build/${PRESET}/tests/im_rtc_engine_tests"

echo ""
echo "全部通过。**注意**：这只证明了 $(uname -s)；Windows 侧未验证。"
