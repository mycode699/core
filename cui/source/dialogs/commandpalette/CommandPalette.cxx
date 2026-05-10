/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Translation unit reserved for W2 Day-1b popover glue (sfx2 dispatch,
 * Enter / ESC handlers, debounced query routing into the controller).
 *
 * The controller class itself is now header-only (CommandPalette.hxx)
 * so the controller cppunit can link without libcui — mirrors the
 * FuzzyMatcher / CommandIndex / RecentStore fast-test layout.
 *
 * Library_cui.mk continues to compile this file so the eventual popover
 * symbols land in libcui without having to re-register the path.
 */

#include <commandpalette/CommandPalette.hxx>

namespace cui::commandpalette
{
// W2 Day-1b will add: popover lifecycle, sfx2::SfxDispatcher hookup,
// Enter / ESC keybinding, debounced query routing. Until then this TU
// is intentionally a pure forward-include so libcui linkage stays stable.
} // namespace cui::commandpalette

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
