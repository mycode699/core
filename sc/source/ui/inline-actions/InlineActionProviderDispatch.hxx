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

class SfxObjectShell;

namespace weld
{
class Widget;
}

namespace sc::inline_actions {

// W4 Day-3: CellAction token → W1 offline ProviderRequest.capability.
//
// | CellAction token   | W1 capability  | Provider call | DiffReview |
// |--------------------|----------------|---------------|------------|
// | explain-data       | summarize      | yes           | no         |
// | suggest-chart      | intent-to-uno  | yes           | yes        |
// | generate-formula   | intent-to-uno  | yes           | yes        |
// | format-clean       | format-fix     | yes           | yes        |
// | format-change      | format-fix     | yes           | yes        |
//
// W4 action tokens stay on the inline-action-request envelope; only the
// dispatch layer translates to the four-token W1 offline allowlist.

SC_DLLPUBLIC OUString offlineCapabilityForCellAction(CellAction eAction);

/// UNO Provider::call + SAL_INFO; opens DiffReview when actionRoutesToDiff().
SC_DLLPUBLIC void dispatchCalcInlineAction(const OUString& rJsonRequest, CellAction eAction,
                                           SfxObjectShell* pDocShell,
                                           weld::Widget* pDiffReviewParent);

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */