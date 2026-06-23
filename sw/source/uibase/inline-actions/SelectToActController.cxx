/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SelectToActController.hxx"
#include "SelectToActPopover.hxx"
#include "WriterBodyTextParagraphId.hxx"

#include <crsrsh.hxx>
#include <ndtxt.hxx>
#include <node.hxx>
#include <o3tl/test_info.hxx>
#include <pam.hxx>
#include <sal/log.hxx>
#include <tools/gen.hxx>
#include <vcl/svapp.hxx>
#include <wrtsh.hxx>

namespace sw::inline_actions {
namespace {

bool lcl_isEligibleTextSelection(const SwWrtShell& rSh)
{
    if (rSh.ActionPend())
        return false;
    if (!rSh.HasSelection())
        return false;
    if (rSh.IsTableMode() || rSh.IsSelFrameMode() || rSh.GetSelectedObjCount())
        return false;
    if (!rSh.IsSelOnePara())
        return false;

    const SelectionType eSel = rSh.GetSelectionType();
    if (!(SelectionType::Text & eSel))
        return false;
    if (SelectionType::TableCell & eSel)
        return false;
    if (rSh.GetSelText().isEmpty())
        return false;
    return true;
}

std::optional<OUString> lcl_paragraphIdForSelection(const SwWrtShell& rSh)
{
    const SwPaM* pPam = rSh.GetCursor();
    if (!pPam || !pPam->HasMark())
        return std::nullopt;

    const SwTextNode* pTextNode = pPam->Start()->GetNode().GetTextNode();
    if (!pTextNode)
        return std::nullopt;

    const std::optional<sal_uInt32> oIndex
        = getBodyTextParagraphIndex(*rSh.GetDoc(), *pTextNode);
    if (!oIndex || *oIndex < 1)
        return std::nullopt;

    return formatBodyTextParagraphId(*oIndex);
}

tools::Rectangle lcl_selectionAnchorRect(SwWrtShell& rSh)
{
    const SwPaM* pPam = rSh.GetCursor();
    if (!pPam)
        return rSh.GetCharRect().SVRect();

    SwRect aCharRect;
    rSh.GetCharRectAt(aCharRect, pPam->End());
    return aCharRect.SVRect();
}

void lcl_dismissPopover() { DismissSelectToActPopover(); }

bool lcl_isInteractivePopoverAllowed()
{
    return !Application::IsHeadlessModeEnabled() && !o3tl::IsRunningUnitTest()
           && !o3tl::IsRunningUITest();
}

} // namespace

void OnWriterSelectionChanged(SwWrtShell& rShell)
{
    // Popovers are interactive UI. Headless/unit/ui-test runs still exercise the
    // selection/request logic without entering VCL popup mode.
    if (!lcl_isInteractivePopoverAllowed())
        return;

    if (!lcl_isEligibleTextSelection(rShell))
    {
        lcl_dismissPopover();
        return;
    }

    const std::optional<OUString> oParagraphId = lcl_paragraphIdForSelection(rShell);
    if (!oParagraphId)
    {
        SAL_INFO("sw.inline_actions",
                 "OnWriterSelectionChanged: no swpara id for selection");
        lcl_dismissPopover();
        return;
    }

    const tools::Rectangle aAnchor = lcl_selectionAnchorRect(rShell);
    ShowSelectToActPopover(rShell, aAnchor, *oParagraphId);
}

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
