/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V4 M2 Day-1c — ApplyPlan diff applier engine.
 * Routes DiffOperations through the document manipulation pipeline.
 * Day-1: logs operations via SAL_INFO; real document mutation via UNO
 * is wired in Day-2+.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATDIFFAPPLIER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATDIFFAPPLIER_HXX

#include <AgentChatDiffExtractor.hxx>

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// Result of applying an ApplyPlan or individual DiffOperation.
struct ApplyResult
{
    bool success = false;             ///< Whether the apply succeeded
    OUString error;                   ///< Error message if not successful
    std::vector<OUString> appliedOps; ///< IDs/descriptions of applied operations
};

/// Static applier engine for ApplyPlan diff operations.
class SAL_DLLPUBLIC_EXPORT AgentChatDiffApplier
{
public:
    /// Apply all operations in the plan sequentially.
    /// Returns an ApplyResult summarizing success/failure per operation.
    static ApplyResult apply(const ApplyPlan& plan);

    /// Apply a single DiffOperation.
    /// Dispatches by opType: "insert", "delete", "replace", "format".
    static ApplyResult applyOperation(const DiffOperation& op);

    /// Check whether a DiffOperation can be applied in the current context.
    static bool canApply(const DiffOperation& op);

    /// Undo the last applied plan by applying its inverse.
    /// Returns the undo result. The original plan must have been successfully
    /// applied before undoing.
    static ApplyResult undo();

    /// Push a successfully-applied plan onto the undo stack.
    /// Called automatically by apply() when the plan succeeds.
    static void pushUndo(const ApplyPlan& plan);

    /// Check whether undo is available.
    static bool canUndo();

    /// Clear the undo stack (e.g., on document close).
    static void clearUndoStack();

    /**
     * Sanitize model output before write-back (M17):
     * strip markdown fences, leading labels like「改写：」, surrounding quotes.
     * Does not mutate the document.
     */
    static OUString sanitizeApplyText(const OUString& rText);

    /**
     * Normalize a plan for Writer/Calc/Impress apply (M17):
     * - fill empty targets from live selection
     * - sanitize newText
     * - prefer target=selection when live selection matches oldText
     * Never mutates the main document by itself.
     */
    static ApplyPlan normalizePlanForApply(const ApplyPlan& rPlan);

private:
    static std::vector<ApplyPlan> s_undoStack;
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
