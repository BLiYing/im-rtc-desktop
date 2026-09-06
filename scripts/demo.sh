#!/usr/bin/env bash
# demo.sh —— 跑 Qt Demo（需要 Qt 6 与真服务端）。
#
#   ./scripts/demo.sh                      # 手动登录
#   ./scripts/demo.sh alice                # 自动以 alice 登录
#   ./scripts/demo.sh alice bob            # 登录后自动拨 bob（语音）
#   ./scripts/demo.sh alice bob video      # …视频
#   RTC_HTTP=http://192.168.1.12:8787 ./scripts/demo.sh alice
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

PRESET="${IMRTC_PRESET:-macos-clang}"
QT_PREFIX="${QT_PREFIX:-$HOME/Qt/6.8.3/macos}"
RTC_HTTP="${RTC_HTTP:-http://127.0.0.1:8787}"

USERNAME="${1:-}"
CALLEE="${2:-}"
MEDIA="${3:-audio}"

BIN="./build/${PRESET}/demo/imrtc_demo.app/Contents/MacOS/imrtc_demo"
if [ ! -x "$BIN" ]; then
  echo "== 先构建 Demo（默认是关的）=="
  cmake --preset "$PRESET" -DIMRTC_BUILD_DEMO=ON -DCMAKE_PREFIX_PATH="$QT_PREFIX"
  cmake --build --preset "$PRESET" --target imrtc_demo
fi

ARGS=(--server "$RTC_HTTP")
[ -n "$USERNAME" ] && ARGS+=(--user "$USERNAME" --profile "$USERNAME")
[ -n "$CALLEE" ] && ARGS+=(--call "$CALLEE")
[ "$MEDIA" = "video" ] && ARGS+=(--video)

# 想看引擎在干什么就打开这一行的日志类别。
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-imrtc.demo.*=true}"
exec "$BIN" "${ARGS[@]}"
