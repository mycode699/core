/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "CellActions.hxx"

namespace sc::inline_actions {

rtl::OUString toToken(CellAction eAction)
{
    switch (eAction)
    {
        case CellAction::ExplainData:     return u"explain-data"_ustr;
        case CellAction::SuggestChart:    return u"suggest-chart"_ustr;
        case CellAction::GenerateFormula: return u"generate-formula"_ustr;
        case CellAction::FormatClean:     return u"format-clean"_ustr;
        case CellAction::FormatChange:    return u"format-change"_ustr;
    }
    return rtl::OUString();
}

CellAction fromToken(const rtl::OUString& rToken)
{
    if (rToken == u"explain-data")     return CellAction::ExplainData;
    if (rToken == u"suggest-chart")    return CellAction::SuggestChart;
    if (rToken == u"generate-formula") return CellAction::GenerateFormula;
    if (rToken == u"format-clean")     return CellAction::FormatClean;
    if (rToken == u"format-change")    return CellAction::FormatChange;
    // Unknown token falls back to ExplainData (popup-only, no ApplyPlan)
    // so an invalid input cannot silently route through Diff Review.
    return CellAction::ExplainData;
}

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
