/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "CalcCellRangePopover.hxx"
#include "CellRangePopover.hxx"
#include "InlineActionRequest.hxx"
#include "InlineActionProviderDispatch.hxx"

#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Popover.hxx>
#include <vcl/weld/weld.hxx>

namespace sc::inline_actions {

CalcCellRangePopover::CalcCellRangePopover(weld::Widget* pParent)
    : m_pAnchorParent(pParent)
    , m_pDiffReviewParent(pParent)
    , m_pDocShell(nullptr)
    , m_xBuilder(Application::CreateBuilder(
          pParent, u"modules/scalc/ui/cell-range-popover.ui"_ustr))
    , m_xPopover(m_xBuilder->weld_popover(u"CellRangePopover"_ustr))
    , m_xExplainData(m_xBuilder->weld_button(u"btn_explain_data"_ustr))
    , m_xSuggestChart(m_xBuilder->weld_button(u"btn_suggest_chart"_ustr))
    , m_xGenerateFormula(m_xBuilder->weld_button(u"btn_generate_formula"_ustr))
    , m_xFormatClean(m_xBuilder->weld_button(u"btn_format_clean"_ustr))
    , m_xFormatChange(m_xBuilder->weld_button(u"btn_format_change"_ustr))
{
    m_xExplainData->connect_clicked(LINK(this, CalcCellRangePopover, OnActionClicked));
    m_xSuggestChart->connect_clicked(LINK(this, CalcCellRangePopover, OnActionClicked));
    m_xGenerateFormula->connect_clicked(LINK(this, CalcCellRangePopover, OnActionClicked));
    m_xFormatClean->connect_clicked(LINK(this, CalcCellRangePopover, OnActionClicked));
    m_xFormatChange->connect_clicked(LINK(this, CalcCellRangePopover, OnActionClicked));
    m_xPopover->connect_closed(LINK(this, CalcCellRangePopover, OnPopoverClosed));
}

void CalcCellRangePopover::open(const tools::Rectangle& rRect,
                                const rtl::OUString& rCellRange)
{
    m_sCellRange = rCellRange;
    m_aAnchorRect = rRect;
    m_xPopover->popup_at_rect(m_pAnchorParent, m_aAnchorRect, weld::Placement::End);
    m_bOpen = true;
}

void CalcCellRangePopover::close()
{
    if (m_bOpen)
        m_xPopover->popdown();
}

void CalcCellRangePopover::setDispatchContext(SfxObjectShell* pDocShell,
                                              weld::Widget* pDiffReviewParent)
{
    m_pDocShell = pDocShell;
    m_pDiffReviewParent = pDiffReviewParent ? pDiffReviewParent : m_pAnchorParent;
}

void CalcCellRangePopover::dispatchSelected(CellAction eAction,
                                            const rtl::OUString& rUserPrompt)
{
    (void)rUserPrompt;
    OUString aSheet;
    OUString aRange;
    if (!splitCalcCellRangeA1(m_sCellRange, aSheet, aRange))
    {
        SAL_INFO("sc.inline_actions",
                 "dispatchSelected: cannot split cell_range=" << m_sCellRange);
        return;
    }
    const OUString aJson
        = buildCalcCellRequest(toToken(eAction), aSheet, aRange, u"offline"_ustr);
    SAL_INFO("sc.inline_actions", "inline_action_request=" << aJson);
    dispatchCalcInlineAction(aJson, eAction, m_pDocShell, m_pDiffReviewParent);
}

void CalcCellRangePopover::pickAction(CellAction eAction)
{
    dispatchSelected(eAction, OUString());
    DismissCellRangePopover();
}

IMPL_LINK(CalcCellRangePopover, OnActionClicked, weld::Button&, rButton, void)
{
    if (&rButton == m_xExplainData.get())
        pickAction(CellAction::ExplainData);
    else if (&rButton == m_xSuggestChart.get())
        pickAction(CellAction::SuggestChart);
    else if (&rButton == m_xGenerateFormula.get())
        pickAction(CellAction::GenerateFormula);
    else if (&rButton == m_xFormatClean.get())
        pickAction(CellAction::FormatClean);
    else if (&rButton == m_xFormatChange.get())
        pickAction(CellAction::FormatChange);
}

IMPL_LINK_NOARG(CalcCellRangePopover, OnPopoverClosed, weld::Popover&, void)
{
    m_bOpen = false;
    NotifyCellRangePopoverDismissed();
}

std::unique_ptr<CellRangePopover> CreateCalcCellRangePopover(weld::Widget* pParent)
{
    return std::make_unique<CalcCellRangePopover>(pParent);
}

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
