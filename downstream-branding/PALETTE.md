# 可圈办公 · Visual System (WPS-modern)

Target: Chinese office modernity on par with WPS Office chrome quality,
while keeping **可圈 blue** brand identity (not WPS red).

## Brand accent

| Token | Hex | Use |
|-------|-----|-----|
| `brand-accent` | `#1677FF` | Primary actions, selection, active |
| `brand-deep` | `#0958D9` | Pressed / deep gradient end |
| `brand-light` | `#69B1FF` | Ring / highlight start |
| `brand-soft` | `#E6F4FF` | Soft fills, chips, hover wash |
| `brand-tint` | `#F0F7FF` | Near-white brand tint |

## Neutrals (chrome)

| Token | Hex | Use |
|-------|-----|-----|
| `bg` | `#F7F8FA` | App chrome / start center rail feel |
| `surface` | `#FFFFFF` | Cards, panels, document surround |
| `text` | `#1F2329` | Primary labels |
| `text-secondary` | `#646A73` | Secondary / helper |
| `border` | `#E5E6EB` | Hairline separators |

## Module accents (icons / file types)

Aligned with common Chinese suite habits:

- Writer → blue `#1677FF`
- Calc → green `#389E0D`
- Impress → orange `#FA541C`
- Draw → gold `#FAAD14`
- Base → teal `#13C2C2`
- Math → magenta `#EB2F96`

## Typography

### UI (chrome)

Platform-first Chinese UI stack (see `officecfg/.../VCL.xcu` `DefaultFonts/zh-cn`):

1. **PingFang SC** (macOS)
2. **Microsoft YaHei UI / 微软雅黑** (Windows)
3. **Noto Sans CJK SC / 思源黑体**
4. HarmonyOS Sans SC / MiSans fallbacks

### Document body (blank Writer)

CJK text prefers **宋体 / Songti SC / SimSun** for WPS-familiar print documents.
Headings / UI / presentations prefer **sans** (PingFang / YaHei).

Brand SVG wordmarks use:

```
'PingFang SC','苹方-简','Microsoft YaHei UI','Microsoft YaHei','微软雅黑','Noto Sans SC',sans-serif
```

## Start Center tiles

- Background: `#E8F3FF` (`15264767`)
- Text: `#1F2329` (`2040617`)

Configured in `officecfg/.../Common.xcu` → `StartCenterThumbnails*Color`.

## Icon generation

From `kdoffice-src`:

```bash
# App / document type marks
python3 downstream-branding/generate_icon_assets.py

# Toolbar theme (default SymbolStyle=colibre_svg): remap LO blues → brand blues
python3 downstream-branding/recolor_colibre_theme.py \
  --pack-to "/path/to/可圈办公.app/Contents/Resources/config"
```

Color remap (see `recolor_colibre_theme.py`):

- `#1E8BCD` → `#1677FF`
- `#0063B1` → `#0958D9`
- `#83BEEC` → `#69B1FF`
- soft LO blues (`#E3F2FD` …) → brand soft blues

Then convert macOS document iconsets → `.icns` and refresh Start Center theme tiles:

- `sysui/desktop/icons/macos/mime-{generic,oasis}/*.iconset` → app `Resources/{generic,oasis}-*.icns`
- `sysui/desktop/icons/hicolor/scalable/apps/{main,writer,calc,...}.svg` → PNGs
- Patch `images_colibre.zip` entries `res/odt_32_8.png` / `ods` / `odp` for Start Center module buttons
- App dock icon: `Resources/main.icns` (from `main` hicolor PNGs via `iconutil`)

## Quiet chrome

Workbench / notebook / AI panels use `shadow-type=none` (no inset frames) for a flatter WPS-like surface.

### Editor chrome defaults (`officecfg`)

| Setting | Value | Why |
|---------|-------|-----|
| Toolbar mode | `TabbedCompact` + `notebookbar_compact.ui` | WPS-like dense ribbon |
| `SymbolSet` | `0` (16×16) | Compact classic toolbar icons |
| `SidebarIconSize` / `NotebookbarIconSize` | `1` (small) | Dense side / ribbon icons |
| `SymbolStyle` | `colibre_svg` | Brand-recolored toolbar theme |

## Do not

- Clone WPS red as product brand (identity collision)
- Hard-code control text colors in C++ (breaks dark mode / high contrast)
- Use green leftover accents (`#1E3A2F`, `#ECFFF3`) in product chrome
