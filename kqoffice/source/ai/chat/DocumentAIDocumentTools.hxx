/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * GenOffice-inspired document tools for AI chat (M8):
 * - bounded document skeleton (index|type|preview) for every turn
 * - lazy block reads (no full-document dump by default)
 * - snapshot hash + stale guard before approved apply
 *
 * Tools never mutate the main document. Apply remains preview → approval → apply.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIDOCUMENTTOOLS_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIDOCUMENTTOOLS_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// One addressable top-level block for model/tool addressing.
struct DocumentToolBlock
{
    sal_Int32 index = 0; ///< 0-based tool index
    OUString type; ///< p | h1..h6 | li | table | cell-row | slide | shape | unknown
    OUString preview; ///< clipped plain preview
    OUString position; ///< para:N / cell:A1 / slide:N for locate/apply anchors
};

struct DocumentToolSkeleton
{
    bool hasDocument = false;
    OUString surface; ///< writer | calc | impress | none
    sal_Int32 blockCount = 0;
    OUString snapshotHash; ///< short SHA256 of structure (for stale detection)
    OUString selectionLine;
    OUString statsLine;
    std::vector<DocumentToolBlock> blocks;
    /// Prompt-ready skeleton text (budget-capped). Empty when no document.
    OUString formatted;
};

struct DocumentToolReadResult
{
    bool success = false;
    OUString content; ///< full-ish block text (clipped/paged)
    bool truncated = false;
    sal_Int32 nextOffset = 0;
    OUString error;
    OUString summary; ///< short UI activity chip
};

/// Visible tool-loop phase (GenOffice AgentPhase-inspired; UI-only).
/// Values: requesting | tool-running | responding | idle
struct DocumentToolActivity
{
    OUString phase; ///< tool-running | requesting
    OUString toolName; ///< get_document_context | read_blocks
    OUString summary; ///< human-readable chip (zh-CN preferred)
    bool mutated = false; ///< always false for document tools
};

/// Result of a local read-only tool pass before Provider / Agent LLM call.
struct DocumentToolPrepResult
{
    bool ranTools = false;
    bool mainDocumentMutation = false; ///< hard false
    OUString intent; ///< consult | edit | read | unknown (M13)
    OUString promptInjection; ///< tool outputs to prepend for the model
    OUString statusLabel; ///< short status for sidebar
    OUString snapshotHash;
    std::vector<DocumentToolActivity> activities;
};

/// Propose-only write tool result (M13). Never mutates the main document.
/// Caller stages into ApplyPlan buffer → human approval → DocumentAIApply.
struct DocumentToolProposeResult
{
    bool success = false;
    bool mainDocumentMutation = false; ///< hard false always
    OUString planId;
    OUString opType; ///< replace
    OUString target; ///< para:N / cell:A1 / slide:N
    OUString oldText;
    OUString newText;
    OUString snapshotHash;
    OUString surface; ///< writer | calc | impress
    OUString summary; ///< short activity chip
    OUString message;
    OUString previewSummaryZh;
};

/// Parsed model-side tool request (M14). Never implies main-doc mutation.
struct DocumentToolRequest
{
    bool isToolRequest = false;
    OUString toolName; ///< read_blocks
    sal_Int32 startIndex = 0; ///< 0-based inclusive
    sal_Int32 endIndex = 0; ///< 0-based inclusive
    OUString rawLine;
    OUString error;
};

/// One executed multi-round tool step (local only).
struct DocumentToolRoundResult
{
    bool executed = false;
    bool mainDocumentMutation = false; ///< hard false
    DocumentToolActivity activity;
    OUString toolResultBlock; ///< inject back into next Provider prompt
    OUString statusLabel;
};

/// Document tools shared by Chat / Agent (read-only; no main-doc mutation).
class SAL_DLLPUBLIC_EXPORT DocumentAIDocumentTools
{
public:
    /// Build bounded skeleton for the currently open document.
    static DocumentToolSkeleton buildSkeleton(sal_Int32 nMaxBlocks = 200,
                                              sal_Int32 nMaxChars = 8000,
                                              sal_Int32 nPreviewChars = 60);

    /// Read full content for an inclusive block index range (paged by offset).
    static DocumentToolReadResult readBlocks(sal_Int32 nStartIndex, sal_Int32 nEndIndex,
                                             sal_Int32 nOffset = 0,
                                             sal_Int32 nMaxChars = 24000);

    /// SHA256 short hash of current skeleton structure (empty when no document).
    static OUString computeSnapshotHash();

    /// Capture baseline after staging an ApplyPlan (first-mutate snapshot).
    static void markSeen(const OUString& rSnapshotHash);

    /// True when the open document structure changed since markSeen.
    static bool isStale();

    /// Clear process-local baseline (e.g. after apply success or clear plan).
    static void clearSeen();

    /// Last markSeen hash (for tests/status); empty if none.
    static OUString lastSeenHash();

    /// Ready-to-inject default context: skeleton + selection discipline notice.
    /// Does not include full document body.
    static OUString buildDefaultContextBlock(sal_Int32 nMaxChars = 8000);

    /// Chinese stale-apply failure for UI / ApplyResult.error.
    static OUString staleApplyErrorZh();

    /// True when an apply error string is the stale-document guard (for regenerate UX).
    static bool isStaleApplyError(const OUString& rError);

    // --- M8.4 explicit read-only tool channel (agent-style, local only) ---

    /// True when rewrite/translate/expand or explicit block-read intent needs full text.
    static bool wantsLazyBlockRead(const OUString& rUserPrompt, const OUString& rCapability,
                                   bool bHasSelection);

