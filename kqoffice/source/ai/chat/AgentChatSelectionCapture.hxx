/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V4 M2 Day-1b — Selection capture for chat-to-document pipeline.
 * Captures current text selection from Writer/Calc/Impress surfaces.
 *
 * Day-1 stub: returns placeholder context. Real integration hooks into
 * SfxViewShell and SwWrtShell / ScViewData / sd::slidesorter infrastructure.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATSELECTIONCAPTURE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATSELECTIONCAPTURE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

/// Represents captured selection state from a document surface.
struct SelectionContext
{
    OUString surface;    ///< "writer", "calc", or "impress"
    OUString text;       ///< Captured text content (selection; empty at bare caret)
    OUString position;   ///< Paragraph/cell/slide reference (e.g., "para:42", "cell:B3", "slide:5")
    sal_Int32 length = 0;///< Character length of selected text
    /// Paragraph / nearby context for high-quality complete (not part of replace target).
    OUString beforeText; ///< text before caret in current paragraph (capped)
    OUString afterText;  ///< text after caret in current paragraph (capped)
    OUString paraText;   ///< full current paragraph plain text (capped)
};

/// Static capture utilities for current document selection.
class SAL_DLLPUBLIC_EXPORT AgentChatSelectionCapture
{
public:
    /// Capture selection from currently active document surface.
    /// Returns SelectionContext with surface type and captured content.
    static SelectionContext captureCurrent();

    /// Capture selection from Writer surface specifically.
    /// Hooks into SwWrtShell::GetSelection() and SwPaM range resolution.
    static SelectionContext captureFromWriter();

    /// Capture selection from Calc surface specifically.
    /// Hooks into ScViewData and ScMarkData for cell/selection state.
    static SelectionContext captureFromCalc();

    /// Capture selection from Impress/Draw surface specifically.
    /// Hooks into sd::slidesorter and SdrPageView selection state.
    static SelectionContext captureFromImpress();
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
