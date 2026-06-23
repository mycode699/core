/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <scdllapi.h>
#include <rtl/ustring.hxx>

// W4 Day-0 skeleton: CellAction enum locked by
// docs/product/v2/w4-select-to-act-spec.md L259-L267.
//
// Lowercase ASCII kebab-case tokens. Must stay in lockstep with the
// `explain-data|suggest-chart|generate-formula|format-change`
// capability strings in apply-plan-runtime.schema.json (+ format-clean
// is shared with W4-A Writer; same literal). Any new action requires
// simultaneous edits in the W4 spec table, this enum, and the schema
// enum.

namespace sc::inline_actions {

enum class CellAction
{
    // popup-only (no ApplyPlan)
    ExplainData,        // explain-data
    SuggestChart,       // suggest-chart
    // goes through Diff
    GenerateFormula,    // generate-formula
    FormatClean,        // format-clean (local, no provider call; shared token with W4-A)
    FormatChange,       // format-change
};

// Free-function signatures only in Day-0 — implementations land in Day-1
// together with popover GUI bring-up. Keeping these declared in the
// header lets Day-1 patches be pure additions without touching the
// enum stability cppunit.
SC_DLLPUBLIC rtl::OUString toToken(CellAction);
SC_DLLPUBLIC CellAction fromToken(const rtl::OUString& rToken);

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
