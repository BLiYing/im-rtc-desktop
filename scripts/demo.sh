#!/usr/bin/env bash
# demo.sh —— 跑 Qt Demo（需要 Qt 6；source/local 档还要真服务端，public 档额外要网络）。
#
#   ./scripts/demo.sh                      # 手动登录（源码档）
#   ./scripts/demo.sh alice                # 自动以 alice 登录
#   ./scripts/demo.sh alice bob            # 登录后自动拨 bob（语音）
#   ./scripts/demo.sh alice bob video      # …视频
#   RTC_HTTP=http://192.168.1.12:8787 ./scripts/demo.sh alice
#
# **三档 SDK**（IMRTC_SDK 环境变量，默认 source）——三档下 Demo 与引擎的链接方式不同，
# 各用各的 build 目录，避免 CMake 缓存互相污染：
#   IMRTC_SDK=source ./scripts/demo.sh   # 现状：add_subdirectory 引擎源码，build/macos-clang
#   IMRTC_SDK=local  ./scripts/demo.sh   # 本机 scripts/package.sh 打的包，build/macos-clang-sdk-local
#   IMRTC_SDK=public ./scripts/demo.sh   # 从 GitHub Release 下载的包，build/macos-clang-sdk-public
# local/public 两档下 Demo 对引擎的链接方式与第三方 `find_package(imrtc)` 完全一样
# （见顶层 CMakeLists.txt 的 IMRTC_SDK_DIR、demo/CMakeLists.txt 的 Frameworks 拷贝）。
#
# 同机开两个实例互打时，各自的设置与通话记录靠 --profile 隔离
# （macOS 的 QStandardPaths 不理会 $HOME，换环境变量隔离不了）：
#   ./scripts/demo.sh alice &   # profile 自动取用户名
#   ./scripts/demo.sh bob
#
# 服务端起法见 ../im-rtc-server/scripts/dev.sh。
# Qt 装法见 demo/CMakeLists.txt 顶部注释。
set -euo pipefail

cd "$(dirname "$0")/.."

if [ "$(uname -s)" != "Darwin" ]; then
  echo "demo.sh 只在 macOS 上跑（本仓这版不交付 Windows）。" >&2
  exit 1
fi

SDK="${IMRTC_SDK:-source}"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/6.8.3/macos}"
RTC_HTTP="${RTC_HTTP:-http://127.0.0.1:8787}"

VERSION=$(sed -nE 's/.*kSdkVersion\[\] = "([0-9]+\.[0-9]+\.[0-9]+)".*/\1/p' \
  engine/include/imrtc/Version.h)
DIST_NAME="imrtc-desktop-${VERSION}-macos"

USERNAME="${1:-}"
CALLEE="${2:-}"
MEDIA="${3:-audio}"

case "$SDK" in
  source)
    # 现状：源码档，PRESET 走 CMakePresets.json 里现成的 macos-clang。
    PRESET="${IMRTC_PRESET:-macos-clang}"
    BUILD_DIR="build/${PRESET}"
    BIN="./${BUILD_DIR}/demo/imrtc_demo.app/Contents/MacOS/imrtc_demo"
    if [ ! -x "$BIN" ]; then
      echo "== [source] 先构建 Demo（默认是关的）=="
      cmake --preset "$PRESET" -DIMRTC_BUILD_DEMO=ON -DCMAKE_PREFIX_PATH="$QT_PREFIX"
      cmake --build --preset "$PRESET" --target imrtc_demo
    fi
    ;;

  local)
    SDK_DIR="$(pwd)/dist/${DIST_NAME}"
    if [ ! -d "$SDK_DIR" ]; then
      echo "找不到本地包：${SDK_DIR}" >&2
      echo "先跑一次 ./scripts/package.sh 打出来。" >&2
      exit 1
    fi
    BUILD_DIR="build/macos-clang-sdk-local"
    BIN="./${BUILD_DIR}/demo/imrtc_demo.app/Contents/MacOS/imrtc_demo"
    if [ ! -x "$BIN" ]; then
      echo "== [local] 先构建 Demo（用 ${SDK_DIR} 这份包，不碰引擎源码）=="
      cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
        -DIMRTC_BUILD_DEMO=ON -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DIMRTC_SDK_DIR="$SDK_DIR"
      cmake --build "$BUILD_DIR" --target imrtc_demo
    fi
    ;;

  public)
    PUBLIC_DIR="$(pwd)/dist/public"
    SDK_DIR="${PUBLIC_DIR}/${DIST_NAME}"
    ZIP_URL="https://github.com/BLiYing/im-rtc-desktop/releases/download/${VERSION}/${DIST_NAME}.zip"
    if [ ! -d "$SDK_DIR" ]; then
      echo "== [public] 下载发布包 =="
      echo "  ${ZIP_URL}"
      mkdir -p "$PUBLIC_DIR"
      ZIP_PATH="${PUBLIC_DIR}/${DIST_NAME}.zip"
      if ! curl -fSL --progress-bar -o "$ZIP_PATH" "$ZIP_URL"; then
        rm -f "$ZIP_PATH"
        echo "" >&2
        echo "下载失败：${ZIP_URL}" >&2
        echo "大概率是还没发布——tag ${VERSION} 的 Release 得由用户确认后才会建。" >&2
        echo "先用 IMRTC_SDK=local（跑 ./scripts/package.sh 打本机包）验这条链路。" >&2
        exit 1
      fi
      unzip -q -o "$ZIP_PATH" -d "$PUBLIC_DIR"
      if [ ! -d "$SDK_DIR" ]; then
        echo "解压完看不到 ${SDK_DIR}，zip 内部布局可能变了。" >&2
        exit 1
      fi
    fi
    BUILD_DIR="build/macos-clang-sdk-public"
    BIN="./${BUILD_DIR}/demo/imrtc_demo.app/Contents/MacOS/imrtc_demo"
    if [ ! -x "$BIN" ]; then
      echo "== [public] 先构建 Demo（用下载下来的 ${SDK_DIR}，不碰引擎源码）=="
      cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
        -DIMRTC_BUILD_DEMO=ON -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DIMRTC_SDK_DIR="$SDK_DIR"
      cmake --build "$BUILD_DIR" --target imrtc_demo
    fi
    ;;

  *)
    echo "IMRTC_SDK 只认 source|local|public，收到：${SDK}" >&2
    exit 1
    ;;
esac

ARGS=(--server "$RTC_HTTP")
[ -n "$USERNAME" ] && ARGS+=(--user "$USERNAME" --profile "$USERNAME")
[ -n "$CALLEE" ] && ARGS+=(--call "$CALLEE")
[ "$MEDIA" = "video" ] && ARGS+=(--video)

# 想看引擎在干什么就打开这一行的日志类别。
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-imrtc.demo.*=true}"
exec "$BIN" "${ARGS[@]}"
