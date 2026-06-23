/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

class SwWrtShell;

namespace sw::inline_actions {

// Called when Writer text selection changes are committed by SwWrtShell.
// Shows the popover for a non-empty single-paragraph text selection; dismisses
// it when the selection is cleared or no longer eligible.
void OnWriterSelectionChanged(SwWrtShell& rShell);

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
