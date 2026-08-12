/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Document AI Fabric — single entry to attach current document/selection
 * context to every Provider / Agent call (Writer · Calc · Impress).
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAICONTEXT_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAICONTEXT_HXX

#include <AgentChatContextBuilder.hxx>
#include <AgentChatSelectionCapture.hxx>

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

/// Result of binding a user prompt to the active document surface.
struct DocumentAIBinding
{
    SelectionContext selection;
    ChatContext chat;
    /// Full prompt string for Provider (system + doc + selection + user).
    OUString enrichedPrompt;
    /// Compact context field for ProviderRequest.context (surface + position + clip).
    OUString providerContext;
    /// Short status line for UI ("writer · 选区 128 字").
    OUString statusLabel;
    /// GenOffice-style bounded document skeleton (index|type|preview); empty if none.
    OUString documentSkeleton;
    /// Snapshot hash of the skeleton used for this bind (stale apply guard).
    OUString documentSnapshotHash;
    bool hasDocument = false;
    bool hasSelection = false;
    bool hasDocumentSkeleton = false;
};

/// Static helpers: always call before Provider / AgentStepRunner.
class SAL_DLLPUBLIC_EXPORT DocumentAIContext
{
public:
    /// Capture current surface + build enriched prompt for rUserInput.
    /// Never throws; empty document yields generic office binding.
    static DocumentAIBinding bindUserInput(const OUString& rUserInput);

    /// Truncate selection text for provider.context (keeps prompt budget).
    static OUString compactContextField(const SelectionContext& rSelection,
                                        sal_Int32 nMaxChars = 4000);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
