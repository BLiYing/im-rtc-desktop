#!/usr/bin/env bash
# check-logging.sh —— 日志纪律的硬闸（CONVENTIONS.md §8）。
#
#   ./scripts/check-logging.sh            全量
#   ./scripts/check-logging.sh --selftest 门禁自检
#
# 为什么需要一道闸：这几条规矩在规范里写了很久，但规范拦不住手滑。
# 姊妹项目 IMServer 上「禁止直接 print」这条**累计出过 54 处违规而无人察觉**——
# 因为有兼容桥接兜底，看起来没坏。**没有闸门的规范等于没有规范。**
#
# 三条规矩（与 im-rtc-server / im-rtc-web 那两份同源，见 docs/mechanism/LOGGING.md §5）：
#   ① 业务代码禁止 std::cout / printf / qDebug 一类 —— 一律走 imrtc::log。
#   ② 媒体热路径（HOTPATH-BEGIN/END 之间）禁止任何日志调用，哪怕 debug：
#      参数求值本身就是开销，而那段代码每个 RTP 包都要走一遍。
#   ③ 凭据与 SDP 不得整条进日志 —— 必须过 redact / redactSdp / redactCandidate。
set -u

SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"

# CHECK_ROOT 让 --selftest 能把闸门指向临时目录。
# 不做这一步的话，自检的子调用会 cd 回真仓库、扫到「全部通过」，
# 于是**自检永远说闸门坏了**——这个坑是 server 仓那份闸门先踩出来的。
SCAN_ROOT="${CHECK_ROOT:-$(dirname "$0")/..}"
cd "$SCAN_ROOT" || { echo "无法定位扫描根目录 $SCAN_ROOT"; exit 2; }

fail=0
report() { echo "  ✗ $1"; fail=1; }

# 允许直接打印的地方：
#   engine/src/observability/ —— 日志设施自己，内置那一路就是它写的 stderr；
#   tools/                    —— CLI 的全部产出就是它打在终端上的东西（CONVENTIONS §8）；
#   tests/                    —— 测试报告本身。
is_print_exempt() {
  case "$1" in
    engine/src/observability/*) return 0 ;;
    tools/*)                    return 0 ;;
    tests/*)                    return 0 ;;
  esac
  return 1
}

scan_files() {
  find engine capi demo transport tools -type f \
       \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.mm' \) 2>/dev/null | sort
}

# ── 自检：闸门本身回归了会静默放行，所以自己也要测 ──────────────
run_selftest() {
  tmp=$(mktemp -d) || exit 2
  trap 'rm -rf "$tmp"' EXIT
  mkdir -p "$tmp/engine/src/bad" "$tmp/scripts"
  cat > "$tmp/engine/src/bad/x.cpp" <<'CPP'
#include <cstdio>
void f(const std::string& token, const std::string& sdp) {
  std::printf("nope\n");
  log(LogLevel::Info, "leak", {{"token", token}, {"sdp", sdp}});
}
CPP
  if CHECK_ROOT="$tmp" bash "$SELF" >/dev/null 2>&1; then
    echo "✗ selftest：闸门放行了明显违规的文件"
    exit 1
  fi
  echo "✓ check-logging selftest 通过"
  exit 0
}

if [ "${1:-}" = "--selftest" ]; then
  run_selftest
fi

echo "== 日志纪律检查 =="

# 文件清单只算一次，三条检查各扫一遍。
# **一次 grep 扫全部文件**，不是每个文件起一个进程：早先那版是后者，
# 一趟要跑 2 分 45 秒，没人会愿意把它放进 pre-commit。
FILES=$(scan_files)
[ -n "$FILES" ] || { echo "  （没有源文件可扫）"; exit 0; }

# shellcheck disable=SC2086  # 文件名里没有空格，这里要的正是分词
set -- $FILES

# ── ① 直接打印 ────────────────────────────────────────────────
echo "  [1/3] 直接打印（std::cout / printf / qDebug 一类）"
PRINTS='(^|[^[:alnum:]_>.])(std::cout|std::cerr|std::clog|printf|fprintf|fputs|puts|qDebug|qInfo|qWarning|qCritical|qFatal)[[:space:]]*\('
grep -nE "$PRINTS" "$@" 2>/dev/null \
  | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*)' \
  | grep -v 'nolint:print' \
  | while IFS= read -r hit; do
      file=${hit%%:*}
      is_print_exempt "$file" && continue
      report "$hit —— 走 imrtc::log，别直接打印"
    done > /tmp/imrtc-logcheck-1.$$ 2>/dev/null
if [ -s /tmp/imrtc-logcheck-1.$$ ]; then cat /tmp/imrtc-logcheck-1.$$; fail=1; fi
rm -f /tmp/imrtc-logcheck-1.$$

# ── ② 热路径里的日志 ──────────────────────────────────────────
echo "  [2/3] 媒体热路径（HOTPATH-BEGIN…HOTPATH-END）里的日志调用"
hot=$(awk '
  # 标记必须紧跟在 // 之后，且同一行不能出现另一个标记。
  # 不这么限的话，说明这套标记怎么用的那句注释会被当成真标记，
  # 于是整个文件都算热路径——server 仓那道闸自己踩过一次。
  FNR==1 { inhot=0 }
  /^[[:space:]]*\/\/[[:space:]]*HOTPATH-BEGIN/ && !/HOTPATH-END/ { inhot=1; next }
  /^[[:space:]]*\/\/[[:space:]]*HOTPATH-END/                     { inhot=0; next }
  inhot && /imrtc::log\(|[^a-zA-Z_]log\(|LogLevel::/ { print FILENAME ":" FNR ": " $0 }
' "$@" 2>/dev/null || true)
if [ -n "$hot" ]; then
  while IFS= read -r hit; do report "$hit —— 热路径禁止日志，要观测请加原子计数器"; done <<< "$hot"
fi

# ── ③ 凭据 / SDP 整条进日志 ───────────────────────────────────
echo "  [3/3] 凭据与 SDP 是否过了脱敏"
# 分三步筛，不用一个带 .* 的大正则：那种写法在长行上会疯狂回溯。
sensitive=$(grep -nE '(^|[^a-zA-Z_])log\(' "$@" 2>/dev/null \
  | grep -E '"(token|room_token|roomToken|secret|password|sdp|candidate)"' \
  | grep -vE 'redact|Redact' || true)
if [ -n "$sensitive" ]; then
  while IFS= read -r hit; do
    file=${hit%%:*}
    is_print_exempt "$file" && continue
    report "$hit —— 用 imrtc::redact / redactSdp / redactCandidate"
  done <<< "$sensitive"
fi

echo ""
if [ "$fail" -ne 0 ]; then
  echo "结果：✗ 日志纪律有违规。见 CONVENTIONS.md §8 与"
  echo "      ../im-rtc-server/docs/mechanism/LOGGING.md。"
  exit 1
fi
echo "结果：✓ 全部通过。"
exit 0
