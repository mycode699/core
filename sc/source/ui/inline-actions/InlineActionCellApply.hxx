/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "CellActions.hxx"
#include <rtl/ustring.hxx>
#include <scdllapi.h>

class ScTabViewShell;

namespace sc::inline_actions {

// W4-B Day-5: which CellAction values may write provider content into the sheet.
SC_DLLPUBLIC bool shouldApplyProviderContentToMarkedCell(CellAction eAction);

// Writes provider content to the top-left cell of the current marked range.
// GenerateFormula only; other actions return false (W3.5 full apply deferred).
SC_DLLPUBLIC bool tryApplyProviderContentToMarkedCell(ScTabViewShell& rShell,
                                                      const OUString& rContent,
                                                      CellAction eAction);

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */