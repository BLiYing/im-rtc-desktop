#!/usr/bin/env bash
# smoke.sh —— 对着**真服务端**跑一遍：握手 → 拨号 → 终局。
#
#   ./scripts/smoke.sh                     # 默认连本机 :8787 的 rtc-server
#   RTC_HTTP=http://192.168.1.12:8787 ./scripts/smoke.sh
#
# 它不在 test.sh 里：单元测试必须在没有网络、没有服务端的机器上也能跑。
# 服务端起法见 ../im-rtc-server/scripts/dev.sh。
set -euo pipefail

cd "$(dirname "$0")/.."

RTC_HTTP=${RTC_HTTP:-http://127.0.0.1:8787}
RTC_WS=${RTC_WS:-ws://${RTC_HTTP#*://}/v1/ws}
USERNAME=${USERNAME:-alice}
CALLEE=${CALLEE:-nobody-offline}   # 必然不在线 → 服务端立刻回 offline
DEVICE_ID=${DEVICE_ID:-mac-smoke-1}

case "$(uname -s)" in
  Darwin) PRESET="${IMRTC_PRESET:-macos-clang}" ;;
  *) PRESET="${IMRTC_PRESET:-macos-clang}" ;;
esac
BIN="./build/${PRESET}/tools/imrtc_smoke"

[ -x "$BIN" ] || { echo "先编译：cmake --build --preset ${PRESET}"; exit 2; }

echo "== 取 demo 票（$USERNAME）=="
# 免密登录只在开发构建可用（rtc-server -demo-login，拍板 §11-9）。
TOKEN=$(curl -fsS -X POST "$RTC_HTTP/v1/demo/login" \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"$USERNAME\"}" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')

[ -n "$TOKEN" ] || { echo "没拿到 token。服务端起来了吗？"; exit 1; }
echo "  票 ${TOKEN:0:6}…（长度 ${#TOKEN}）"   # 凭据只打前 6 位 + 长度（CONVENTIONS §8）

echo ""
echo "== 跑 =="
"$BIN" "$RTC_WS" "$TOKEN" "$DEVICE_ID" "$CALLEE"
