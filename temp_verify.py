"""temp_verify.py —— 静态检查「SDK 统一 1.0.0 + 设置页详细日志 + libwebrtc 现状」这一刀。

只读源码，不编译、不跑进程（编译与界面测试交给 ./scripts/test.sh）：
  1. 版本号只有一处真相：engine/include/imrtc/Version.h 的 kSdkVersion == "1.0.0"，
     C ABI 的 imrtc_v1_version()、引擎两个 Options 的 sdk 默认串都从它取；
  2. 其余写死版本号的地方都是 1.0.0：Info.plist.in、两个测试；Demo 的 applicationVersion 取 C ABI；
  3. 仓里（git 跟踪的源码与文档，current_task*.md 除外）没有残留的 0.1.0 / 0.0.1；
  4. SettingsPage.cpp 里每一条 tr() 在 demo/i18n/imrtc_demo_en.ts 的 SettingsPage 上下文里都有非空译文，
     .ts 里也没有 SettingsPage.cpp 已经不用的旧条目，%1 占位符个数对得上；
  5. QSettings 键 "log/verbose" 在 EngineBridge.cpp 定义、被测试引用；设置页有日志分组与 verboseLog 勾选；
     main.cpp 启动时按存的值设级别；libwebrtc 那一行照实写着 m150.7871.3.2 / M150；
  6. SettingsTest 挂进了 demo/CMakeLists.txt 的测试列表；
  7. （可选）TEST_SH_LOG 指向的 test.sh 日志以「全部通过」结尾且 exit=0；没给就 SKIP。

运行：python3 temp_verify.py
"""

from __future__ import annotations

import logging
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional

log = logging.getLogger("temp_verify")

REPO = Path(__file__).resolve().parent
VERSION = "1.0.0"
OLD_VERSION = re.compile(r"(?<![\d.])0\.(?:1\.0|0\.1)(?![\d.])")
SKIP_RESIDUE = {"current_task.md", "current_task.archive.md", "temp_verify.py"}
TEXT_SUFFIXES = {".cpp", ".h", ".hpp", ".mm", ".in", ".txt", ".md", ".sh", ".json", ".ts", ".cmake"}


@dataclass
class Outcome:
    name: str
    status: str = "PASS"  # PASS / FAIL / SKIP
    details: List[str] = field(default_factory=list)

    def fail(self, message: str) -> None:
        self.status = "FAIL"
        self.details.append(message)


def read(relative: str) -> str:
    path = REPO / relative
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"读不了 {relative}：{error}") from error


def require(outcome: Outcome, text: str, needle: str, where: str) -> None:
    if needle not in text:
        outcome.fail(f"{where} 里找不到：{needle}")


def check_single_source() -> Outcome:
    o = Outcome("版本号单一真相源 = 1.0.0")
    require(o, read("engine/include/imrtc/Version.h"), f'kSdkVersion[] = "{VERSION}"', "Version.h")
    require(o, read("capi/src/imrtc_c.cpp"), "imrtc_v1_version(void) { return imrtc::kSdkVersion; }",
            "imrtc_c.cpp")
    for header in ("engine/include/imrtc/Connection.h", "engine/include/imrtc/CallEngine.h"):
        text = read(header)
        require(o, text, '#include "imrtc/Version.h"', header)
        require(o, text, 'std::string sdk = std::string("desktop/") + kSdkVersion;', header)
    require(o, read("capi/include/imrtc/CallEngine.hpp"),
            'std::string("desktop-cpp/") + imrtc_v1_version()', "CallEngine.hpp")
    return o


def check_other_sites() -> Outcome:
    o = Outcome("其余版本号位置")
    plist = read("demo/Info.plist.in")
    for key in ("CFBundleShortVersionString", "CFBundleVersion"):
        if not re.search(rf"<key>{key}</key>\s*<string>{re.escape(VERSION)}</string>", plist):
            o.fail(f"Info.plist.in 的 {key} 不是 {VERSION}")
    require(o, read("demo/main.cpp"), "setApplicationVersion(EngineBridge::versionString())", "main.cpp")
    require(o, read("demo/EngineBridge.cpp"), '"desktop-qt-demo/" + std::string(imrtc_v1_version())',
            "EngineBridge.cpp")
    require(o, read("tools/Smoke.cpp"), 'std::string("desktop-smoke/") + imrtc_v1_version()', "Smoke.cpp")
    require(o, read("tests/ConnectionTest.cpp"), '"desktop/1.0.0"', "ConnectionTest.cpp")
    capi_test = read("tests/CapiTest.cpp")
    require(o, capi_test, '"desktop-test/1.0.0"', "CapiTest.cpp")
    require(o, capi_test, 'std::string(imrtc_v1_version()), std::string("1.0.0")', "CapiTest.cpp")
    return o


def tracked_files() -> List[str]:
    try:
        result = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard"],
                                cwd=REPO, capture_output=True, text=True, check=True, timeout=30)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"git ls-files 失败：{error}") from error
    return [line for line in result.stdout.splitlines() if line and not line.startswith(".claude/")]


def check_no_residue() -> Outcome:
    o = Outcome("无残留 0.1.0 / 0.0.1")
    for relative in tracked_files():
        path = REPO / relative
        if Path(relative).name in SKIP_RESIDUE or path.suffix not in TEXT_SUFFIXES or not path.is_file():
            continue
        for number, line in enumerate(read(relative).splitlines(), start=1):
            if OLD_VERSION.search(line):
                o.fail(f"{relative}:{number}: {line.strip()}")
    return o


