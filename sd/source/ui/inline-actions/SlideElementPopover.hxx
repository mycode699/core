/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "SlideElementActions.hxx"
#include <memory>
#include <rtl/ustring.hxx>
#include <sal/types.h>
#include <tools/gen.hxx>

class SfxObjectShell;

namespace sd
{
class DrawViewShell;
}

namespace weld
{
class Widget;
}

// W4 Impress inline-action popover lifecycle. Day-1 wires weld::Popover;
// dispatchSelected() is signal-only until W1 provider + W4-D diff land.

namespace sd::inline_actions {

class SlideElementPopover
{
public:
    virtual ~SlideElementPopover() = default;

    // Anchor the popover at rRect in screen coordinates for the slide
    // element identified by rElementId (e.g. "slide-3/shape-7").
    virtual void open(const tools::Rectangle& rRect, sal_Int32 nSlideIndex,
                      const rtl::OUString& rElementId) = 0;
    virtual void close() = 0;

    virtual void setDispatchContext(SfxObjectShell* pDocShell, weld::Widget* pDiffReviewParent) = 0;

    virtual void dispatchSelected(SlideElementAction eAction,
                                  const rtl::OUString& rUserPrompt) = 0;
};

std::unique_ptr<SlideElementPopover> CreateImpressSlideElementPopover(weld::Widget* pParent);

// Day-1 stub: anchor popover at rRect for rElementId. Selection
// integration lands in a follow-up; callers/tests may invoke directly.
void ShowSlideElementPopover(DrawViewShell& rShell, const tools::Rectangle& rRect,
                             sal_Int32 nSlideIndex, const rtl::OUString& rElementId);

// Called when the weld popover is dismissed (ESC / focus loss).
void NotifySlideElementPopoverDismissed();

// Close the active popover without clearing the singleton (popover close handler
// still calls NotifySlideElementPopoverDismissed).
void DismissSlideElementPopover();

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */