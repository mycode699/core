/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

namespace sd
{
class DrawViewShell;
}

namespace sd::inline_actions {

// Called when Impress/Draw object selection changes (DrawViewShell::SelectionHasChanged).
// Shows the popover for a single selected text shape; dismisses when ineligible.
void OnImpressSlideElementSelectionChanged(DrawViewShell& rShell);

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */