/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "CellActions.hxx"
#include <memory>
#include <rtl/ustring.hxx>
#include <tools/gen.hxx>

class ScTabViewShell;
class SfxObjectShell;

namespace weld
{
class Widget;
}

// W4 Calc inline-action popover lifecycle. Day-1 wires weld::Popover;
// dispatchSelected() is signal-only until W1 provider + W4-D diff land.

namespace sc::inline_actions {

class CellRangePopover
{
public:
    virtual ~CellRangePopover() = default;

    // Anchor the popover at rRect in screen coordinates for the cell
    // range identified by rCellRange (e.g. "Sheet1.B2:D5").
    virtual void open(const tools::Rectangle& rRect,
                      const rtl::OUString& rCellRange) = 0;
    virtual void close() = 0;

    // W4 Day-3: document shell + weld parent for Provider / DiffReview dispatch.
    virtual void setDispatchContext(SfxObjectShell* pDocShell, weld::Widget* pDiffReviewParent) = 0;

    virtual void dispatchSelected(CellAction eAction,
                                  const rtl::OUString& rUserPrompt) = 0;
};

std::unique_ptr<CellRangePopover> CreateCalcCellRangePopover(weld::Widget* pParent);

// Anchor popover at rRect for rCellRangeA1 (e.g. Sheet1.B2:D5).
void ShowCellRangePopover(ScTabViewShell& rShell, const tools::Rectangle& rRect,
                          const rtl::OUString& rCellRangeA1);

// Called when the weld popover is dismissed (ESC / focus loss).
void NotifyCellRangePopoverDismissed();

// Close the active popover without clearing the singleton (popover close handler
// still calls NotifyCellRangePopoverDismissed).
void DismissCellRangePopover();

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */