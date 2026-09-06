#!/usr/bin/env bash
# test.sh —— 本仓**唯一**的测试入口（CLAUDE.md「完成的定义」第 2 条）。
#
#   ./scripts/test.sh              配置 + 编译 + 跑测试（默认 macos-clang / windows-msvc）
#   IMRTC_PRESET=macos-clang-release ./scripts/test.sh
#
# 五步，任一步失败即整体失败：
#   1) 体量门禁（防「上帝类」，CONVENTIONS §3）
#   2) CMake 配置
#   3) 编译（-Wall -Wextra -Wconversion，警告当错看待）
#   4) 单测 + 一致性向量
#   5) ABI 导出面体检（只许导出 imrtc_v1_*，CONVENTIONS §2 红线 1）
set -euo pipefail

cd "$(dirname "$0")/.."

case "$(uname -s)" in
  Darwin) DEFAULT_PRESET="macos-clang" ;;
  MINGW*|MSYS*|CYGWIN*) DEFAULT_PRESET="windows-msvc" ;;
  *) DEFAULT_PRESET="macos-clang" ;;
esac
PRESET="${IMRTC_PRESET:-$DEFAULT_PRESET}"

echo "== 1/5 体量门禁 =="
./scripts/check-file-size.sh

echo ""
echo "== 2/5 CMake 配置（preset: ${PRESET}）=="
cmake --preset "$PRESET"

echo ""
echo "== 3/5 编译 =="
cmake --build --preset "$PRESET"

echo ""
echo "== 4/5 单测 + 一致性向量 =="
# 直接跑可执行文件而不是 ctest：一致性向量的失败信息（哪个用例第几步、期望什么）
# 才是排障时真正要看的东西，ctest 的摘要会把它折叠掉。
"./build/${PRESET}/tests/im_rtc_engine_tests"

echo ""
echo "== 5/5 ABI 导出面体检 =="
# 离线构建（-DIMRTC_WITH_IX_TRANSPORT=OFF）不出动态库，这一步自然跳过。
if [ -f "build/${PRESET}/capi/libim_rtc_engine_capi.dylib" ]; then
  ./scripts/check-abi.sh
else
  echo "  （没有动态库，跳过——离线构建关掉了 capi）"
fi

echo ""
echo "全部通过。**注意**：这只证明了 $(uname -s)；Windows 侧未验证。"
