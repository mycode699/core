/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ParagraphActions.hxx"

namespace sw::inline_actions {

rtl::OUString toToken(ParagraphAction eAction)
{
    switch (eAction)
    {
        case ParagraphAction::Rewrite:     return u"rewrite"_ustr;
        case ParagraphAction::Expand:      return u"expand"_ustr;
        case ParagraphAction::Shorten:     return u"shorten"_ustr;
        case ParagraphAction::TranslateEn: return u"translate-en"_ustr;
        case ParagraphAction::FormatClean: return u"format-clean"_ustr;
        case ParagraphAction::Explain:     return u"explain"_ustr;
        case ParagraphAction::Custom:      return u"custom"_ustr;
    }
    return rtl::OUString();
}

ParagraphAction fromToken(const rtl::OUString& rToken)
{
    if (rToken == u"rewrite")      return ParagraphAction::Rewrite;
    if (rToken == u"expand")       return ParagraphAction::Expand;
    if (rToken == u"shorten")      return ParagraphAction::Shorten;
    if (rToken == u"translate-en") return ParagraphAction::TranslateEn;
    if (rToken == u"format-clean") return ParagraphAction::FormatClean;
    if (rToken == u"explain")      return ParagraphAction::Explain;
    if (rToken == u"custom")       return ParagraphAction::Custom;
    // Unknown token falls back to Custom so an invalid input surfaces
    // at the user_prompt layer rather than silently enabling a Diff.
    return ParagraphAction::Custom;
}

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
