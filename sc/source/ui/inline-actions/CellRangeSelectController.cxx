/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "CellRangeSelectController.hxx"
#include "CellRangePopover.hxx"

#include <optional>

#include <formula/grammar.hxx>
#include <markdata.hxx>
#include <o3tl/test_info.hxx>
#include <rangeutl.hxx>
#include <sal/log.hxx>
#include <tabview.hxx>
#include <tabvwsh.hxx>
#include <tools/gen.hxx>
#include <vcl/svapp.hxx>
#include <viewdata.hxx>

namespace sc::inline_actions {
namespace {

bool lcl_isEligibleCellRangeSelection(const ScTabViewShell& rShell)
{
    if (!rShell.GetViewData().IsActive())
        return false;
    if (rShell.IsRefInputMode())
        return false;
    if (rShell.GetCurObjectSelectionType() != ObjectSelectionType::OST_Cell)
        return false;

    const ScMarkData& rMark = rShell.GetViewData().GetMarkData();
    if (rMark.GetMarkingFlag())
        return false;
    if (!rMark.IsMarked() && !rMark.IsMultiMarked())
        return false;
    return true;
}

std::optional<OUString> lcl_cellRangeA1String(const ScTabViewShell& rShell)
{
    const ScMarkData& rMark = rShell.GetViewData().GetMarkData();
    const ScRangeList aRanges = rMark.GetMarkedRanges();
    if (aRanges.empty())
        return std::nullopt;

    OUString aCellRange;
    const ScDocument& rDoc = rShell.GetViewData().GetDocument();
    ScRangeStringConverter::GetStringFromRangeList(
        aCellRange, &aRanges, &rDoc, formula::FormulaGrammar::CONV_OOO);
    if (aCellRange.isEmpty())
        return std::nullopt;
    return aCellRange;
}

tools::Rectangle lcl_selectionAnchorRect(ScTabViewShell& rShell)
{
    ScViewData& rViewData = rShell.GetViewData();
    const ScMarkData& rMark = rViewData.GetMarkData();
    const ScRange& rArea = rMark.GetArea();

    SCCOL nStartX = rArea.aStart.Col();
    SCROW nStartY = rArea.aStart.Row();
    SCCOL nEndX = rArea.aEnd.Col();
    SCROW nEndY = rArea.aEnd.Row();

    const ScSplitPos eWhich = rViewData.GetActivePart();
    vcl::Window* pWin = rShell.GetWindowByPos(eWhich);
    if (!pWin)
        return tools::Rectangle();

    const Point aStart = rViewData.GetScrPos(nStartX, nStartY, eWhich);
    const Point aEnd = rViewData.GetScrPos(nEndX + 1, nEndY + 1, eWhich);
    const bool bLeft = nEndX < nStartX;
    const bool bTop = nEndY < nStartY;
    Point aPos(bLeft ? aStart.X() : (aEnd.X() + 3), bTop ? aStart.Y() : (aEnd.Y() + 3));
    return tools::Rectangle(pWin->OutputToScreenPixel(aPos), Size(1, 1));
}

void lcl_dismissPopover() { DismissCellRangePopover(); }

bool lcl_isInteractivePopoverAllowed()
{
    return !Application::IsHeadlessModeEnabled() && !o3tl::IsRunningUnitTest()
           && !o3tl::IsRunningUITest();
}

} // namespace

void OnCalcCellRangeSelectionChanged(ScTabViewShell& rShell)
{
    if (!lcl_isInteractivePopoverAllowed())
        return;

    if (!lcl_isEligibleCellRangeSelection(rShell))
    {
        lcl_dismissPopover();
        return;
    }

    const std::optional<OUString> oCellRange = lcl_cellRangeA1String(rShell);
    if (!oCellRange)
    {
        SAL_INFO("sc.inline_actions",
                 "OnCalcCellRangeSelectionChanged: no A1 range for selection");
        lcl_dismissPopover();
        return;
    }

    const tools::Rectangle aAnchor = lcl_selectionAnchorRect(rShell);
    if (aAnchor.IsEmpty())
    {
        SAL_INFO("sc.inline_actions",
                 "OnCalcCellRangeSelectionChanged: no anchor rect for selection");
        lcl_dismissPopover();
        return;
    }

    ShowCellRangePopover(rShell, aAnchor, *oCellRange);
}

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
