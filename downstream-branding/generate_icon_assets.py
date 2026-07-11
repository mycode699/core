#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import shutil
import subprocess
from textwrap import dedent


ROOT = Path(__file__).resolve().parents[1]
HICOLOR = ROOT / "sysui/desktop/icons/hicolor"
MACOS = ROOT / "sysui/desktop/icons/macos"

PNG_SIZES = {
    "16x16": 16,
    "24x24": 24,
    "32x32": 32,
    "48x48": 48,
    "64x64": 64,
    "128x128": 128,
    "256x256": 256,
    "512x512": 512,
    "512x512@2": 1024,
}

MAC_ICONSET_SIZES = [
    ("icon_16x16.png", 16),
    ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32),
    ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512),
    ("icon_512x512@2x.png", 1024),
]

INTRO_EXPORTS = ["intro.png", "intro-highres.png"]
INTRO_PACKAGE_DIRS = [
    ROOT / "icon-themes/colibre/brand",
    ROOT / "icon-themes/colibre/brand_dev",
]

# WPS-modern Chinese office palette: clean neutrals + vivid module accents.
# Product brand stays blue (not WPS red) for 可圈 identity.
BRAND_RING_START = "#69B1FF"
BRAND_RING_END = "#0958D9"
BRAND_SWEEP = "#E6F4FF"
BRAND_ACCENT = "#1677FF"
BRAND_SOFT_WHITE = "#F0F7FF"


@dataclass(frozen=True)
class Palette:
    bg_start: str
    bg_end: str
    accent: str
    soft: str


APP_ICON_NAMES = [
    "main",
    "startcenter",
    "writer",
    "calc",
    "impress",
    "draw",
    "chart",
    "base",
    "math",
]

APP_GENERATED = {
    # Module colors aligned with common Chinese office suite habits (WPS-like).
    "writer": Palette("#69B1FF", "#0958D9", "#1677FF", "#E6F4FF"),
    "calc": Palette("#73D13D", "#237804", "#389E0D", "#F6FFED"),
    "impress": Palette("#FF9C6E", "#D4380D", "#FA541C", "#FFF2E8"),
    "draw": Palette("#FFC53D", "#D48806", "#FAAD14", "#FFFBE6"),
    "chart": Palette("#8C8C8C", "#434343", "#595959", "#F5F5F5"),
    "base": Palette("#36CFC9", "#006D75", "#13C2C2", "#E6FFFB"),
    "math": Palette("#FF85C0", "#C41D7F", "#EB2F96", "#FFF0F6"),
    "basic": Palette("#A6A6A6", "#434343", "#595959", "#FAFAFA"),
}

APP_SVG_GENERATED = {
    "main": Palette("#69B1FF", "#0958D9", "#1677FF", "#E6F4FF"),
    "startcenter": Palette("#69B1FF", "#0958D9", "#1677FF", "#E6F4FF"),
    **APP_GENERATED,
}

DOC_GENERATED = {
    "text": ("writer", False),
    "text-template": ("writer", True),
    "spreadsheet": ("calc", False),
    "spreadsheet-template": ("calc", True),
    "presentation": ("impress", False),
    "presentation-template": ("impress", True),
    "drawing": ("draw", False),
    "drawing-template": ("draw", True),
    "database": ("base", False),
    "formula": ("math", False),
    "master-document": ("chart", False),
    "master-document-template": ("chart", True),
    "extension": ("base", False),
    "empty": ("chart", False),
    "web": ("writer", False),
    "web-template": ("writer", True),
}

HICOLOR_DOC_SVGS = {
    "oasis-text": "text",
    "oasis-text-template": "text-template",
    "oasis-spreadsheet": "spreadsheet",
    "oasis-spreadsheet-template": "spreadsheet-template",
    "oasis-presentation": "presentation",
    "oasis-presentation-template": "presentation-template",
    "oasis-drawing": "drawing",
    "oasis-drawing-template": "drawing-template",
    "oasis-database": "database",
    "oasis-formula": "formula",
    "oasis-master-document": "master-document",
    "oasis-master-document-template": "master-document-template",
    "extension": "extension",
    "oasis-empty": "empty",
    "oasis-web": "web",
    "oasis-web-template": "web-template",
}

