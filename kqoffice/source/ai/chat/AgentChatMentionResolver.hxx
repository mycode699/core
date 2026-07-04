/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V4 M2 Day-1a — Mention resolution for chat-to-document pipeline.
 * Parses @doc, @selection, @connector:ID patterns to extract context targets.
 *
 * No external regex: manual linear scan matching the V2 OllamaAdapter
 * parse style keeps the build hermetic and dependency-free.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATMENTIONRESOLVER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATMENTIONRESOLVER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// Represents a resolved @mention context in user chat input.
struct MentionContext
{
    /// Type of mention target.
    enum class Type : sal_uInt8 { Document, Selection, Connector };

    Type type = Type::Document;
    OUString connectorId; ///< Only populated for Connector type
    OUString rawText;     ///< Original @mention text from input

    /// Check if this context references a document.
    bool isDocument() const { return type == Type::Document; }
    /// Check if this context references current selection.
    bool isSelection() const { return type == Type::Selection; }
    /// Check if this context references an external connector.
    bool isConnector() const { return type == Type::Connector; }
};

/// Static resolver for @mention patterns in chat input.
class SAL_DLLPUBLIC_EXPORT AgentChatMentionResolver
{
public:
    /// Parse all @mentions from input text.
    /// Returns vector of MentionContext in order of appearance.
    /// Patterns: @doc, @selection, @connector:ID
    static std::vector<MentionContext> parse(const OUString& input);

    /// Quick check if input contains any @mention.
    static bool hasMention(const OUString& input);

    /// Resolve a MentionContext to a target identifier string.
    /// For Document: returns document URI/ref
    /// For Selection: returns "selection" sentinel
    /// For Connector: returns connector ID
    static OUString resolveTarget(const MentionContext& ctx);

private:
    /// Helper: scan for @ at position, return end of token or -1.
    static sal_Int32 scanMention(const OUString& input, sal_Int32 start);

    /// Helper: classify and build MentionContext from raw token.
    static MentionContext classifyToken(const OUString& token);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
