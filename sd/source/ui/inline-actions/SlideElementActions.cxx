/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SlideElementActions.hxx"

namespace sd::inline_actions {

rtl::OUString toToken(SlideElementAction eAction)
{
    switch (eAction)
    {
        case SlideElementAction::RewriteText:   return u"rewrite-text"_ustr;
        case SlideElementAction::AdjustColor:   return u"adjust-color"_ustr;
        case SlideElementAction::Relayout:      return u"relayout"_ustr;
        case SlideElementAction::TranslateText: return u"translate-text"_ustr;
    }
    return rtl::OUString();
}

SlideElementAction fromToken(const rtl::OUString& rToken)
{
    if (rToken == u"rewrite-text")   return SlideElementAction::RewriteText;
    if (rToken == u"adjust-color")   return SlideElementAction::AdjustColor;
    if (rToken == u"relayout")       return SlideElementAction::Relayout;
    if (rToken == u"translate-text") return SlideElementAction::TranslateText;
    // Unknown token falls back to RewriteText so the request still goes
    // through Diff Review rather than mutating the slide silently.
    return SlideElementAction::RewriteText;
}

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