MACOS_DOC_SVGS = {
    "web": "web",
    "web-template": "web-template",
}


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


def render_png(svg_path: Path, png_path: Path, size: int) -> None:
    png_path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "sips",
            "-z",
            str(size),
            str(size),
            "-s",
            "format",
            "png",
            str(svg_path),
            "--out",
            str(png_path),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def rect(x: int, y: int, width: int, height: int, rx: int, fill: str, opacity: float | None = None) -> str:
    attrs = [f'x="{x}"', f'y="{y}"', f'width="{width}"', f'height="{height}"', f'rx="{rx}"', f'fill="{fill}"']
    if opacity is not None:
        attrs.append(f'opacity="{opacity}"')
    return "<rect " + " ".join(attrs) + "/>"


def circle(cx: int, cy: int, r: int, fill: str, opacity: float | None = None) -> str:
    attrs = [f'cx="{cx}"', f'cy="{cy}"', f'r="{r}"', f'fill="{fill}"']
    if opacity is not None:
        attrs.append(f'opacity="{opacity}"')
    return "<circle " + " ".join(attrs) + "/>"


def glyph_writer(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          {rect(184, 184, 134, 22, 11, accent, 0.16)}
          {rect(184, 228, 188, 16, 8, accent, 0.88)}
          {rect(184, 264, 164, 16, 8, accent, 0.62)}
          {rect(184, 300, 178, 16, 8, accent, 0.62)}
          {rect(184, 336, 116, 16, 8, accent, 0.62)}
        </g>
        """
    ).strip()


def glyph_calc(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <rect x="184" y="186" width="176" height="176" rx="22" fill="{soft}" opacity=".18" stroke="{accent}" stroke-width="12"/>
          <rect x="184" y="186" width="176" height="34" rx="22" fill="{accent}" opacity=".16"/>
          <path d="M242 186v176M301 186v176M184 244h176M184 303h176" fill="none" stroke="{accent}" stroke-width="12" opacity=".74"/>
        </g>
        """
    ).strip()


def glyph_impress(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <rect x="168" y="188" width="208" height="132" rx="20" fill="{soft}" opacity=".18" stroke="{accent}" stroke-width="12"/>
          {rect(192, 212, 160, 16, 8, accent, 0.84)}
          {rect(214, 246, 116, 14, 7, accent, 0.56)}
          <path d="M272 320v34M236 354h72" fill="none" stroke="{accent}" stroke-linecap="round" stroke-width="12"/>
        </g>
        """
    ).strip()


def glyph_draw(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <circle cx="210" cy="292" r="48" fill="{soft}" opacity=".2" stroke="{accent}" stroke-width="12"/>
          <rect x="274" y="244" width="102" height="102" rx="18" fill="{soft}" opacity=".16" stroke="{accent}" stroke-width="12"/>
          <path d="M176 356h94l-47-82z" fill="{accent}" opacity=".72"/>
        </g>
        """
    ).strip()


def glyph_chart(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <rect x="186" y="264" width="38" height="108" rx="10" fill="{accent}" opacity=".84"/>
          <rect x="244" y="222" width="38" height="150" rx="10" fill="{accent}" opacity=".68"/>
          <rect x="302" y="286" width="38" height="86" rx="10" fill="{accent}" opacity=".52"/>
          <rect x="360" y="238" width="38" height="134" rx="10" fill="{accent}" opacity=".76"/>
          <path d="M184 330l72-62 58 24 86-78" fill="none" stroke="{soft}" stroke-linecap="round" stroke-linejoin="round" stroke-width="16"/>
        </g>
        """
    ).strip()


def glyph_base(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <ellipse cx="256" cy="214" rx="76" ry="28" fill="{soft}" opacity=".22" stroke="{accent}" stroke-width="12"/>
          <path d="M180 214v108c0 18 34 32 76 32s76-14 76-32V214" fill="{soft}" opacity=".14" stroke="{accent}" stroke-width="12"/>
          <path d="M180 256c0 18 34 32 76 32s76-14 76-32M180 298c0 18 34 32 76 32s76-14 76-32" fill="none" stroke="{accent}" stroke-width="12" opacity=".8"/>
        </g>
        """
    ).strip()


