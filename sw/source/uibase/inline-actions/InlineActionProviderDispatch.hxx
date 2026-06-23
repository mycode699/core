/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4 Day-3: Writer popover → Provider).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <optional>
#include <rtl/ustring.hxx>
#include <swdllapi.h>

class SfxObjectShell;

namespace weld
{
class Widget;
}

namespace sw::inline_actions {

/// Maps a W4 Writer action token to an offline ServiceModePolicy capability.
/// Returns nullopt when the action has no provider route (e.g. explain) or is unknown.
/// See InlineActionProviderDispatch.cxx for the W4→W1 table (do not widen the allowlist).
SW_DLLPUBLIC std::optional<OUString> mapWriterActionToProviderCapability(
    const OUString& rActionToken);

/// Builds the constrained W3 runtime JSON instruction sent to the Provider.
/// The model may still fail, but Writer must ask for apply-plan-runtime JSON,
/// not free-form prose, so the response can flow through TryParseApplyPlanRuntimeJson().
SW_DLLPUBLIC OUString buildWriterRuntimeJsonPromptForProvider(
    const OUString& rActionToken, const OUString& rUserPrompt, const OUString& rParagraphId,
    const OUString& rRequestId);

/// Parses inline-action-request JSON, calls com.sun.star.ai.Provider, opens DiffReview.
SW_DLLPUBLIC void dispatchWriterInlineAction(const OUString& rJson, weld::Widget* pParent,
                                             SfxObjectShell* pDocShell);

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