def cpp_unescape(literal: str) -> str:
    return literal.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")


def tr_strings(source: str) -> List[str]:
    """抽出 tr("…" "…") 的实参：相邻字面量按 C++ 规则拼接。"""
    out: List[str] = []
    for match in re.finditer(r'\btr\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', source):
        pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1))
        out.append(cpp_unescape("".join(pieces)))
    return out


def ts_context(context_name: str) -> Dict[str, Optional[ET.Element]]:
    try:
        root = ET.parse(REPO / "demo/i18n/imrtc_demo_en.ts").getroot()
    except (OSError, ET.ParseError) as error:
        raise RuntimeError(f".ts 解析失败：{error}") from error
    for context in root.iter("context"):
        if context.findtext("name") == context_name:
            return {m.findtext("source") or "": m.find("translation") for m in context.iter("message")}
    return {}


def check_translations() -> Outcome:
    o = Outcome("SettingsPage 的 tr() 都有英文译文")
    sources = tr_strings(read("demo/SettingsPage.cpp"))
    messages = ts_context("SettingsPage")
    if not sources or not messages:
        o.fail(f"抽到 {len(sources)} 条 tr()、.ts 里 {len(messages)} 条——至少一边是空的")
        return o
    for text in sources:
        node = messages.get(text)
        if node is None:
            o.fail(f".ts 缺条目：{text[:40]!r}")
        elif not (node.text or "").strip() or node.get("type") in ("unfinished", "obsolete", "vanished"):
            o.fail(f"译文空或未完成：{text[:40]!r}")
        elif text.count("%1") != (node.text or "").count("%1"):
            o.fail(f"%1 个数对不上：{text[:40]!r}")
    for stale in set(messages) - set(sources):
        o.fail(f".ts 里有已不用的旧条目：{stale[:40]!r}")
    for needle in ("日志", "详细日志", "libwebrtc：未接入（计划锁 m150.7871.3.2 / M150）。"):
        if not any(needle in text for text in sources):
            o.fail(f"SettingsPage.cpp 的 tr() 里没有：{needle}")
    return o


def check_setting_wiring() -> Outcome:
    o = Outcome("log/verbose 键与启动时恢复")
    bridge = read("demo/EngineBridge.cpp")
    require(o, bridge, 'kVerboseLogKey[] = "log/verbose"', "EngineBridge.cpp")
    require(o, bridge, "imrtc_v1_set_log_level(verbose ? IMRTC_V1_LOG_DEBUG : IMRTC_V1_LOG_INFO)",
            "EngineBridge.cpp")
    require(o, bridge, "value(QLatin1String(kVerboseLogKey), false)", "EngineBridge.cpp（默认关）")
    page = read("demo/SettingsPage.cpp")
    require(o, page, 'setObjectName(QStringLiteral("verboseLog"))', "SettingsPage.cpp")
    require(o, page, "EngineBridge::setVerboseLog(on)", "SettingsPage.cpp")
    main = read("demo/main.cpp")
    require(o, main, "if (level.isEmpty()) EngineBridge::applyLogLevel(EngineBridge::verboseLog());", "main.cpp")
    if main.find("applyLogLevel(EngineBridge::verboseLog())") < main.find("setApplicationName("):
        o.fail("main.cpp 在定 applicationName 之前就读了 QSettings")
    require(o, read("demo/tests/SettingsTest.cpp"), 'kKey[] = "log/verbose"', "SettingsTest.cpp")
    return o


def check_cmake() -> Outcome:
    o = Outcome("SettingsTest 挂进 CMake")
    if not re.search(r"foreach\(_case [^)]*\bSettings\b", read("demo/CMakeLists.txt")):
        o.fail("demo/CMakeLists.txt 的测试列表里没有 Settings")
    if not (REPO / "demo/tests/SettingsTest.cpp").is_file():
        o.fail("demo/tests/SettingsTest.cpp 不存在")
    return o


def check_test_log() -> Outcome:
    o = Outcome("test.sh 日志")
    location = os.environ.get("TEST_SH_LOG")
    if not location:
        o.status = "SKIP"
        o.details.append("没给 TEST_SH_LOG")
        return o
    try:
        text = Path(location).read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        o.status = "SKIP"
        o.details.append(f"读不了 {location}：{error}")
        return o
    if "全部通过" not in text or "exit=0" not in text:
        o.fail("日志里没有「全部通过」或 exit=0")
    if "SettingsTest" not in text:
        o.fail("日志里没看到 SettingsTest 跑过")
    return o


CHECKS: List[Callable[[], Outcome]] = [
    check_single_source, check_other_sites, check_no_residue,
    check_translations, check_setting_wiring, check_cmake, check_test_log,
]


def run(check: Callable[[], Outcome]) -> Outcome:
    try:
        return check()
    except RuntimeError as error:
        outcome = Outcome(check.__name__)
        outcome.fail(str(error))
        return outcome


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    outcomes = [run(check) for check in CHECKS]
    for outcome in outcomes:
        log.info("[%s] %s", outcome.status, outcome.name)
        if outcome.status != "PASS":
            for detail in outcome.details:
                log.info("       %s", detail)
    failed = sum(1 for outcome in outcomes if outcome.status == "FAIL")
    skipped = sum(1 for outcome in outcomes if outcome.status == "SKIP")
    log.info("通过 %d，失败 %d，跳过 %d", len(outcomes) - failed - skipped, failed, skipped)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