    // --- M13 intent + propose-only write channel (ApplyPlan buffer; no main-doc mutate) ---

    /// Classify user intent: consult | edit | read | unknown.
    /// consult = Q&A / explain (no write tools); edit = rewrite/translate; read = explicit blocks.
    static OUString classifyIntent(const OUString& rUserPrompt, const OUString& rCapability,
                                   bool bHasSelection);

    /// True when intent is pure consult (no rewrite). Explicit read_blocks still wins for lazy read.
    static bool wantsConsultIntent(const OUString& rUserPrompt, const OUString& rCapability,
                                   bool bHasSelection);

    /// True when intent is edit/rewrite-style (may still require approval before apply).
    static bool wantsEditIntent(const OUString& rUserPrompt, const OUString& rCapability,
                                bool bHasSelection);

    /**
     * Propose replace_blocks into an ApplyPlan buffer only.
     * Does NOT mutate the main document. Caller must stage plan + human approve + DocumentAIApply.
     *
     * @param nStartIndex 0-based inclusive
     * @param nEndIndex 0-based inclusive
     * @param rNewText proposed replacement (bounded)
     * @param rRationale short reason for evidence chip
     */
    static DocumentToolProposeResult proposeReplaceBlocks(sal_Int32 nStartIndex, sal_Int32 nEndIndex,
                                                          const OUString& rNewText,
                                                          const OUString& rRationale = OUString());

    /// Propose replace of current selection (or first selected block). No main-doc mutation.
    static DocumentToolProposeResult proposeReplaceSelection(const OUString& rNewText,
                                                             const OUString& rRationale = OUString());

    /// Map position token (para:N / slide:N / cell:A1) to skeleton block index; -1 if missing.
    static sal_Int32 findBlockIndexByPosition(const DocumentToolSkeleton& rSkeleton,
                                              const OUString& rPosition);

    /// Parse explicit ranges from prompt: "读第3-5段", "blocks 2-4", "read_blocks:1-3".
    /// Returns false when no explicit range is present.
    static bool parseExplicitBlockRange(const OUString& rUserPrompt, sal_Int32& rStartOut,
                                        sal_Int32& rEndOut);

    /**
     * Local tool pass before Provider call:
     * - always records get_document_context when skeleton is available
     * - optionally runs read_blocks for selection / explicit ranges / rewrite intents
     * - consult intent skips lazy read unless explicit range present
     * - never mutates the main document
     *
     * @param rUserPrompt raw user prompt
     * @param rCapability provider capability (chat/rewrite/…)
     * @param rSkeleton pre-built skeleton from bind (may be empty)
     * @param rSelectionPosition para:/cell:/slide: from selection capture
     * @param bHasSelection whether selection text is non-empty
     */
    static DocumentToolPrepResult prepareReadOnlyToolPass(
        const OUString& rUserPrompt, const OUString& rCapability,
        const DocumentToolSkeleton& rSkeleton, const OUString& rSelectionPosition,
        bool bHasSelection);

    // --- M14 multi-round read-only tool protocol (local only; no main-doc mutate) ---

    /// Max model↔tool rounds after the first Provider call (default 3).
    static constexpr sal_Int32 kDefaultMaxToolRounds = 3;

    /// Min selection chars to treat as "full text for edit" (skip multi-round).
    static constexpr sal_Int32 kSelectionCompleteMinChars = 4;

    /// Max selection chars still treated as quick-edit (light slot).
    static constexpr sal_Int32 kSelectionQuickEditMaxChars = 1500;

    /// Protocol notice injected so models can request more blocks mid-turn.
    static OUString buildMultiRoundToolProtocolNotice();

    /**
     * True when selection already supplies the full edit payload: skip multi-round
     * TOOL_REQUEST loop (saves one model round). Explicit block ranges still need tools.
     */
    static bool shouldSkipMultiRoundForSelection(bool bHasSelection, sal_Int32 nSelectionLen,
                                                 const OUString& rCapability,
                                                 const OUString& rUserPrompt);

    /// True for short selection rewrites that prefer the light model slot.
    static bool shouldUseLightSlotForSelectionEdit(bool bHasSelection, sal_Int32 nSelectionLen,
                                                   const OUString& rCapability);

    /**
     * Compact notice when selection text is complete: output rewrite only,
     * forbid TOOL_REQUEST (no extra multi-round).
     */
    static OUString buildSelectionCompleteEditNotice(const OUString& rSelectionText,
                                                     const OUString& rPosition,
                                                     sal_Int32 nMaxChars = 6000);

    /**
     * Parse a model reply for a local tool request.
     * Accepts:
     *   TOOL_REQUEST: read_blocks <start>-<end>   (0-based index, matches skeleton)
     *   TOOL_REQUEST: read_blocks para:N          (1-based Writer position)
     *   TOOL_REQUEST: read_blocks 读第3-5段
     *   read_blocks:0-2                           (standalone line)
     * Never mutates the main document.
     */
    static DocumentToolRequest parseModelToolRequest(const OUString& rModelText);

    /// True when the reply is primarily a tool request (not a final answer).
    static bool isPrimarilyToolRequest(const OUString& rModelText);

    /**
     * Execute one local read-only tool request (read_blocks only).
     * Returns toolResultBlock for re-injection; mainDocumentMutation always false.
     */
    static DocumentToolRoundResult executeReadOnlyToolRequest(
        const DocumentToolRequest& rRequest, const DocumentToolSkeleton& rSkeleton);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
