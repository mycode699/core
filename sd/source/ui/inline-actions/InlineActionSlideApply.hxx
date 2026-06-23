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

namespace sd
{
class DrawViewShell;
}

namespace sd::inline_actions {

// W4-C Day-5: which SlideElementAction values may write provider content into the slide.
SD_DLLPUBLIC bool shouldApplyProviderContentToMarkedTextShape(SlideElementAction eAction);

// Writes provider content to the single selected SdrTextObj (outline text via SetText).
// RewriteText and TranslateText only; other actions return false (DiffReview preview only).
SD_DLLPUBLIC bool tryApplyProviderContentToMarkedTextShape(DrawViewShell& rShell,
                                                           const OUString& rContent,
                                                           SlideElementAction eAction);

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */