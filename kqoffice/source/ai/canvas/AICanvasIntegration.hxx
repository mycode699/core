/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AICanvasIntegration — bridge between canvas mode and AIChatPanel.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CANVAS_AICANVASINTEGRATION_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CANVAS_AICANVASINTEGRATION_HXX

#include "AICanvasMode.hxx"

#include <rtl/ustring.hxx>

namespace kqoffice::ai::canvas
{

/// Bridge connecting canvas mode to the existing AIChatPanel sidebar.
/// When canvas mode is active, the AIChatPanel switches to canvas workflow
/// mode where each chat message is a step requirement, confirmation, or
/// revision feedback.
class SAL_DLLPUBLIC_EXPORT AICanvasIntegration
{
public:
    /// Check if canvas mode is currently active.
    static bool isCanvasActive();

    /// Start canvas mode via the chat panel.
    /// The chat panel becomes the canvas workflow interface.
    /// @return true if canvas session started successfully
    static bool startCanvasViaChat(CanvasDocType docType,
                                   const OUString& goal);

    /// Process a chat message as a canvas step requirement.
    /// Returns the LLM response formatted for display.
    static OUString processCanvasMessage(const OUString& message);

    /// Process a confirmation command ("确认", "yes", "next").
    /// Returns the next step prompt or completion message.
    static OUString processConfirm();

    /// Process a revision command ("修改: ...", "change: ...").
    /// Returns the revised content for re-review.
    static OUString processRevise(const OUString& feedback);

    /// Get the current canvas session for UI display.
    static const CanvasSession& getCurrentSession();

    /// End the canvas session and apply all steps.
    static void endCanvasSession();

private:
    static AICanvasMode s_canvasMode;
    static bool s_isActive;
};

} // namespace kqoffice::ai::canvas

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
