/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "ParagraphActions.hxx"
#include <memory>
#include <rtl/ustring.hxx>
#include <tools/gen.hxx>

class SfxObjectShell;
class SwViewShell;

namespace weld
{
class Widget;
}

// W4 Writer inline-action popover lifecycle. Day-1 wires weld::Popover;
// dispatchSelected() is signal-only until W1 provider + W4-D diff land.

namespace sw::inline_actions {

class SelectToActPopover
{
public:
    virtual ~SelectToActPopover() = default;

    // Anchor the popover at rRect in document coordinates for the
    // paragraph identified by rParagraphId (W1 paragraph_id shape).
    virtual void open(const tools::Rectangle& rRect,
                      const rtl::OUString& rParagraphId) = 0;
    virtual void close() = 0;

    // Emitted when the user picks an action. Day-0: signal-only, no
    // provider round-trip.
    virtual void dispatchSelected(ParagraphAction eAction,
                                  const rtl::OUString& rUserPrompt) = 0;
};

std::unique_ptr<SelectToActPopover> CreateWriterSelectToActPopover(weld::Widget* pParent,
                                                                   SfxObjectShell* pDocShell);

// Anchor popover at rRect for rParagraphId. Production path:
// SelectToActController::OnWriterSelectionChanged via DrawSelChanged.
void ShowSelectToActPopover(SwViewShell& rShell, const tools::Rectangle& rRect,
                            const rtl::OUString& rParagraphId);

// Called when the weld popover is dismissed (ESC / focus loss).
void NotifySelectToActPopoverDismissed();

// Close an open popover without opening a new one (selection cleared).
void DismissSelectToActPopover();

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