def glyph_math(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <path d="M188 226l42 42M230 226l-42 42" fill="none" stroke="{accent}" stroke-linecap="round" stroke-width="16"/>
          <path d="M272 248h64M304 216v64" fill="none" stroke="{accent}" stroke-linecap="round" stroke-width="16"/>
          <path d="M372 230h64M372 270h64" fill="none" stroke="{soft}" stroke-linecap="round" stroke-width="16"/>
          <path d="M196 334h240" fill="none" stroke="{accent}" stroke-linecap="round" stroke-width="16" opacity=".74"/>
        </g>
        """
    ).strip()


def glyph_basic(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <path d="M208 224l-44 50 44 50" fill="none" stroke="{accent}" stroke-linecap="round" stroke-linejoin="round" stroke-width="16"/>
          <path d="M304 224l44 50-44 50" fill="none" stroke="{accent}" stroke-linecap="round" stroke-linejoin="round" stroke-width="16"/>
          <path d="M270 204l-28 140" fill="none" stroke="{soft}" stroke-linecap="round" stroke-width="16"/>
        </g>
        """
    ).strip()


def glyph_master(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <path d="M220 204h94l42 42v118h-136z" fill="{soft}" opacity=".16" stroke="{accent}" stroke-linejoin="round" stroke-width="12"/>
          <path d="M176 246h94l42 42v118H176z" fill="{soft}" opacity=".28" stroke="{accent}" stroke-linejoin="round" stroke-width="12"/>
          {rect(204, 304, 78, 14, 7, accent, 0.62)}
          {rect(204, 338, 78, 14, 7, accent, 0.62)}
        </g>
        """
    ).strip()


def glyph_extension(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <path d="M228 214h34c0 18 26 18 26 0h34v34c-18 0-18 26 0 26v34h-34c0-18-26-18-26 0h-34v-34c18 0 18-26 0-26z" fill="{soft}" opacity=".24" stroke="{accent}" stroke-linejoin="round" stroke-width="12"/>
          <circle cx="275" cy="261" r="14" fill="{accent}" opacity=".84"/>
        </g>
        """
    ).strip()


def glyph_empty(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <rect x="182" y="188" width="148" height="176" rx="24" fill="none" stroke="{accent}" stroke-width="12" stroke-dasharray="18 16"/>
          {rect(212, 232, 88, 14, 7, accent, 0.28)}
          {rect(212, 266, 120, 14, 7, accent, 0.22)}
        </g>
        """
    ).strip()


def glyph_web(accent: str, soft: str) -> str:
    return dedent(
        f"""
        <g>
          <rect x="168" y="196" width="208" height="144" rx="18" fill="{soft}" opacity=".16" stroke="{accent}" stroke-width="12"/>
          {rect(168, 196, 208, 32, 18, accent, 0.18)}
          {circle(194, 212, 6, accent, 0.72)}
          {circle(214, 212, 6, accent, 0.56)}
          {circle(234, 212, 6, accent, 0.4)}
          <circle cx="272" cy="286" r="42" fill="none" stroke="{accent}" stroke-width="12" opacity=".8"/>
          <path d="M230 286h84M272 244a62 62 0 0 0 0 84M272 244a62 62 0 0 1 0 84" fill="none" stroke="{accent}" stroke-width="10" opacity=".64"/>
        </g>
        """
    ).strip()


GLYPHS = {
    "writer": glyph_writer,
    "calc": glyph_calc,
    "impress": glyph_impress,
    "draw": glyph_draw,
    "chart": glyph_chart,
    "base": glyph_base,
    "math": glyph_math,
    "basic": glyph_basic,
    "master": glyph_master,
    "extension": glyph_extension,
    "empty": glyph_empty,
    "web": glyph_web,
}


def build_branded_app_svg(name: str, palette: Palette) -> str:
    if name in {"main", "startcenter"}:
        glyph = ""
    else:
        glyph_name = "chart" if name == "chart" else name
        glyph = GLYPHS[glyph_name](palette.accent, palette.soft)
    return dedent(
        f"""
        <svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <linearGradient id="bg" x1="72" y1="48" x2="448" y2="464" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="{palette.bg_start}"/>
              <stop offset="1" stop-color="{palette.bg_end}"/>
            </linearGradient>
            <linearGradient id="panel" x1="140" y1="96" x2="372" y2="432" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="#ffffff"/>
              <stop offset="1" stop-color="#f5fbff"/>
            </linearGradient>
            <linearGradient id="fold" x1="286" y1="96" x2="362" y2="172" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="#eaf6ff"/>
              <stop offset="1" stop-color="#ffffff"/>
            </linearGradient>
            <linearGradient id="ring" x1="120" y1="316" x2="216" y2="412" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="{BRAND_RING_START}"/>
              <stop offset="1" stop-color="{BRAND_RING_END}"/>
            </linearGradient>
          </defs>
          <rect x="40" y="40" width="432" height="432" rx="108" fill="url(#bg)"/>
          <rect x="40" y="40" width="432" height="432" rx="108" fill="none" opacity=".14" stroke="#ffffff" stroke-width="8"/>
          <path d="M164 96h122l74 74v246c0 22.091-17.909 40-40 40H164c-22.091 0-40-17.909-40-40V136c0-22.091 17.909-40 40-40z" fill="url(#panel)"/>
          <path d="M286 96v54c0 11.046 8.954 20 20 20h54z" fill="url(#fold)"/>
          <circle cx="166" cy="364" r="42" fill="none" stroke="url(#ring)" stroke-width="24"/>
          <circle cx="166" cy="364" r="12" fill="{BRAND_SOFT_WHITE}"/>
          <path d="M196 333a42 42 0 0 1 9 24" fill="none" stroke="{BRAND_SWEEP}" stroke-linecap="round" stroke-width="8"/>
          {glyph}
        </svg>
        """
    ).strip() + "\n"


def build_module_app_svg(name: str, palette: Palette) -> str:
    glyph_name = "chart" if name == "chart" else name
    glyph = GLYPHS[glyph_name](palette.accent, palette.soft)
    return dedent(
        f"""
        <svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <linearGradient id="bg" x1="72" y1="48" x2="448" y2="464" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="{palette.bg_start}"/>
              <stop offset="1" stop-color="{palette.bg_end}"/>
            </linearGradient>
            <linearGradient id="panel" x1="132" y1="112" x2="380" y2="400" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="#ffffff"/>
              <stop offset="1" stop-color="#f8fbff"/>
            </linearGradient>
          </defs>
          <rect x="40" y="40" width="432" height="432" rx="104" fill="url(#bg)"/>
          <rect x="96" y="96" width="320" height="320" rx="84" fill="#ffffff" opacity=".16"/>
          <rect x="128" y="112" width="256" height="288" rx="52" fill="url(#panel)"/>
          <rect x="128" y="112" width="256" height="48" rx="52" fill="{palette.accent}" opacity=".14"/>
          {circle(166, 136, 6, palette.accent, 0.72)}
          {circle(188, 136, 6, palette.accent, 0.5)}
          {circle(210, 136, 6, palette.accent, 0.34)}
          {glyph}
        </svg>
        """
    ).strip() + "\n"


def build_app_svg(name: str, palette: Palette) -> str:
    if name in {"main", "startcenter"}:
        return build_branded_app_svg(name, palette)
    return build_module_app_svg(name, palette)


def doc_key_to_palette(doc_key: str) -> Palette:
    return APP_GENERATED[DOC_GENERATED[doc_key][0]]


def doc_glyph(doc_key: str, accent: str, soft: str) -> str:
    if doc_key.startswith("text"):
        glyph = GLYPHS["writer"](accent, soft)
        return f'<g transform="translate(46 58)"><g transform="scale(.72)">{glyph}</g></g>'
    if doc_key.startswith("spreadsheet"):
        glyph = GLYPHS["calc"](accent, soft)
        return f'<g transform="translate(48 58)"><g transform="scale(.72)">{glyph}</g></g>'
    if doc_key.startswith("presentation"):
        glyph = GLYPHS["impress"](accent, soft)
        return f'<g transform="translate(48 58)"><g transform="scale(.72)">{glyph}</g></g>'
    if doc_key.startswith("drawing"):
        glyph = GLYPHS["draw"](accent, soft)
        return f'<g transform="translate(48 58)"><g transform="scale(.72)">{glyph}</g></g>'
    if doc_key == "database":
        glyph = GLYPHS["base"](accent, soft)
        return f'<g transform="translate(36 62)"><g transform="scale(.74)">{glyph}</g></g>'
    if doc_key == "formula":
        glyph = GLYPHS["math"](accent, soft)
        return f'<g transform="translate(40 62)"><g transform="scale(.74)">{glyph}</g></g>'
    if doc_key.startswith("master-document"):
        glyph = GLYPHS["master"](accent, soft)
        return f'<g transform="translate(46 66)"><g transform="scale(.72)">{glyph}</g></g>'
    if doc_key == "extension":
        glyph = GLYPHS["extension"](accent, soft)
        return f'<g transform="translate(52 72)"><g transform="scale(.7)">{glyph}</g></g>'
    if doc_key == "empty":
        glyph = GLYPHS["empty"](accent, soft)
        return f'<g transform="translate(18 26)"><g transform="scale(.82)">{glyph}</g></g>'
    if doc_key.startswith("web"):
        glyph = GLYPHS["web"](accent, soft)
        return f'<g transform="translate(48 62)"><g transform="scale(.72)">{glyph}</g></g>'
    raise ValueError(doc_key)


def build_doc_svg(doc_key: str, palette: Palette, template: bool) -> str:
    glyph = doc_glyph(doc_key, palette.accent, palette.soft)
    ribbon = ""
    if template:
        ribbon = dedent(
            """
            <path d="M116 84h82l-82 82z" fill="#1677FF"/>
            <path d="M150 112l8 15 17 2-12 12 3 17-16-8-15 8 3-17-12-12 17-2z" fill="#F0F7FF"/>
            """
        ).strip()
    return dedent(
        f"""
        <svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <linearGradient id="accent" x1="116" y1="110" x2="178" y2="420" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="{palette.bg_start}"/>
              <stop offset="1" stop-color="{palette.bg_end}"/>
            </linearGradient>
            <linearGradient id="panel" x1="116" y1="84" x2="360" y2="444" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="#ffffff"/>
              <stop offset="1" stop-color="#f8fbff"/>
            </linearGradient>
            <linearGradient id="fold" x1="278" y1="84" x2="348" y2="154" gradientUnits="userSpaceOnUse">
              <stop offset="0" stop-color="#edf5ff"/>
              <stop offset="1" stop-color="#ffffff"/>
            </linearGradient>
          </defs>
          <path d="M132 98h130l74 74v238c0 22-18 40-40 40H132c-22 0-40-18-40-40V138c0-22 18-40 40-40z" fill="#000000" opacity=".08"/>
          <path d="M116 84h130l74 74v238c0 22-18 40-40 40H116c-22 0-40-18-40-40V124c0-22 18-40 40-40z" fill="url(#panel)"/>
          <rect x="116" y="84" width="58" height="352" rx="22" fill="url(#accent)"/>
          <path d="M246 84v54c0 11.046 8.954 20 20 20h54z" fill="url(#fold)"/>
          {ribbon}
          {glyph}
        </svg>
        """
    ).strip() + "\n"


def generate_app_svgs() -> None:
    for name, palette in APP_SVG_GENERATED.items():
        content = build_app_svg(name, palette)
        write_text(HICOLOR / "scalable/apps" / f"{name}.svg", content)
        if name != "basic":
            for size_dir in ("16x16", "24x24", "48x48"):
                write_text(HICOLOR / size_dir / "apps" / f"{name}.svg", content)


def generate_doc_svgs() -> dict[str, Path]:
    doc_contents: dict[str, str] = {}
    hicolor_sources: dict[str, Path] = {}
    for doc_key in set(HICOLOR_DOC_SVGS.values()) | set(MACOS_DOC_SVGS.values()):
        _, template = DOC_GENERATED[doc_key]
        palette = doc_key_to_palette(doc_key)
        doc_contents[doc_key] = build_doc_svg(doc_key, palette, template)

    for target_name, doc_key in HICOLOR_DOC_SVGS.items():
        content = doc_contents[doc_key]
        scalable_path = HICOLOR / "scalable/mimetypes" / f"{target_name}.svg"
        write_text(scalable_path, content)
        hicolor_sources[doc_key] = scalable_path
        for size_dir in ("16x16", "24x24", "48x48"):
            write_text(HICOLOR / size_dir / "mimetypes" / f"{target_name}.svg", content)

    for target_name, doc_key in MACOS_DOC_SVGS.items():
        write_text(MACOS / "scalable" / f"{target_name}.svg", doc_contents[doc_key])

    return hicolor_sources


def render_hicolor_app_pngs() -> None:
    for name in APP_ICON_NAMES:
        svg_source = HICOLOR / "scalable/apps" / f"{name}.svg"
        for size_dir, size in PNG_SIZES.items():
            render_png(svg_source, HICOLOR / size_dir / "apps" / f"{name}.png", size)


def render_hicolor_doc_pngs(doc_sources: dict[str, Path]) -> None:
    for hicolor_name, doc_key in HICOLOR_DOC_SVGS.items():
        svg_source = doc_sources[doc_key]
        for size_dir, size in PNG_SIZES.items():
            render_png(svg_source, HICOLOR / size_dir / "mimetypes" / f"{hicolor_name}.png", size)


def render_macos_iconsets() -> None:
    for icon_name in MACOS_DOC_SVGS:
        svg_source = MACOS / "scalable" / f"{icon_name}.svg"
        generic_dir = MACOS / "mime-generic" / f"{icon_name}.iconset"
        oasis_dir = MACOS / "mime-oasis" / f"{icon_name}.iconset"
        generic_dir.mkdir(parents=True, exist_ok=True)
        oasis_dir.mkdir(parents=True, exist_ok=True)
        for file_name, size in MAC_ICONSET_SIZES:
            generic_png = generic_dir / file_name
            oasis_png = oasis_dir / file_name
            render_png(svg_source, generic_png, size)
            shutil.copyfile(generic_png, oasis_png)


def sync_intro_exports() -> None:
    branding_root = ROOT / "downstream-branding"
    for name in INTRO_EXPORTS:
        src = branding_root / name
        for out_dir in INTRO_PACKAGE_DIRS:
            out_dir.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, out_dir / name)


def main() -> None:
    generate_app_svgs()
    doc_sources = generate_doc_svgs()
    render_hicolor_app_pngs()
    render_hicolor_doc_pngs(doc_sources)
    render_macos_iconsets()
    sync_intro_exports()


if __name__ == "__main__":
    main()
