#!/usr/bin/env python3
"""桌面 Demo 的翻译与跨端文案表对齐 + 完整性体检。

Qt 侧以中文原文为 key（`tr("…")`），翻译在 demo/i18n/imrtc_demo_en.ts。这份 .ts 里，
**中文原文与 im-rtc-server/docs/i18n/strings.json 某条中文相同的**，英文必须逐字取表里的那句
（只有一个占位符时 `{x}` ↔ `%1`）；桌面独有的文案不在表里，手写。

  python3 scripts/i18n_sync.py          把与表相同的条目改成表里的英文（就地写回 .ts）
  python3 scripts/i18n_sync.py --check  不改文件：有不一致、或有 tr() 原文没翻译就失败（test.sh 用）
"""
import html
import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from i18n_scan import sources  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TS = ROOT / "demo/i18n/imrtc_demo_en.ts"
SRC = Path(os.environ.get("RTC_I18N_FILE", ROOT.parent / "im-rtc-server/docs/i18n/strings.json"))


def normalize(text: str) -> str | None:
    """表里的 {x} 换成 Qt 的 %1；多个不同占位符桌面没法一一对应，返回 None 表示不参与对齐。"""
    holes = set(re.findall(r"\{(\w+)\}", text))
    if len(holes) > 1:
        return None
    return re.sub(r"\{\w+\}", "%1", text)


def shared_map() -> dict[str, str]:
    strings = json.loads(SRC.read_text(encoding="utf-8"))["strings"]
    out: dict[str, str] = {}
    for row in strings.values():
        zh, en = normalize(row["zh-CN"]), normalize(row["en"])
        if zh is not None and en is not None:
            out.setdefault(zh, en)
    return out


def main() -> int:
    if not SRC.exists():
        print(f"✗ 找不到文案表：{SRC}\n  把 im-rtc-server 克隆到本仓同级，或设 RTC_I18N_FILE。")
        return 1
    check = "--check" in sys.argv
    table = shared_map()
    text = TS.read_text(encoding="utf-8")
    changed: list[str] = []

    def fix(match: re.Match[str]) -> str:
        block = match.group(0)
        source = re.search(r"<source>(.*?)</source>", block, re.S)
        translation = re.search(r"(<translation[^>]*>)(.*?)(</translation>)", block, re.S)
        if not source or not translation:
            return block
        zh = html.unescape(source.group(1))
        want = table.get(zh)
        if want is None or html.unescape(translation.group(2)) == want:
            return block
        changed.append(zh)
        new = translation.group(1).replace(' type="unfinished"', "") + html.escape(want, quote=False) + translation.group(3)
        return block[: translation.start()] + new + block[translation.end():]

    fixed = re.sub(r"<message>.*?</message>", fix, text, flags=re.S)
    if changed and not check:
        TS.write_text(fixed, encoding="utf-8")
    if changed:
        print(f"  {'✗ 与文案表不一致' if check else '已对齐'} {len(changed)} 条：" + "、".join(c[:14] for c in changed[:8]))

    from i18n_scan import tsmap  # 完整性：每个 tr() 中文原文都要有已完成的译文
    ts = tsmap(str(TS))
    missing = [s for s in sources(str(ROOT / "demo")) if s not in ts or not ts[s][0] or "unfinished" in ts[s][1]]
    if missing:
        print(f"  ✗ {len(missing)} 条 tr() 原文没有翻译：" + "、".join(m[:14] for m in missing[:8]))
    if check and (changed or missing):
        return 1
    if not changed and not missing:
        print(f"  翻译完整，与文案表一致（{len(table)} 条参与对齐）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
