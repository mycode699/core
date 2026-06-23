/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "ParagraphActions.hxx"
#include <rtl/ustring.hxx>
#include <swdllapi.h>

class SfxObjectShell;

namespace weld
{
class Widget;
}

// W4 Day-2: inline-action-request envelope (docs/schemas/inline-action-request.schema.json).

namespace sw::inline_actions {

/// Builds a one-line JSON envelope for writer-paragraph dispatch.
SW_DLLPUBLIC OUString buildWriterParagraphRequest(const OUString& rActionToken,
                                                  const OUString& rParagraphId,
                                                  const OUString& rServiceMode
                                                      = u"offline"_ustr,
                                                  const OUString& rUserPrompt = OUString());

/// True for actions that route through Diff per W4 spec (not popup-only explain).
SW_DLLPUBLIC bool actionRoutesToDiff(ParagraphAction eAction);

/// Provider dispatch + DiffReviewPanel (W4 Day-3).
void OpenDiffReviewForInlineAction(const OUString& rJsonRequest, weld::Widget* pParent,
                                   SfxObjectShell* pDocShell);

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */