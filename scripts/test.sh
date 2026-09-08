#!/usr/bin/env bash
# test.sh —— 本仓**唯一**的测试入口（CLAUDE.md「完成的定义」第 2 条）。
#
#   ./scripts/test.sh              配置 + 编译 + 跑测试（默认 macos-clang / windows-msvc）
#   IMRTC_PRESET=macos-clang-release ./scripts/test.sh
#
# 八步，任一步失败即整体失败：
#   1) 体量门禁（防「上帝类」，CONVENTIONS §3）
#   2) 日志纪律（CONVENTIONS §8）——直接打印 / 热路径 / 脱敏
#   3) CMake 配置
#   4) 编译（-Wall -Wextra -Wconversion，警告当错看待）
#   5) 单测 + 一致性向量
#   6) ABI 导出面体检（只许导出 imrtc_v1_*，CONVENTIONS §2 红线 1）
#   7) C++ 包装头必须接满 C 回调表（漏接不会报错，只会让宿主收不到事件）
#   8) Demo 界面测试（**只在打开了 IMRTC_BUILD_DEMO 时存在**，需要 Qt）
set -euo pipefail

cd "$(dirname "$0")/.."

case "$(uname -s)" in
  Darwin) DEFAULT_PRESET="macos-clang" ;;
  MINGW*|MSYS*|CYGWIN*) DEFAULT_PRESET="windows-msvc" ;;
  *) DEFAULT_PRESET="macos-clang" ;;
esac
PRESET="${IMRTC_PRESET:-$DEFAULT_PRESET}"

echo "== 1/8 体量门禁 =="
./scripts/check-file-size.sh

echo ""
echo "== 2/8 日志纪律 =="
# 闸门自己回归了会静默放行，所以先让它自检一次再上岗（LOGGING.md §5）。
./scripts/check-logging.sh --selftest
./scripts/check-logging.sh

echo ""
echo "== 3/8 CMake 配置（preset: ${PRESET}）=="
cmake --preset "$PRESET"

echo ""
echo "== 4/8 编译 =="
cmake --build --preset "$PRESET"

echo ""
echo "== 5/8 单测 + 一致性向量 =="
# 直接跑可执行文件而不是 ctest：一致性向量的失败信息（哪个用例第几步、期望什么）
# 才是排障时真正要看的东西，ctest 的摘要会把它折叠掉。
"./build/${PRESET}/tests/im_rtc_engine_tests"

echo ""
echo "== 6/8 ABI 导出面体检 =="
# 离线构建（-DIMRTC_WITH_IX_TRANSPORT=OFF）不出动态库，这一步自然跳过。
if [ -f "build/${PRESET}/capi/libim_rtc_engine_capi.dylib" ]; then
  ./scripts/check-abi.sh
else
  echo "  （没有动态库，跳过——离线构建关掉了 capi）"
fi

echo ""
echo "== 7/8 C++ 包装头回调覆盖 =="
./scripts/check-wrapper-coverage.sh

echo ""
echo "== 8/8 Demo 界面测试 =="
# Demo 默认不构建，所以这一步通常是跳过的。有它的时候必须跑：
# 它守的是「看代码看不出来、跑真服务端才暴露」的那类规则。
# **不要写死某一个文件名**：测试目标是按用例分文件的，写死会在改名后
# 静默跑一个过时的二进制（踩过一次）。这里遍历，一个都不许漏。
found=0
for t in "build/${PRESET}/demo/"imrtc_demo_*_test; do
  [ -x "$t" ] || continue
  found=$((found + 1))
  # **不能 offscreen**：原生子窗口那组要真窗口才有 backing scale。
  "$t"
done
if [ "$found" -eq 0 ]; then
  echo "  （没构建 Demo，跳过——打开 -DIMRTC_BUILD_DEMO=ON 才有）"
else
  echo "  跑了 ${found} 组。"
fi

echo ""
echo "全部通过。**注意**：这只证明了 $(uname -s)；Windows 侧未验证。"
