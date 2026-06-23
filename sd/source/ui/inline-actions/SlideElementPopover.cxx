/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SlideElementPopover.hxx"
#include "ImpressSlideElementPopover.hxx"

#include <DrawViewShell.hxx>
#include <DrawDocShell.hxx>
#include <o3tl/test_info.hxx>
#include <sfx2/objsh.hxx>
#include <sal/log.hxx>
#include <Window.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld/weldutils.hxx>

namespace sd::inline_actions {

namespace {

std::unique_ptr<SlideElementPopover> g_pActivePopover;

bool lcl_isInteractivePopoverAllowed()
{
    return !Application::IsHeadlessModeEnabled() && !o3tl::IsRunningUnitTest()
           && !o3tl::IsRunningUITest();
}

} // namespace

void ShowSlideElementPopover(DrawViewShell& rShell, const tools::Rectangle& rRect,
                             sal_Int32 nSlideIndex, const rtl::OUString& rElementId)
{
    if (!lcl_isInteractivePopoverAllowed())
    {
        SAL_INFO("sd.inline_actions", "ShowSlideElementPopover: non-interactive runtime");
        return;
    }

    ::sd::Window* pWin = rShell.GetActiveWindow();
    if (!pWin)
    {
        SAL_INFO("sd.inline_actions", "ShowSlideElementPopover: no view window");
        return;
    }

    std::unique_ptr<SlideElementPopover> pPreviousPopover = std::move(g_pActivePopover);
    if (pPreviousPopover)
        pPreviousPopover->close();

    tools::Rectangle aRect = rRect;
    weld::Window* pParent = weld::GetPopupParent(*pWin, aRect);
    g_pActivePopover = CreateImpressSlideElementPopover(pParent);
    g_pActivePopover->setDispatchContext(static_cast<SfxObjectShell*>(rShell.GetDocSh()),
                                        pParent);
    g_pActivePopover->open(aRect, nSlideIndex, rElementId);

    SAL_INFO("sd.inline_actions", "ShowSlideElementPopover opened slide_index=" << nSlideIndex
                                << " element_id=" << rElementId);
}

void NotifySlideElementPopoverDismissed() { g_pActivePopover.reset(); }

void DismissSlideElementPopover()
{
    std::unique_ptr<SlideElementPopover> pActivePopover = std::move(g_pActivePopover);
    if (!lcl_isInteractivePopoverAllowed())
        return;
    if (pActivePopover)
        pActivePopover->close();
}

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
