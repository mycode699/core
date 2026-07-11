/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V4 M2 Day-1c — Context builder for chat-to-document pipeline.
 * Combines user input, @mentions, and selection into a prompt context.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATCONTEXTBUILDER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATCONTEXTBUILDER_HXX

#include <AgentChatMentionResolver.hxx>
#include <AgentChatSelectionCapture.hxx>

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// Complete context for generating AI chat responses.
struct ChatContext
{
    OUString documentTitle; ///< Current document title/filename
    OUString documentType; ///< "writer", "calc", "impress"
    OUString selectionText; ///< Captured selection content (if any)
    OUString selectionPosition; ///< para:/cell:/slide: anchor when available
    OUString userQuery; ///< User request after @mention strip
    std::vector<OUString> recentMessages; ///< Conversation history (last N messages)
    OUString systemPrompt; ///< System instruction for the AI
    sal_Int32 maxTokens = 2048; ///< Max response tokens

    /// Check if context has meaningful selection.
    bool hasSelection() const { return !selectionText.isEmpty(); }

    /// Build a compact debug string representation.
    OUString debugString() const;
};

/// Builder for ChatContext from user input and document state.
class SAL_DLLPUBLIC_EXPORT AgentChatContextBuilder
{
public:
    /// Build ChatContext from user input, resolved mentions, and selection.
    /// @param userInput Raw user chat input
    /// @param mentions Resolved @mention contexts from input
    /// @param selection Captured selection context from document
    /// @return Complete ChatContext ready for AI provider
    static ChatContext build(const OUString& userInput,
                            const std::vector<MentionContext>& mentions,
                            const SelectionContext& selection);

    /// Serialize ChatContext to a prompt string for LLM consumption.
    /// Formats context as structured text: system prompt + context sections + user query.
    static OUString toPromptString(const ChatContext& ctx);

    /// Extract the user query portion (input minus @mention tokens).
    static OUString extractUserQuery(const OUString& input,
                                    const std::vector<MentionContext>& mentions);

private:
    /// Build system prompt based on document type and mentions.
    static OUString buildSystemPrompt(const OUString& docType,
                                     const std::vector<MentionContext>& mentions);

    /// Build document context section for the prompt.
    static OUString buildDocumentSection(const ChatContext& ctx);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
