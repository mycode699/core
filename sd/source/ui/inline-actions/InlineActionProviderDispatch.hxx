/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "SlideElementActions.hxx"
#include <rtl/ustring.hxx>
#include <sddllapi.h>

class SfxObjectShell;

namespace weld
{
class Widget;
}

namespace sd::inline_actions {

// W4 Day-3: SlideElementAction token → W1 offline ProviderRequest.capability.
//
// | SlideElementAction token | W1 capability | Provider call | DiffReview |
// |--------------------------|---------------|---------------|------------|
// | rewrite-text             | rewrite       | yes           | yes        |
// | adjust-color             | format-fix    | yes           | yes        |
// | relayout                 | format-fix    | yes           | yes        |
// | translate-text           | rewrite       | yes           | yes        |

SD_DLLPUBLIC OUString offlineCapabilityForSlideElementAction(SlideElementAction eAction);

/// UNO Provider::call + SAL_INFO; opens DiffReview when actionRoutesToDiff().
SD_DLLPUBLIC void dispatchImpressInlineAction(const OUString& rJsonRequest,
                                              SlideElementAction eAction,
                                              SfxObjectShell* pDocShell,
                                              weld::Widget* pDiffReviewParent);

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */