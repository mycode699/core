#!/usr/bin/env python3
"""Replace user-visible LibreOffice product naming in installed help HTML with CoC Office / 可圈办公."""
from __future__ import annotations
import re
import sys
from pathlib import Path

def patch_html(text: str, zh: bool) -> str:
    pairs_zh = [
        ("LibreOffice Impress", "可圈办公 演示"),
        ("LibreOffice Calc", "可圈办公 表格"),
        ("LibreOffice Writer", "可圈办公 文字"),
        ("LibreOffice Draw", "可圈办公 绘图"),
        ("LibreOffice Base", "可圈办公 数据库"),
        ("LibreOffice Math", "可圈办公 公式"),
        ("LibreOffice Online", "可圈办公 Online"),
    ]
    pairs_en = [
        ("LibreOffice Impress", "CoC Office Impress"),
        ("LibreOffice Calc", "CoC Office Calc"),
        ("LibreOffice Writer", "CoC Office Writer"),
        ("LibreOffice Draw", "CoC Office Draw"),
        ("LibreOffice Base", "CoC Office Base"),
        ("LibreOffice Math", "CoC Office Math"),
        ("LibreOffice Online", "CoC Office Online"),
    ]
    for a, b in (pairs_zh if zh else pairs_en):
        text = text.replace(a, b)
    repl = "可圈办公" if zh else "CoC Office"
    text = re.sub(r"(?<![/\\w.])LibreOffice(?![\\w.-])", repl, text)
    text = text.replace(
        "https://documentation.libreoffice.org/en/join-community/update-help-contents",
        "https://www.03122.com",
    )
    text = text.replace(
        "https://www.libreoffice.org/about-us/credits/",
        "https://www.03122.com",
    )
    return text

def main(root: Path) -> int:
    n = 0
    for p in root.rglob("*.html"):
        zh = "/zh-CN/" in str(p).replace("\\", "/") or p.parts[-2:] == ("help",)  # top indexes
        if "zh-CN" in p.parts:
            zh = True
        elif "en-US" in p.parts:
            zh = False
        else:
            zh = True
        orig = p.read_text(encoding="utf-8", errors="ignore")
        new = patch_html(orig, zh)
        if new != orig:
            p.write_text(new, encoding="utf-8")
            n += 1
    print(f"patched {n} html files under {root}")
    return 0

if __name__ == "__main__":
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    raise SystemExit(main(root))
