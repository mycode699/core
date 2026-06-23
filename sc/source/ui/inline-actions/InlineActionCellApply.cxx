/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "InlineActionCellApply.hxx"

#include <docfunc.hxx>
#include <docsh.hxx>
#include <formula/grammar.hxx>
#include <formulacell.hxx>
#include <markdata.hxx>
#include <optional>
#include <sal/log.hxx>
#include <tabvwsh.hxx>
#include <viewdata.hxx>

namespace sc::inline_actions {
namespace {

bool lcl_hasMarkedCellRange(const ScTabViewShell& rShell)
{
    const ScViewData& rViewData = rShell.GetViewData();
    if (!rViewData.IsActive())
        return false;
    if (rShell.IsRefInputMode())
        return false;
    if (rShell.GetCurObjectSelectionType() != ObjectSelectionType::OST_Cell)
        return false;

    const ScMarkData& rMark = rViewData.GetMarkData();
    if (rMark.GetMarkingFlag())
        return false;
    if (!rMark.IsMarked() && !rMark.IsMultiMarked())
        return false;
    return true;
}

std::optional<ScAddress> lcl_markedRangeTopLeft(const ScTabViewShell& rShell)
{
    const ScMarkData& rMark = rShell.GetViewData().GetMarkData();
    return rMark.GetArea().aStart;
}

} // namespace

bool shouldApplyProviderContentToMarkedCell(CellAction eAction)
{
    return eAction == CellAction::GenerateFormula;
}

bool tryApplyProviderContentToMarkedCell(ScTabViewShell& rShell, const OUString& rContent,
                                         CellAction eAction)
{
    if (!shouldApplyProviderContentToMarkedCell(eAction))
        return false;

    if (rContent.isEmpty())
    {
        SAL_INFO("sc.inline_actions",
                 "tryApplyProviderContentToMarkedCell: empty provider content");
        return false;
    }

    if (!lcl_hasMarkedCellRange(rShell))
    {
        SAL_INFO("sc.inline_actions",
                 "tryApplyProviderContentToMarkedCell: no eligible marked cell range");
        return false;
    }

    const std::optional<ScAddress> oPos = lcl_markedRangeTopLeft(rShell);
    if (!oPos)
        return false;

    ScDocShell* pDocShell = rShell.GetViewData().GetDocShell();
    if (!pDocShell)
        return false;

    ScDocFunc& rDocFunc = pDocShell->GetDocFunc();
    ScDocument& rDoc = pDocShell->GetDocument();
    const formula::FormulaGrammar::Grammar eGrammar = rDoc.GetGrammar();

    const bool bFormula = rContent.startsWith("=");
    const bool bOk
        = bFormula
              ? rDocFunc.SetFormulaCell(
                    *oPos, new ScFormulaCell(rDoc, *oPos, rContent, eGrammar), true)
              : rDocFunc.SetStringCell(*oPos, rContent, true);

    SAL_INFO("sc.inline_actions",
             "tryApplyProviderContentToMarkedCell action=" << toToken(eAction)
             << " applied=" << bOk << " formula=" << bFormula);
    return bOk;
}

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */