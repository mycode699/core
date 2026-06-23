/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "CellRangePopover.hxx"
#include "CalcCellRangePopover.hxx"

#include <o3tl/test_info.hxx>
#include <sal/log.hxx>
#include <tabvwsh.hxx>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>
#include <vcl/weld/weldutils.hxx>

namespace sc::inline_actions {

namespace {

std::unique_ptr<CellRangePopover> g_pActivePopover;

bool lcl_isInteractivePopoverAllowed()
{
    return !Application::IsHeadlessModeEnabled() && !o3tl::IsRunningUnitTest()
           && !o3tl::IsRunningUITest();
}

} // namespace

void ShowCellRangePopover(ScTabViewShell& rShell, const tools::Rectangle& rRect,
                          const rtl::OUString& rCellRangeA1)
{
    if (!lcl_isInteractivePopoverAllowed())
    {
        SAL_INFO("sc.inline_actions", "ShowCellRangePopover: non-interactive runtime");
        return;
    }

    vcl::Window* pWin = rShell.GetWindow();
    if (!pWin)
    {
        SAL_INFO("sc.inline_actions", "ShowCellRangePopover: no view window");
        return;
    }

    std::unique_ptr<CellRangePopover> pPreviousPopover = std::move(g_pActivePopover);
    if (pPreviousPopover)
        pPreviousPopover->close();

    tools::Rectangle aRect = rRect;
    weld::Window* pParent = weld::GetPopupParent(*pWin, aRect);
    g_pActivePopover = CreateCalcCellRangePopover(pParent);
    g_pActivePopover->setDispatchContext(rShell.GetViewData().GetDocShell(), pParent);
    g_pActivePopover->open(aRect, rCellRangeA1);

    SAL_INFO("sc.inline_actions",
             "ShowCellRangePopover opened cell_range=" << rCellRangeA1);
}

void NotifyCellRangePopoverDismissed() { g_pActivePopover.reset(); }

void DismissCellRangePopover()
{
    std::unique_ptr<CellRangePopover> pActivePopover = std::move(g_pActivePopover);
    if (!lcl_isInteractivePopoverAllowed())
        return;
    if (pActivePopover)
        pActivePopover->close();
}

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
