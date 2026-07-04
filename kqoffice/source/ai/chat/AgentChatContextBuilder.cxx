/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AgentChatContextBuilder.
 */

#include <AgentChatContextBuilder.hxx>
#include <AgentChatMentionResolver.hxx>
#include <AgentChatSelectionCapture.hxx>

#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <algorithm>

using namespace kqoffice::ai::chat;

namespace
{
/// Build the system prompt based on document type.
OUString buildSystemPromptImpl(const OUString& docType,
                               const std::vector<MentionContext>& /*mentions*/)
{
    if (docType.equalsIgnoreAsciiCase("writer"))
    {
        return u"You are a document editing assistant. You help users write, "
               u"edit, and format text documents. Respond with clear, "
               u"actionable suggestions."_ustr;
    }
    if (docType.equalsIgnoreAsciiCase("calc"))
    {
        return u"You are a spreadsheet formula assistant. You help users "
               u"analyze data, build formulas, and format spreadsheets. "
               u"Respond with precise cell references and formulas."_ustr;
    }
    if (docType.equalsIgnoreAsciiCase("impress"))
    {
        return u"You are a presentation design assistant. You help users "
               u"create and refine slide decks with effective layouts, "
               u"visuals, and messaging."_ustr;
    }

    // Default: generic office assistant
    return u"You are a helpful office productivity assistant. You help users "
           u"with documents, spreadsheets, and presentations."_ustr;
}

/// Build the document context section for the prompt string.
OUString buildDocumentSectionImpl(const ChatContext& ctx)
{
    OUStringBuffer buf;

    buf.append(u"--- Document Context ---\n");
    buf.append(u"Title: ");
    buf.append(ctx.documentTitle.isEmpty() ? u"(untitled)"_ustr : ctx.documentTitle);
    buf.append(u"\nType: ");
    buf.append(ctx.documentType.isEmpty() ? u"unknown"_ustr : ctx.documentType);

    if (ctx.hasSelection())
    {
        buf.append(u"\n--- Selection ---\n");
        buf.append(ctx.selectionText);
    }

    buf.append(u"\n");

    return buf.makeStringAndClear();
}
} // anonymous namespace

ChatContext AgentChatContextBuilder::build(const OUString& userInput,
                                          const std::vector<MentionContext>& mentions,
                                          const SelectionContext& selection)
{
    ChatContext ctx;

    // Extract user query (strip @mention tokens)
    ctx.systemPrompt = AgentChatContextBuilder::buildSystemPrompt(selection.surface, mentions);

    // Set document title and type from selection context
    ctx.documentTitle = u"Current Document"_ustr; // Day-1: placeholder; real integration picks from model
    ctx.documentType = selection.surface;

    // Capture selection text
    ctx.selectionText = selection.text;

    // Truncate recentMessages to last 20 (empty for a fresh context)
    // Caller populates this field before passing to toPromptString.
    ctx.recentMessages.clear();

    SAL_INFO("kqoffice.ai.chat",
             "Built ChatContext: docType=" << selection.surface
                 << ", hasSelection=" << (selection.text.isEmpty() ? 0 : 1)
                 << ", input=[" << userInput << "]");

    return ctx;
}

OUString AgentChatContextBuilder::toPromptString(const ChatContext& ctx)
{
    OUStringBuffer buf;

    // System prompt
    buf.append(u"=== System Instruction ===\n");
    buf.append(ctx.systemPrompt);
    buf.append(u"\n\n");

    // Document context
    buf.append(buildDocumentSectionImpl(ctx));

    // Conversation history (last 20)
    if (!ctx.recentMessages.empty())
    {
        buf.append(u"--- Conversation History ---\n");
        const sal_Int32 start = std::max(static_cast<sal_Int32>(0),
                                         static_cast<sal_Int32>(ctx.recentMessages.size()) - 20);
        for (sal_Int32 i = start; i < static_cast<sal_Int32>(ctx.recentMessages.size()); ++i)
        {
            buf.append(ctx.recentMessages[i]);
            buf.append(u"\n");
        }
    }

    // User query placeholder
    buf.append(u"--- User Request ---\n");

    SAL_INFO("kqoffice.ai.chat",
             "Generated prompt string, length=" << buf.toString().getLength());

    return buf.makeStringAndClear();
}

OUString AgentChatContextBuilder::extractUserQuery(const OUString& input,
                                                   const std::vector<MentionContext>& mentions)
{
    // Start with the full input
    OUString result = input;

    // Strip each mention in reverse order (to preserve positions during removal)
    for (auto it = mentions.rbegin(); it != mentions.rend(); ++it)
    {
        const sal_Int32 idx = result.indexOf(it->rawText);
        if (idx >= 0)
        {
            // Remove the mention token and any trailing whitespace
            sal_Int32 end = idx + it->rawText.getLength();
            // Skip trailing whitespace
            while (end < result.getLength() && result[end] == ' ')
                ++end;
            result = result.replaceAt(idx, end - idx, u""_ustr);
        }
    }

    // Trim leading/trailing whitespace
    result = result.trim();

    return result;
}

OUString AgentChatContextBuilder::buildSystemPrompt(const OUString& docType,
                                                    const std::vector<MentionContext>& mentions)
{
    return buildSystemPromptImpl(docType, mentions);
}

OUString AgentChatContextBuilder::buildDocumentSection(const ChatContext& ctx)
{
    return buildDocumentSectionImpl(ctx);
}

OUString ChatContext::debugString() const
{
    OUStringBuffer buf;
    buf.append(u"ChatContext{");
    buf.append(u"title=\"");
    buf.append(documentTitle);
    buf.append(u"\", type=\"");
    buf.append(documentType);
    buf.append(u"\", hasSelection=");
    buf.append(hasSelection() ? u"true"_ustr : u"false"_ustr);
    buf.append(u", messages=");
    buf.append(static_cast<sal_Int32>(recentMessages.size()));
    buf.append(u", maxTokens=");
    buf.append(maxTokens);
    buf.append(u"}");
    return buf.makeStringAndClear();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
