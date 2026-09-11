"""temp_verify.py —— 验证 demo/SystemAlertAttention_win.cpp（Windows 闪任务栏改用 FlashWindowEx）。

本机是 Intel Mac，编不了真的 Windows。能做的是：
  1. 用 brew llvm 的 clang 以 x86_64-w64-mingw32 为目标，对着真实的 mingw-w64 头和 Qt 6.8.3 头做语法检查，
     我们自己的代码开 -Werror（与 CMake 非 MSVC 分支同一组警告）；
  2. 反例对照：把 FLASHW_STOP 改错名的副本必须编不过——证明检查不是空转；
  3. demo/CMakeLists.txt 在 WIN32 下选 _win.cpp、Apple 分支不变；
  4. _win.cpp 的 cancel() 真的发 FLASHW_STOP 并清掉句柄；
  5. macOS 上 ./scripts/test.sh 的结果（读日志，不在这里重跑；没给日志就跳过）。

运行：python3 temp_verify.py
需要：Homebrew 的 llvm（Intel / Apple Silicon 两个前缀都找）、~/Qt/6.8.3/macos、首次运行能连 GitHub。
可选环境变量：
  VERIFY_SCRATCH  头文件缓存目录，默认「系统临时目录/imrtc-desktop-verify」，拉过一次就不再联网；
  TEST_SH_LOG     `./scripts/test.sh > 某文件 2>&1; echo "exit=$?" >> 某文件` 产出的日志路径。
"""

from __future__ import annotations

import logging
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, List

log = logging.getLogger("temp_verify")

REPO = Path(__file__).resolve().parent
DEMO = REPO / "demo"
WIN_CPP = DEMO / "SystemAlertAttention_win.cpp"
SCRATCH = Path(os.environ.get("VERIFY_SCRATCH") or Path(tempfile.gettempdir()) / "imrtc-desktop-verify")
# 不用系统 /usr/bin/clang++：Apple 的 clang 不带 libc++ 头的独立副本，mingw 目标下找不到 <functional>。
LLVM_PREFIXES = [Path("/usr/local/opt/llvm"), Path("/opt/homebrew/opt/llvm")]
QT_LIB = Path.home() / "Qt/6.8.3/macos/lib"
QT_WINDEFS_URL = "https://raw.githubusercontent.com/qt/qtbase/v6.8.3/src/gui/kernel/qwindowdefs_win.h"
MINGW_URL = "https://github.com/mingw-w64/mingw-w64.git"
WARNINGS = ["-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Wshadow", "-Werror"]


@dataclass
class Report:
    passed: List[str] = field(default_factory=list)
    failed: List[str] = field(default_factory=list)
    skipped: List[str] = field(default_factory=list)

    def check(self, name: str, ok: bool, detail: str = "") -> None:
        (self.passed if ok else self.failed).append(name)
        if ok:
            log.info("PASS %s", name)
        else:
            log.error("FAIL %s %s", name, detail)

    def skip(self, name: str, why: str) -> None:
        self.skipped.append(name)
        log.warning("SKIP %s（%s）", name, why)


@dataclass(frozen=True)
class Toolchain:
    clang: Path
    libcxx: Path


def find_toolchain() -> Toolchain:
    for prefix in LLVM_PREFIXES:
        tc = Toolchain(prefix / "bin/clang++", prefix / "include/c++/v1")
        if tc.clang.exists() and tc.libcxx.is_dir():
            return tc
    tried = "、".join(str(p) for p in LLVM_PREFIXES)
    raise RuntimeError(f"没找到 Homebrew llvm（试过 {tried}）：brew install llvm")


def run(cmd: List[str], cwd: Path | None = None, timeout: int = 300) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return subprocess.CompletedProcess(cmd, 127, "", f"{type(exc).__name__}: {exc}")


def ensure_mingw_headers() -> Path:
    """sparse clone 只取头文件；已经有了就不再拉。失败时删掉半截目录，下次重来。"""
    root = SCRATCH / "mingw"
    if (root / "mingw-w64-headers/include/winuser.h").exists():
        return root / "mingw-w64-headers"
    shutil.rmtree(root, ignore_errors=True)
    steps = [
        ["git", "clone", "--depth", "1", "--filter=blob:none", "--sparse", MINGW_URL, str(root)],
        ["git", "-C", str(root), "sparse-checkout", "set", "mingw-w64-headers/include", "mingw-w64-headers/crt"],
    ]
    for cmd in steps:
        res = run(cmd, timeout=600)
        if res.returncode != 0:
            shutil.rmtree(root, ignore_errors=True)
            raise RuntimeError(f"拉 mingw-w64 头失败：{res.stderr.strip()[-400:]}")
    return root / "mingw-w64-headers"


def fetch_qt_windefs(dest: Path, attempts: int = 3) -> None:
    """macOS 版 Qt 不带 qwindowdefs_win.h；从 qtbase 同版本 tag 取一份。重试 3 次，写临时文件再改名。"""
    if dest.exists():
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    last: Exception | None = None
    for i in range(attempts):
        try:
            with urllib.request.urlopen(QT_WINDEFS_URL, timeout=30) as resp:
                data = resp.read()
            tmp = dest.with_suffix(".part")
            tmp.write_bytes(data)
            tmp.replace(dest)
            return
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last = exc
            log.warning("取 qwindowdefs_win.h 第 %d 次失败：%s", i + 1, exc)
    raise RuntimeError(f"取 qwindowdefs_win.h 失败：{last}")


