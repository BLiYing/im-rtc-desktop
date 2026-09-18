#!/usr/bin/env bash
# package.sh —— 打公网发布包：`dist/imrtc-desktop-<版本>-macos.zip`。
#
#   ./scripts/package.sh
#
# 只做 macOS（这版不交付 Windows，用户已确认）。用 release preset
# （macos-clang-release，universal）配置 + 编译，`cmake --install` 到
# `dist/imrtc-desktop-<版本>-macos/`，再打成同名 zip。
#
# 版本号**只有一处来源**：`engine/include/imrtc/Version.h` 的 `kSdkVersion`
# （C ABI 的 `imrtc_v1_version()` 与顶层 CMakeLists.txt 的 IMRTC_VERSION 都从它抠出来）。
# 这里再抠一遍只是给 zip 文件名用，抠完立刻跟 CMake 配置期打出来的那行核对一致，
# 免得脚本这份哪天手滑改了正则、悄悄跟 CMake 那份对不上。
#
# 装出来的布局（第三方解压后 `find_package(imrtc)` 直接用）：
#   LICENSE（MIT）
#   include/imrtc/imrtc_c.h
#   include/imrtc/CallEngine.hpp
#   include/imrtc/CallEngineTypes.hpp（CallEngine.hpp 拆出来的值类型，被它 include）
#   lib/libim_rtc_engine_capi.dylib
#   lib/cmake/imrtc/imrtcConfig.cmake …
#
# 打完包做几项核对，任一项不过整体失败：
#   ① otool -D 是 @rpath 名  ② lipo -archs 是 x86_64 + arm64  ③ 导出符号只有 imrtc_v1_*（复用 check-abi.sh）
#   ④ LICENSE 在装出来的目录里，且 zip 里也确实带着它
set -euo pipefail

cd "$(dirname "$0")/.."

if [ "$(uname -s)" != "Darwin" ]; then
  echo "package.sh 只在 macOS 上跑（本仓这版不交付 Windows）。" >&2
  exit 1
fi

PRESET="macos-clang-release"

VERSION=$(sed -nE 's/.*kSdkVersion\[\] = "([0-9]+\.[0-9]+\.[0-9]+)".*/\1/p' \
  engine/include/imrtc/Version.h)
if [ -z "$VERSION" ]; then
  echo "解析不到版本号：engine/include/imrtc/Version.h 的 kSdkVersion" >&2
  exit 1
fi

echo "== 1/5 配置（preset: ${PRESET}，universal x86_64+arm64）=="
CONFIGURE_LOG=$(mktemp)
trap 'rm -f "$CONFIGURE_LOG"' EXIT
cmake --preset "$PRESET" | tee "$CONFIGURE_LOG"

# CMake 那份版本号（顶层 CMakeLists.txt 用 file(STRINGS) 从同一个 Version.h 抠出来的）
# 打在配置日志里；两份对不上说明这个脚本的正则和 CMake 的正则至少有一个漂了。
if ! grep -q "IMRTC_VERSION = ${VERSION}（" "$CONFIGURE_LOG"; then
  echo "版本号核对失败：脚本抠出 ${VERSION}，CMake 配置日志里没看到同一行。" >&2
  exit 1
fi
echo "  版本号核对通过：${VERSION}（脚本与 CMake 各抠一遍，一致）"

echo ""
echo "== 2/5 编译（只编交付目标 im_rtc_engine_capi，含其依赖 engine/transport_ix）=="
cmake --build --preset "$PRESET" --target im_rtc_engine_capi

DIST_NAME="imrtc-desktop-${VERSION}-macos"
DIST_DIR="dist/${DIST_NAME}"
ZIP_PATH="dist/${DIST_NAME}.zip"

echo ""
echo "== 3/5 安装到 ${DIST_DIR} =="
rm -rf "$DIST_DIR"
cmake --install "build/${PRESET}" --prefix "$(pwd)/${DIST_DIR}"

DYLIB="${DIST_DIR}/lib/libim_rtc_engine_capi.dylib"
if [ ! -f "$DYLIB" ]; then
  echo "装出来的目录里没有 ${DYLIB}" >&2
  exit 1
fi

# MIT LICENSE（仓根 LICENSE 的原样拷贝，capi/CMakeLists.txt 的 install(FILES ... DESTINATION .)）
# 必须在包根——第三方解压第一眼就该看到协议，不是翻进 include/ 或 lib/ 才找得到。
if [ ! -f "${DIST_DIR}/LICENSE" ]; then
  echo "装出来的目录里没有 ${DIST_DIR}/LICENSE（capi/CMakeLists.txt 的 install(FILES LICENSE) 没生效？）" >&2
  exit 1
fi

echo ""
echo "== 4/5 核对装出来的 dylib =="

INSTALL_NAME=$(otool -D "$DYLIB" | tail -n1)
echo "  install_name: ${INSTALL_NAME}"
if [ "$INSTALL_NAME" != "@rpath/libim_rtc_engine_capi.dylib" ]; then
  echo "  ✗ install_name 不是 @rpath/libim_rtc_engine_capi.dylib" >&2
  exit 1
fi

ARCHS=$(lipo -archs "$DYLIB")
echo "  archs: ${ARCHS}"
if ! { echo "$ARCHS" | grep -qw x86_64 && echo "$ARCHS" | grep -qw arm64; }; then
  echo "  ✗ 不是 x86_64 + arm64 universal（实际：${ARCHS}）" >&2
  exit 1
fi

echo "  导出符号（复用 scripts/check-abi.sh）："
./scripts/check-abi.sh "$DYLIB"

echo "  LICENSE: 有（MIT，${DIST_DIR}/LICENSE）"

echo ""
echo "== 5/5 打 zip =="
( cd dist && rm -f "${DIST_NAME}.zip" && zip -rq -X "${DIST_NAME}.zip" "${DIST_NAME}" )
echo "  ${ZIP_PATH}（$(du -h "$ZIP_PATH" | cut -f1)）"

# zip 里确实带着 LICENSE，不只是装出来的目录有——两者理论上该一致，但 zip 步骤独立，
# 万一以后有人改了 zip 那行的打包范围（比如换成 zip -x 排除了什么），这里能第一时间炸出来。
#
# **先整份捕获再 grep，不要 `unzip -l ... | grep -q ...` 直接接管道**：`grep -q` 一找到匹配就
# 提前退出、不再读管道，`unzip` 这时还在写后面的文件列表，会收到 SIGPIPE 非零退出；脚本开了
# `pipefail`，这个非零会盖过 grep 那边「其实找到了」的结果，导致**文件明明在 zip 里却报「没找到」**
# ——真踩过，现象是间歇性的（取决于 unzip 输出被 grep 提前掐断的时机），排查时反复手动重跑那条
# 管道又总是成功，就是因为手动重跑时没有这层 pipefail 竞态。
ZIP_LISTING=$(unzip -l "$ZIP_PATH")
if ! grep -q " ${DIST_NAME}/LICENSE\$" <<< "$ZIP_LISTING"; then
  echo "  ✗ zip 里没有 ${DIST_NAME}/LICENSE" >&2
  exit 1
fi

echo ""
echo "完成：${DIST_DIR}/ 与 ${ZIP_PATH}"
