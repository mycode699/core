/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Start Center day/night tokens — single source aligned with
 * downstream-branding/PALETTE.md (Fluent / 可圈 blue, not Apple/Pages third colors).
 */

#pragma once

#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <tools/color.hxx>

namespace sfx2::sc_theme
{
/// True when the active dialog/window surface is dark.
inline bool isDark()
{
    return Application::GetSettings().GetStyleSettings().GetDialogColor().IsDark();
}

// —— Clavue brand mark tokens (clavue_icon_refined_final.svg) ——
// Gradient spine: #1D4FFF → #22D3EE
inline Color brandAccent() { return Color(0x1D, 0x4F, 0xFF); }
inline Color brandDeep() { return Color(0x15, 0x3E, 0xD6); }
inline Color brandLight() { return Color(0x22, 0xD3, 0xEE); }
inline Color brandSoft() { return Color(0xE8, 0xF0, 0xFF); }
inline Color brandTint() { return Color(0xF5, 0xF8, 0xFF); }

// Module accents (Writer aligned to brand blue; Calc/Impress distinct)
inline Color moduleWriter() { return Color(0x1D, 0x4F, 0xFF); }
inline Color moduleCalc() { return Color(0x38, 0x9E, 0x0D); }
inline Color moduleImpress() { return Color(0xFA, 0x54, 0x1C); }
inline Color modulePdf() { return Color(0xCF, 0x13, 0x22); }

/// Calm premium surfaces + text for Start Center custom paint.
struct Tokens
{
    bool bDark = false;

    Color canvas; ///< main workbench / gallery field
    Color rail; ///< left nav / chrome rail
    Color card; ///< elevated white/gray card
    Color cardHover;
    Color cardBorder;
    Color cardShadow; ///< soft under-card fill (drawn solid offset)
    Color textPrimary;
    Color textSecondary;
    Color textTertiary;
    Color divider;
    Color accent; ///< brand blue #1D4FFF (Clavue)
    Color accentSoft; ///< soft blue wash
    Color selectRing; ///< selection ring = brand accent (not Numbers green)
    Color chipIdle;
    Color chipIdleBorder;
    Color pillSelected;
    Color pillSelectedBorder;
    Color paper; ///< document preview paper (stays light in dark)
    Color paperEdge;
    Color paperWash;
    Color moreHover;
    Color moreLink; ///< brand blue (not system #007AFF)
    Color cancelFill;
    Color cancelBorder;
    Color createFill; ///< primary CTA = brand blue #1D4FFF
};

inline Tokens tokens()
{
    const StyleSettings& r = Application::GetSettings().GetStyleSettings();
    Tokens t;
    t.bDark = r.GetDialogColor().IsDark();
    t.accent = brandAccent();
    t.selectRing = brandAccent();
    t.createFill = brandAccent();
    // Document paper stays light in both themes.
    t.paper = COL_WHITE;
    t.paperEdge = Color(0xE5, 0xE6, 0xEB);
    t.paperWash = Color(0xF7, 0xF8, 0xFA);

    if (!t.bDark)
    {
        // PALETTE neutrals
        t.canvas = Color(0xFF, 0xFF, 0xFF); // surface
        t.rail = Color(0xF7, 0xF8, 0xFA); // bg
        t.card = COL_WHITE;
        t.cardHover = Color(0xF7, 0xF8, 0xFA);
        t.cardBorder = Color(0xE5, 0xE6, 0xEB);
        t.cardShadow = Color(0xE8, 0xE8, 0xED);
        t.textPrimary = Color(0x1F, 0x23, 0x29);
        t.textSecondary = Color(0x64, 0x6A, 0x73);
        t.textTertiary = Color(0x8F, 0x95, 0x9E);
        t.divider = Color(0xE5, 0xE6, 0xEB);
        t.accentSoft = brandSoft();
        t.chipIdle = Color(0xF7, 0xF8, 0xFA);
        t.chipIdleBorder = Color(0xE5, 0xE6, 0xEB);
        t.pillSelected = COL_WHITE;
        t.pillSelectedBorder = Color(0xE5, 0xE6, 0xEB);
        t.moreHover = brandSoft();
        t.moreLink = brandAccent();
        t.cancelFill = COL_WHITE;
        t.cancelBorder = Color(0xD0, 0xD3, 0xD6);
    }
    else
    {
        Color aWin = r.GetWindowColor();
        if (!aWin.IsDark() || aWin.GetLuminance() > 90)
            aWin = Color(0x1C, 0x1C, 0x1E);
        t.canvas = aWin;
        t.rail = Color(0x28, 0x28, 0x2A);
        t.card = Color(0x2C, 0x2C, 0x2E);
        t.cardHover = Color(0x3A, 0x3A, 0x3C);
        t.cardBorder = Color(0x3A, 0x3A, 0x3C);
        t.cardShadow = aWin;
        t.textPrimary = Color(0xF5, 0xF5, 0xF7);
        t.textSecondary = Color(0xAE, 0xAE, 0xB2);
        t.textTertiary = Color(0x8E, 0x8E, 0x93);
        t.divider = Color(0x3A, 0x3A, 0x3C);
        t.accentSoft = Color(0x1A, 0x33, 0x55);
        t.chipIdle = aWin;
        t.chipIdleBorder = Color(0x48, 0x48, 0x4A);
        t.pillSelected = Color(0x3A, 0x3A, 0x3C);
        t.pillSelectedBorder = Color(0x48, 0x48, 0x4A);
        t.moreHover = Color(0x1A, 0x33, 0x55);
        t.moreLink = brandLight(); // readable brand-light on dark
        t.cancelFill = Color(0x3A, 0x3A, 0x3C);
        t.cancelBorder = Color(0x48, 0x48, 0x4A);
        t.paperEdge = Color(0xC7, 0xC7, 0xCC);
        t.paperWash = Color(0xEB, 0xEB, 0xF0);
    }
    return t;
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