def build_include_tree(headers: Path) -> Path:
    """mingw 的 include + crt 拷成一个 sysroot；_mingw.h 由 .in 生成；Qt 三个模块用软链。"""
    tree = SCRATCH / "wininc"
    sysdir, qtdir = tree / "sys", tree / "qt"
    if not (sysdir / "_mingw.h").exists():
        shutil.rmtree(sysdir, ignore_errors=True)
        shutil.copytree(headers / "include", sysdir)
        shutil.copytree(headers / "crt", sysdir, dirs_exist_ok=True)
        text = (headers / "crt/_mingw.h.in").read_text()
        text = text.replace("@DEFAULT_WIN32_WINNT@", "0xa00").replace("@DEFAULT_MSVCRT_VERSION@", "0xE00")
        (sysdir / "_mingw.h").write_text(re.sub(r"@[A-Z_0-9]+@", "", text))
    if not (QT_LIB / "QtWidgets.framework/Headers").is_dir():
        raise RuntimeError(f"没找到 Qt 头：{QT_LIB}（装法见 demo/CMakeLists.txt 顶部）")
    qtdir.mkdir(parents=True, exist_ok=True)
    for mod in ("QtCore", "QtGui", "QtWidgets"):
        link = qtdir / mod
        if not link.exists():
            link.symlink_to(QT_LIB / f"{mod}.framework/Headers")
    fetch_qt_windefs(tree / "qtwin/QtGui/qwindowdefs_win.h")
    return tree


def syntax_check(tc: Toolchain, tree: Path, source: Path) -> subprocess.CompletedProcess[str]:
    qt = tree / "qt"
    cmd = [str(tc.clang), "--target=x86_64-w64-mingw32", "-std=c++17", "-fsyntax-only", "-nostdinc++",
           "-isystem", str(tc.libcxx), "-isystem", str(tree / "sys"),
           "-isystem", str(tree / "qtwin"), "-isystem", str(qt),
           *[f"-isystem{qt / m}" for m in ("QtCore", "QtGui", "QtWidgets")],
           f"-I{DEMO}", *WARNINGS, str(source)]
    return run(cmd, cwd=source.parent)


def check_compiles(report: Report, tc: Toolchain, tree: Path) -> None:
    res = syntax_check(tc, tree, WIN_CPP)
    report.check("mingw 目标下 _win.cpp 语法检查（-Werror）", res.returncode == 0, res.stderr.strip()[-1500:])


def check_negative_control(report: Report, tc: Toolchain, tree: Path) -> None:
    """边界：同一条命令对一个改坏的副本必须报错，否则上面那条 PASS 不可信。"""
    with tempfile.TemporaryDirectory(dir=SCRATCH) as tmp:
        broken = Path(tmp) / WIN_CPP.name
        broken.write_text(WIN_CPP.read_text().replace("FLASHW_STOP", "FLASHW_STOPP"))
        res = syntax_check(tc, tree, broken)
        hit = "FLASHW_STOPP" in res.stderr
    report.check("反例：FLASHW_STOP 改错名必须编不过", res.returncode != 0 and hit)


def check_cmake(report: Report) -> None:
    text = (DEMO / "CMakeLists.txt").read_text()
    m = re.search(r"if\(APPLE\)(.*?)elseif\(WIN32\)(.*?)else\(\)(.*?)endif\(\)", text, re.S)
    report.check("CMake 有 APPLE / WIN32 / 其余 三个分支", m is not None)
    if m is None:
        return
    apple, win, other = m.groups()
    report.check("Apple 分支仍是 _mac.mm", "SystemAlertAttention_mac.mm" in apple)
    report.check("WIN32 分支用 _win.cpp、不用 stub", "SystemAlertAttention_win.cpp" in win
                 and "SystemAlertAttention_stub.cpp" not in win)
    report.check("其余平台仍是 stub", "SystemAlertAttention_stub.cpp" in other)


def check_cancel_semantics(report: Report) -> None:
    src = WIN_CPP.read_text()
    cancel = re.search(r"void attention::cancel\(\) \{(.*?)\n\}", src, re.S)
    body = cancel.group(1) if cancel else ""
    report.check("cancel() 发 FLASHW_STOP", "FLASHW_STOP" in body)
    report.check("cancel() 清掉句柄（重复调无害）", "gFlashing = nullptr" in body and "== nullptr) return" in body)
    report.check("request() 用 TIMERNOFG（切回前台系统自停）", "FLASHW_ALL | FLASHW_TIMERNOFG" in src)
    report.check("request() 不替没句柄的窗口新建句柄", "internalWinId()" in src and "winId()" not in
                 src.replace("internalWinId()", ""))


def check_mac_tests(report: Report) -> None:
    name = "macOS ./scripts/test.sh 退出码 0"
    raw = os.environ.get("TEST_SH_LOG")
    if not raw:
        report.skip(name, "没设 TEST_SH_LOG")
        return
    try:
        text = Path(raw).read_text(errors="replace")
    except OSError as exc:
        report.skip(name, f"读不了日志：{exc}")
        return
    report.check(name, "exit=0" in text, text.strip()[-800:])


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    report = Report()
    steps: List[Callable[[Report], None]] = [check_cmake, check_cancel_semantics, check_mac_tests]
    for step in steps:
        step(report)
    try:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        tc = find_toolchain()
        tree = build_include_tree(ensure_mingw_headers())
        check_compiles(report, tc, tree)
        check_negative_control(report, tc, tree)
    except (RuntimeError, OSError) as exc:
        report.check("准备编译器与 Windows 头文件", False, str(exc))
    log.info("\n%d 通过，%d 失败，%d 跳过", len(report.passed), len(report.failed), len(report.skipped))
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
