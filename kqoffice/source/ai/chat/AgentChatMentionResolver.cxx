/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AgentChatMentionResolver.
 */

#include <AgentChatMentionResolver.hxx>

#include <rtl/strbuf.hxx>
#include <sal/log.hxx>

#include <AiI18nStrings.hxx>

using namespace kqoffice::ai::chat;

namespace
{
/// Scan forward from 'atPos' looking for a valid mention token.
/// Returns the index after the token, or -1 if none found.
sal_Int32 scanMentionImpl(const OUString& input, sal_Int32 atPos)
{
    const sal_Int32 len = input.getLength();
    if (atPos >= len || input[atPos] != '@')
        return -1;

    // Find token end: whitespace, newline, or end of string
    sal_Int32 end = atPos + 1;
    while (end < len)
    {
        sal_Unicode c = input[end];
        // Token ends at whitespace, newline, or special chars
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ':')
            break;
        // Also stop at common punctuation that shouldn't be in token
        if (c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ')')
            break;
        ++end;
    }
    return (end > atPos + 1) ? end : -1;
}

/// Classify a raw mention token (without @) into MentionContext.
MentionContext classifyTokenImpl(const OUString& token, const OUString& fullRaw)
{
    MentionContext ctx;
    ctx.rawText = fullRaw;

    if (token.equalsIgnoreAsciiCase("doc"))
    {
        ctx.type = MentionContext::Type::Document;
    }
    else if (token.equalsIgnoreAsciiCase("selection"))
    {
        ctx.type = MentionContext::Type::Selection;
    }
    else if (token.startsWithIgnoreAsciiCase("connector"))
    {
        ctx.type = MentionContext::Type::Connector;
        // Extract ID after colon if present: connector:ID
        if (fullRaw.indexOf(':') > 0)
        {
            sal_Int32 colonPos = fullRaw.indexOf(':');
            if (colonPos + 1 < fullRaw.getLength())
            {
                ctx.connectorId = fullRaw.copy(colonPos + 1).trim();
            }
        }
    }
    else
    {
        // Unknown mention type - default to Document for safety
        ctx.type = MentionContext::Type::Document;
    }
    return ctx;
}
} // anonymous namespace

std::vector<MentionContext> AgentChatMentionResolver::parse(const OUString& input)
{
    std::vector<MentionContext> results;
    const sal_Int32 len = input.getLength();

    for (sal_Int32 i = 0; i < len; ++i)
    {
        if (input[i] == '@')
        {
            sal_Int32 tokenEnd = scanMentionImpl(input, i);
            if (tokenEnd > 0)
            {
                OUString rawToken = input.copy(i, tokenEnd - i);
                OUString tokenName = rawToken.copy(1).toAsciiLowerCase(); // strip @

                MentionContext ctx = classifyTokenImpl(tokenName, rawToken);
                results.push_back(ctx);

                // Advance past this token
                i = tokenEnd - 1;
            }
        }
    }

    SAL_INFO("kqoffice.ai.chat", "Parsed " << results.size() << " mentions from input");
    return results;
}

bool AgentChatMentionResolver::hasMention(const OUString& input)
{
    const sal_Int32 len = input.getLength();
    for (sal_Int32 i = 0; i < len; ++i)
    {
        if (input[i] == '@')
        {
            sal_Int32 tokenEnd = scanMentionImpl(input, i);
            if (tokenEnd > 0)
                return true;
        }
    }
    return false;
}

OUString AgentChatMentionResolver::resolveTarget(const MentionContext& ctx)
{
    switch (ctx.type)
    {
        case MentionContext::Type::Document:
            // In a real integration, this would resolve to current document URI
            return u"current-document"_ustr;

        case MentionContext::Type::Selection:
            return u"selection"_ustr;

        case MentionContext::Type::Connector:
            return ctx.connectorId.isEmpty() ? u"connector"_ustr : ctx.connectorId;

        default:
            return u""_ustr;
    }
}

sal_Int32 AgentChatMentionResolver::scanMention(const OUString& input, sal_Int32 start)
{
    return scanMentionImpl(input, start);
}

MentionContext AgentChatMentionResolver::classifyToken(const OUString& token)
{
    return classifyTokenImpl(token, token);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
