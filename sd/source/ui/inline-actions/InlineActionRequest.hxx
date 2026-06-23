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
#include <sal/types.h>

// W4 Day-2: inline-action-request envelope (docs/schemas/inline-action-request.schema.json).

namespace sd::inline_actions {

/// Builds a one-line JSON envelope for impress-slide-element dispatch.
SD_DLLPUBLIC OUString buildImpressSlideElementRequest(const OUString& rActionToken,
                                                      sal_Int32 nSlideIndex,
                                                      const OUString& rElementId,
                                                      const OUString& rServiceMode
                                                          = u"offline"_ustr);

/// All four SlideElementAction tokens route through Diff.
SD_DLLPUBLIC bool actionRoutesToDiff(SlideElementAction eAction);

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */