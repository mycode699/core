/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SelectToActPopover.hxx"
#include "WriterSelectToActPopover.hxx"

#include <doc.hxx>
#include <docsh.hxx>
#include <o3tl/test_info.hxx>
#include <sal/log.hxx>
#include <viewsh.hxx>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>
#include <vcl/weld/weldutils.hxx>

namespace sw::inline_actions {

namespace {

std::unique_ptr<SelectToActPopover> g_pActivePopover;
OUString g_sActiveParagraphId;

bool lcl_isInteractivePopoverAllowed()
{
    return !Application::IsHeadlessModeEnabled() && !o3tl::IsRunningUnitTest()
           && !o3tl::IsRunningUITest();
}

} // namespace

void ShowSelectToActPopover(SwViewShell& rShell, const tools::Rectangle& rRect,
                            const rtl::OUString& rParagraphId)
{
    if (!lcl_isInteractivePopoverAllowed())
    {
        SAL_INFO("sw.inline_actions", "ShowSelectToActPopover: non-interactive runtime");
        return;
    }

    vcl::Window* pWin = rShell.GetWin();
    if (!pWin)
    {
        SAL_INFO("sw.inline_actions", "ShowSelectToActPopover: no view window");
        return;
    }

    if (g_pActivePopover && g_sActiveParagraphId == rParagraphId)
    {
        g_pActivePopover->open(rRect, rParagraphId);
        return;
    }

    std::unique_ptr<SelectToActPopover> pPreviousPopover = std::move(g_pActivePopover);
    g_sActiveParagraphId.clear();
    if (pPreviousPopover)
        pPreviousPopover->close();

    tools::Rectangle aRect = rRect;
    weld::Window* pParent = weld::GetPopupParent(*pWin, aRect);
    SfxObjectShell* pDocShell = rShell.GetDoc() ? rShell.GetDoc()->GetDocShell() : nullptr;
    g_pActivePopover = CreateWriterSelectToActPopover(pParent, pDocShell);
    g_sActiveParagraphId = rParagraphId;
    g_pActivePopover->open(aRect, rParagraphId);

    SAL_INFO("sw.inline_actions",
             "ShowSelectToActPopover opened paragraph_id=" << rParagraphId);
}

void NotifySelectToActPopoverDismissed()
{
    g_pActivePopover.reset();
    g_sActiveParagraphId.clear();
}

void DismissSelectToActPopover()
{
    std::unique_ptr<SelectToActPopover> pActivePopover = std::move(g_pActivePopover);
    g_sActiveParagraphId.clear();
    if (!lcl_isInteractivePopoverAllowed())
        return;
    if (pActivePopover)
        pActivePopover->close();
}

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
