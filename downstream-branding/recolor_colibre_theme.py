#!/usr/bin/env python3
"""Recolor LibreOffice colibre(_dark)_svg toolbar themes to 可圈 WPS-modern brand blues.

Does not redesign glyphs — only remaps LO blue family and a few chrome neutrals
so the default SymbolStyle=colibre_svg looks cohesive with product branding.
"""
from __future__ import annotations

import argparse
import re
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
THEMES = [
    ROOT / "icon-themes" / "colibre_svg",
    ROOT / "icon-themes" / "colibre_dark_svg",
]

# LO Colibre blues → 可圈 brand blues (case-insensitive replace via hex normalize)
COLOR_MAP = {
    # Primary LO blues
    "1E8BCD": "1677FF",
    "0063B1": "0958D9",
    "83BEEC": "69B1FF",
    "0E5A8C": "0958D9",
    "2E75B6": "1677FF",
    "5B9BD5": "69B1FF",
    "9DC3E6": "BAE0FF",
    "BDD7EE": "E6F4FF",
    # Older LO / accent variants often in cmd icons
    "0084C8": "1677FF",
    "0070C0": "0958D9",
    "00A0F0": "69B1FF",
    "4FC3F7": "69B1FF",
    "039BE5": "1677FF",
    "0288D1": "0958D9",
    # Soft document fills that read "old LO blue"
    "E3F2FD": "E6F4FF",
    "BBDEFB": "BAE0FF",
    "90CAF9": "91CAFF",
}

HEX_RE = re.compile(r"#([0-9A-Fa-f]{6})\b")
HEX3_RE = re.compile(r"#([0-9A-Fa-f]{3})\b")


def expand3(h: str) -> str:
    return "".join(c * 2 for c in h)


def map_hex(h: str) -> str | None:
    key = h.upper()
    if key in COLOR_MAP:
        return COLOR_MAP[key]
    return None


def recolor_svg(text: str) -> tuple[str, int]:
    n = 0

    def repl6(m: re.Match[str]) -> str:
        nonlocal n
        mapped = map_hex(m.group(1))
        if mapped:
            n += 1
            return "#" + mapped
        return m.group(0)

    def repl3(m: re.Match[str]) -> str:
        nonlocal n
        full = expand3(m.group(1))
        mapped = map_hex(full)
        if mapped:
            n += 1
            return "#" + mapped
        return m.group(0)

    out = HEX_RE.sub(repl6, text)
    out = HEX3_RE.sub(repl3, out)
    return out, n


def recolor_tree(theme_dir: Path) -> dict[str, int]:
    stats = {"files": 0, "changed": 0, "replacements": 0}
    if not theme_dir.is_dir():
        return stats
    for path in theme_dir.rglob("*.svg"):
        stats["files"] += 1
        original = path.read_text(encoding="utf-8", errors="ignore")
        updated, n = recolor_svg(original)
        if n and updated != original:
            path.write_text(updated, encoding="utf-8")
            stats["changed"] += 1
            stats["replacements"] += n
    return stats


def pack_theme_zip(theme_dir: Path, zip_path: Path) -> None:
    """Pack theme directory into LO images_*.zip layout (paths relative to theme root)."""
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    tmp = zip_path.with_suffix(".zip.tmp")
    with zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(theme_dir.rglob("*")):
            if not path.is_file():
                continue
            if path.name in {"COPYING-ICONS", "README.txt", "links.txt"}:
                continue
            arc = path.relative_to(theme_dir).as_posix()
            zf.write(path, arcname=arc)
    tmp.replace(zip_path)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--pack-to",
        type=Path,
        default=None,
        help="Directory that receives images_colibre_svg.zip and images_colibre_dark_svg.zip",
    )
    args = ap.parse_args()

    for theme in THEMES:
        stats = recolor_tree(theme)
        print(
            f"{theme.name}: files={stats['files']} changed={stats['changed']} "
            f"replacements={stats['replacements']}"
        )

    if args.pack_to:
        out = args.pack_to
        pack_theme_zip(ROOT / "icon-themes/colibre_svg", out / "images_colibre_svg.zip")
        pack_theme_zip(ROOT / "icon-themes/colibre_dark_svg", out / "images_colibre_dark_svg.zip")
        print(f"packed zips into {out}")


if __name__ == "__main__":
    main()
