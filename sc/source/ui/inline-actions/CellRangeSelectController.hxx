/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

class ScTabViewShell;

namespace sc::inline_actions {

// Called when Calc cell/range selection changes (ScTabView::SelectionChanged).
// Shows the popover for a non-empty marked range; dismisses when ineligible.
void OnCalcCellRangeSelectionChanged(ScTabViewShell& rShell);

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */