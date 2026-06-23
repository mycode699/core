/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <swdllapi.h>
#include <rtl/ustring.hxx>

// W4 Day-0 skeleton: ParagraphAction enum locked by
// docs/product/v2/w4-select-to-act-spec.md L249-L258.
//
// Lowercase ASCII kebab-case tokens. Must stay in lockstep with the
// `rewrite|expand|shorten|translate-en|explain|custom` capability
// strings in apply-plan-runtime.schema.json (+ format-clean which is
// local-only). Any new action requires simultaneous edits in the W4
// spec table, this enum, and the schema enum.

namespace sw::inline_actions {

enum class ParagraphAction
{
    // goes through Diff
    Rewrite,        // rewrite
    Expand,         // expand
    Shorten,        // shorten
    TranslateEn,    // translate-en
    FormatClean,    // format-clean (local, no provider call)
    // popup-only (no ApplyPlan)
    Explain,        // explain
    // free-form, routes to user_prompt
    Custom,         // custom
};

// Free-function signatures only in Day-0 — implementations land in Day-1
// together with popover GUI bring-up. Keeping these declared in the
// header lets Day-1 patches be pure additions without touching the
// enum stability cppunit.
SW_DLLPUBLIC rtl::OUString toToken(ParagraphAction);
SW_DLLPUBLIC ParagraphAction fromToken(const rtl::OUString& rToken);

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
