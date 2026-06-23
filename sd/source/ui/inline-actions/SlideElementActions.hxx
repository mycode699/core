/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sddllapi.h>
#include <rtl/ustring.hxx>

// W4 Day-0 skeleton: SlideElementAction enum locked by
// docs/product/v2/w4-select-to-act-spec.md L269-L276.
//
// Lowercase ASCII kebab-case tokens. Must stay in lockstep with the
// `rewrite-text|adjust-color|relayout|translate-text` capability
// strings in apply-plan-runtime.schema.json. Any new action requires
// simultaneous edits in the W4 spec table, this enum, and the schema
// enum.

namespace sd::inline_actions {

enum class SlideElementAction
{
    // all four go through Diff
    RewriteText,        // rewrite-text
    AdjustColor,        // adjust-color
    Relayout,           // relayout
    TranslateText,      // translate-text
};

// Free-function signatures only in Day-0 — implementations land in Day-1
// together with popover GUI bring-up. Keeping these declared in the
// header lets Day-1 patches be pure additions without touching the
// enum stability cppunit.
SD_DLLPUBLIC rtl::OUString toToken(SlideElementAction);
SD_DLLPUBLIC SlideElementAction fromToken(const rtl::OUString& rToken);

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
