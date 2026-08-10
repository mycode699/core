#!/usr/bin/env bash
# Clavue / 可圈办公 brand integrity smoke check for a built app bundle (or BUILDDIR instdir).
# Usage:
#   ./kq-brand-smoke.sh
#   ./kq-brand-smoke.sh /path/to/可圈办公.app
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRAND="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP="${1:-}"
if [[ -z "$APP" ]]; then
  # Prefer BUILDDIR next to common worktrees
  for cand in \
    "$PWD/instdir/可圈办公.app" \
    "$PWD/instdir"/*.app \
    "/Volumes/MobileDrive/devpc/可点office/instdir/可圈办公.app"
  do
    # shellcheck disable=SC2086
    if [[ -d $cand ]]; then APP=$cand; break; fi
  done
fi

if [[ -z "${APP:-}" || ! -d "$APP" ]]; then
  echo "FAIL: app bundle not found. Pass path to 可圈办公.app" >&2
  exit 2
fi

RES="$APP/Contents/Resources"
FAIL=0
pass() { echo "  OK  $*"; }
fail() { echo "  FAIL $*"; FAIL=$((FAIL + 1)); }
warn() { echo "  WARN $*"; }

echo "== kq-brand-smoke =="
echo "APP=$APP"
echo "BRAND=$BRAND"

# --- shell brand SVGs ---
for f in logo.svg logo_inverted.svg logo-sc.svg logo-sc_inverted.svg about.svg; do
  if [[ -f "$RES/shell/$f" ]]; then
    if grep -q '1D4FFF\|22D3EE' "$RES/shell/$f" 2>/dev/null; then
      pass "shell/$f has Clavue gradient"
    else
      fail "shell/$f missing Clavue colors"
    fi
  else
    fail "missing shell/$f"
  fi
done

# --- intro splash dimensions (LO progress coords assume 644x199 / 990x254) ---
if command -v sips >/dev/null 2>&1; then
  w=$(sips -g pixelWidth "$RES/intro.png" 2>/dev/null | awk '/pixelWidth/{print $2}')
  h=$(sips -g pixelHeight "$RES/intro.png" 2>/dev/null | awk '/pixelHeight/{print $2}')
  if [[ "$w" == "644" && "$h" == "199" ]]; then
    pass "intro.png ${w}x${h}"
  else
    fail "intro.png expected 644x199 got ${w}x${h}"
  fi
  wh=$(sips -g pixelWidth "$RES/intro-highres.png" 2>/dev/null | awk '/pixelWidth/{print $2}')
  hh=$(sips -g pixelHeight "$RES/intro-highres.png" 2>/dev/null | awk '/pixelHeight/{print $2}')
  if [[ "$wh" == "990" && "$hh" == "254" ]]; then
    pass "intro-highres.png ${wh}x${hh}"
  else
    fail "intro-highres.png expected 990x254 got ${wh}x${hh}"
  fi
else
  warn "sips not available; skip intro size check"
fi

# --- sofficerc progress bar Clavue blue ---
RC="$RES/sofficerc"
if [[ -f "$RC" ]]; then
  if grep -q 'ProgressBarColor=29,79,255' "$RC"; then
    pass "sofficerc ProgressBarColor=#1D4FFF"
  else
    fail "sofficerc ProgressBarColor not Clavue (want 29,79,255)"
    grep ProgressBarColor "$RC" || true
  fi
  if grep -q 'ProgressFrameColor=21,62,214' "$RC"; then
    pass "sofficerc ProgressFrameColor=#153ED6"
  else
    fail "sofficerc ProgressFrameColor not brand-deep"
  fi
else
  fail "missing sofficerc"
fi

# --- main.icns present ---
if [[ -f "$RES/main.icns" ]]; then
  sz=$(stat -f%z "$RES/main.icns" 2>/dev/null || stat -c%s "$RES/main.icns")
  if [[ "$sz" -gt 100000 ]]; then
    pass "main.icns size=$sz"
  else
    fail "main.icns suspiciously small ($sz)"
  fi
else
  fail "missing main.icns"
fi

# --- suite module icns ---
for name in writer calc impress draw; do
  if [[ -f "$RES/${name}.icns" ]]; then
    pass "${name}.icns"
  else
    warn "missing ${name}.icns"
  fi
done

# --- kq_sc icons in colibre zip ---
ZIP="$RES/config/images_colibre.zip"
if [[ -f "$ZIP" ]]; then
  n=$(unzip -l "$ZIP" 2>/dev/null | grep -c 'res/kq_sc_' || true)
  if [[ "$n" -ge 20 ]]; then
    pass "images_colibre.zip kq_sc icons=$n"
  else
    fail "images_colibre.zip kq_sc icons too few ($n)"
  fi
else
  fail "missing config/images_colibre.zip"
fi

# --- skins accent ---
SKIN="$RES/skins/business-clear/theme.json"
if [[ -f "$SKIN" ]]; then
  if grep -q '#1D4FFF' "$SKIN"; then
    pass "business-clear accent=#1D4FFF"
  else
    fail "business-clear accent not #1D4FFF"
    grep accent "$SKIN" || true
  fi
else
  warn "no business-clear theme.json"
fi


# --- user-facing copy brand (no competitor / unbranded AI) ---
SC_UI="$RES/config/soffice.cfg/sfx/ui/startcenter.ui"
AI_UI="$RES/config/soffice.cfg/sfx/ui/aichatpanel.ui"
PERM_UI="$RES/config/soffice.cfg/sfx/ui/kq_permission_prompt.ui"
if [[ -f "$SC_UI" ]]; then
  if grep -qE '对标 WPS|Acrobat|LibreOffice' "$SC_UI"; then
    fail "startcenter.ui still has competitor/LO user-facing terms"
    grep -nE '对标 WPS|Acrobat|LibreOffice' "$SC_UI" | head -5 || true
  else
    pass "startcenter.ui free of competitor terms"
  fi
  if grep -q '可圈 AI' "$SC_UI"; then
    pass "startcenter.ui mentions 可圈 AI"
  else
    warn "startcenter.ui missing 可圈 AI string (may be painted in C++)"
  fi
else
  warn "startcenter.ui not installed under config"
fi
if [[ -f "$AI_UI" ]]; then
  if grep -q '可圈 AI' "$AI_UI"; then
    pass "aichatpanel.ui branded 可圈 AI"
  else
    fail "aichatpanel.ui missing 可圈 AI"
  fi
fi
if [[ -f "$PERM_UI" ]]; then
  if grep -q '可圈办公' "$PERM_UI"; then
    pass "permission prompt branded 可圈办公"
  else
    fail "permission prompt missing 可圈办公"
  fi
fi
if [[ -f "$RES/docs/user-guide-start-center.md" ]]; then
  pass "user-guide installed under Resources/docs"
else
  warn "user-guide not in app Resources/docs"
fi
if [[ -f "$RES/docs/brand-voice-checklist.md" ]]; then
  pass "brand-voice-checklist installed"
else
  warn "brand-voice-checklist missing in app"
fi

# --- product identity ---
if [[ -f "$RES/bootstraprc" ]]; then
  if grep -q '可圈办公' "$RES/bootstraprc" "$APP/Contents/Info.plist" 2>/dev/null; then
    pass "product name 可圈办公 present"
  else
    warn "could not confirm product name in bootstrap/Info"
  fi
fi
if [[ -f "$RES/versionrc" ]]; then
  if grep -qiE 'libreoffice\.org|documentfoundation' "$RES/versionrc"; then
    fail "versionrc still points to LibreOffice update hosts"
  else
    pass "versionrc free of LO update hosts"
  fi
  if grep -q 'Vendor=可圈办公\|Vendor=CoC' "$RES/versionrc"; then
    pass "versionrc vendor is CoC/可圈"
  else
    warn "versionrc vendor line unexpected"
  fi
fi
if [[ -f "$RES/resource/zh_CN/LC_MESSAGES/cui.mo" ]]; then
  if grep -a -q '衍生自 LibreOffice' "$RES/resource/zh_CN/LC_MESSAGES/cui.mo" 2>/dev/null; then
    fail "cui.mo still contains 衍生自 LibreOffice"
  else
    pass "cui.mo About free of 衍生自 LibreOffice"
  fi
fi

# --- source brand parity (optional) ---
if [[ -f "$BRAND/logo-sc.svg" && -f "$RES/shell/logo-sc.svg" ]]; then
  if command -v md5 >/dev/null 2>&1; then
    a=$(md5 -q "$BRAND/logo-sc.svg")
    b=$(md5 -q "$RES/shell/logo-sc.svg")
    if [[ "$a" == "$b" ]]; then
      pass "logo-sc.svg matches downstream-branding"
    else
      warn "logo-sc.svg differs from brand source (runtime may be fine)"
    fi
  fi
fi


# --- donation banner source (dev tree) ---
if [[ -f "$SRC_ROOT/include/sfx2/donationbanner.hrc" ]]; then
  if grep -q 'LibreOffice is free software' "$SRC_ROOT/include/sfx2/donationbanner.hrc"; then
    fail "donation banner still LO community copy"
  else
    pass "donation banner not LO community copy"
  fi
fi


# --- filter UI names (zh-CN langpack) ---
FCFG_ZH="$RES/registry/res/fcfg_langpack_zh-CN.xcd"
if [[ -f "$FCFG_ZH" ]]; then
  if grep -q '可圈办公 文字文档' "$FCFG_ZH" 2>/dev/null || grep -Fq $'可圈办公 文字文档' "$FCFG_ZH"; then
    pass "filter zh-CN: 可圈办公 文字文档 present"
  else
    # UTF-8 grep fallback
    if python3 -c "import sys; sys.exit(0 if '可圈办公 文字文档' in open(sys.argv[1],encoding='utf-8',errors='ignore').read() else 1)" "$FCFG_ZH"; then
      pass "filter zh-CN: 可圈办公 文字文档 present"
    else
      fail "filter zh-CN missing 可圈办公 文字文档"
    fi
  fi
else
  warn "fcfg_langpack_zh-CN.xcd not found"
fi

echo "== result: $FAIL failure(s) =="
exit "$FAIL"
