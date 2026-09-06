#!/usr/bin/env bash
# check-abi.sh —— **红线 1 的守门人**：动态库只许导出 imrtc_v1_*。
#
#   ./scripts/check-abi.sh [动态库路径]
#
# 为什么要有这个脚本：-fvisibility=hidden 挡不住 libc++ 的 RTTI（它们是 weak-def，
# 以默认可见性发出），实测会漏 42 个 std::function 的 typeinfo 出去。
# 它们弱符号、会与宿主的合并，实践上不出事——但「实践上不出事」不是 ABI 契约，
# 而且下一次谁在 capi 里多用一个模板，漏出去的就换一批。这条只能机器守。
set -euo pipefail

cd "$(dirname "$0")/.."

PRESET="${IMRTC_PRESET:-macos-clang}"
LIB="${1:-build/${PRESET}/capi/libim_rtc_engine_capi.dylib}"

if [ ! -f "$LIB" ]; then
  echo "找不到动态库：$LIB"
  echo "（先 cmake --build --preset ${PRESET}；离线关掉了 transport 的话这一步会被跳过）"
  exit 2
fi

case "$(uname -s)" in
  Darwin)
    # dyld_info -exports 给的是**导出表**，不是符号表。nm -g 会把没导出的全局符号
    # 也列出来，用它判会得出错误结论（第一次就这么误判过）。
    EXPORTS=$(dyld_info -exports "$LIB" | tail -n +4 | awk '{print $2}' | grep -v '^$')
    ;;
  *)
    echo "本脚本只在 macOS 上有实现。Windows 侧由 __declspec(dllexport) 天然收口，"
    echo "但仍应在集成方那边用 dumpbin /exports 核一次。"
    exit 0
    ;;
esac

TOTAL=$(printf '%s\n' "$EXPORTS" | grep -c . || true)
OURS=$(printf '%s\n' "$EXPORTS" | grep -c '^_imrtc_v1_' || true)
STRAY=$(printf '%s\n' "$EXPORTS" | grep -v '^_imrtc_v1_' || true)

echo "== ABI 导出面体检 =="
echo "  导出总数：${TOTAL}，其中 imrtc_v1_*：${OURS}"

if [ -n "$STRAY" ]; then
  echo ""
  echo "  ✗ 漏了这些非 imrtc_v1_ 的符号："
  printf '%s\n' "$STRAY" | sed 's/^/      /' | head -20
  echo ""
  echo "  修法：把它们挡在 capi/exported_symbols.txt 之外，或者别在 capi 边界上"
  echo "  暴露会产出 RTTI 的类型。**不要放宽这条**——它是「宿主换个编译器也能用」的全部依据。"
  exit 1
fi

if [ "$OURS" -lt 20 ]; then
  echo "  ✗ 只导出了 ${OURS} 个符号，太少了——白名单是不是把该导的也挡住了？"
  exit 1
fi

echo "  ✓ 干净：只导出 imrtc_v1_*。"
