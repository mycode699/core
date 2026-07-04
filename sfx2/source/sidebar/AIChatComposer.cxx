/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈 office project (V4 M1: AI-native workspace).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatComposer.hxx"
#include "AIChatSlashCommands.hxx"

#include <rtl/ustrbuf.hxx>
#include <vcl/event.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/keycod.hxx>

#include <algorithm>

namespace sfx2::sidebar
{
namespace
{

bool IsMentionBoundary(sal_Unicode c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u','
           || c == u';' || c == u'.' || c == u')' || c == u']' || c == u'}'
           || c == u'!' || c == u'?' || c == u':' || c == u'"';
}

bool IsValidConnectorIdChar(sal_Unicode c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-';
}

bool IsValidConnectorMention(const OUString& rMention)
{
    constexpr OUStringLiteral CONNECTOR_PREFIX = u"@connector:";
    if (!rMention.startsWith(CONNECTOR_PREFIX))
        return false;

    const OUString sId = rMention.copy(CONNECTOR_PREFIX.getLength());
    if (sId.isEmpty())
        return false;

    for (sal_Int32 i = 0; i < sId.getLength(); ++i)
    {
        if (!IsValidConnectorIdChar(sId[i]))
            return false;
    }
    return true;
}

bool IsValidMention(const OUString& rToken)
{
    if (rToken.isEmpty() || rToken[0] != u'@')
        return false;

    if (rToken == u"@doc"_ustr || rToken == u"@selection"_ustr)
        return true;

    if (IsValidConnectorMention(rToken))
        return true;

    return false;
}

} // anonymous namespace

AIChatComposer::AIChatComposer(weld::Entry* pInput)
    : m_pInput(pInput)
{
    m_pInput->connect_changed(LINK(this, AIChatComposer, OnInputChanged));
    m_pInput->connect_key_press(LINK(this, AIChatComposer, OnKeyPress));
}

AIChatComposer::~AIChatComposer() = default;

std::vector<AIChatMention> AIChatComposer::ParseMentions() const
{
    if (!m_pInput)
        return {};
    return ParseMentions(m_pInput->get_text());
}

std::vector<AIChatMention> AIChatComposer::ParseMentions(const OUString& rText)
{
    std::vector<AIChatMention> aResult;

    sal_Int32 nPos = 0;
    while (nPos < rText.getLength())
    {
        if (rText[nPos] != u'@')
        {
            ++nPos;
            continue;
        }

        sal_Int32 nEnd = nPos + 1;
        while (nEnd < rText.getLength() && !IsMentionBoundary(rText[nEnd]))
            ++nEnd;

        const OUString sMention = rText.copy(nPos, nEnd - nPos);

        if (sMention == u"@selection"_ustr)
        {
            AIChatMention aMention;
            aMention.type = u"selection"_ustr;
            aMention.id = OUString();
            aResult.push_back(aMention);
        }
        else if (sMention == u"@doc"_ustr)
        {
            AIChatMention aMention;
            aMention.type = u"doc"_ustr;
            aMention.id = OUString();
            aResult.push_back(aMention);
        }
        else if (IsValidConnectorMention(sMention))
        {
            AIChatMention aMention;
            aMention.type = u"connector"_ustr;
            constexpr OUStringLiteral CONNECTOR_PREFIX = u"@connector:";
            aMention.id = sMention.copy(CONNECTOR_PREFIX.getLength());
            aResult.push_back(aMention);
        }

        nPos = nEnd;
    }

    return aResult;
}

bool AIChatComposer::HasMention(const OUString& rText)
{
    return !ParseMentions(rText).empty();
}

OUString AIChatComposer::StripMentions(const OUString& rText)
{
    rtl::OUStringBuffer aResult;
    sal_Int32 nPos = 0;

    while (nPos < rText.getLength())
    {
        if (rText[nPos] != u'@')
        {
            aResult.append(rText[nPos]);
            ++nPos;
            continue;
        }

        sal_Int32 nEnd = nPos + 1;
        while (nEnd < rText.getLength() && !IsMentionBoundary(rText[nEnd]))
            ++nEnd;

        const OUString sMention = rText.copy(nPos, nEnd - nPos);

        if (!IsValidMention(sMention))
            aResult.append(sMention);

        nPos = nEnd;
    }

    return aResult.makeStringAndClear();
}

bool AIChatComposer::IsInsideMention() const
{
    if (!m_pInput)
        return false;

    sal_Int32 nCursor = m_pInput->get_position();
    const OUString sText = m_pInput->get_text();

    if (nCursor < 0 || nCursor >= sText.getLength())
        return false;

    sal_Int32 nPos = nCursor - 1;
    while (nPos >= 0 && !IsMentionBoundary(sText[nPos]))
    {
        if (sText[nPos] == u'@')
            return true;
        --nPos;
    }

    return false;
}

OUString AIChatComposer::GetCurrentMentionToken() const
{
    if (!m_pInput)
        return OUString();

    sal_Int32 nCursor = m_pInput->get_position();
    const OUString sText = m_pInput->get_text();

    if (nCursor <= 0 || nCursor > sText.getLength())
        return OUString();

    sal_Int32 nStart = nCursor - 1;
    while (nStart >= 0 && !IsMentionBoundary(sText[nStart]))
    {
        if (sText[nStart] == u'@')
            break;
        --nStart;
    }

    if (nStart < 0 || sText[nStart] != u'@')
        return OUString();

    sal_Int32 nEnd = nCursor;
    while (nEnd < sText.getLength() && !IsMentionBoundary(sText[nEnd]))
        ++nEnd;

    return sText.copy(nStart, nEnd - nStart);
}

void AIChatComposer::FindMentionBounds(sal_Int32& rStart, sal_Int32& rEnd) const
{
    rStart = -1;
    rEnd = -1;

    if (!m_pInput)
        return;

    sal_Int32 nCursor = m_pInput->get_position();
    const OUString sText = m_pInput->get_text();

    if (nCursor <= 0 || nCursor > sText.getLength())
        return;

    sal_Int32 nStart = nCursor - 1;
    while (nStart >= 0 && !IsMentionBoundary(sText[nStart]))
    {
        if (sText[nStart] == u'@')
            break;
        --nStart;
    }

    if (nStart < 0 || sText[nStart] != u'@')
        return;

    sal_Int32 nEnd = nCursor;
    while (nEnd < sText.getLength() && !IsMentionBoundary(sText[nEnd]))
        ++nEnd;

    rStart = nStart;
    rEnd = nEnd;
}

std::vector<OUString> AIChatComposer::BuildSuggestions(const OUString& rPrefix) const
{
    std::vector<OUString> aSuggestions;

    const std::vector<OUString> aKnownMentions = {
        u"@doc"_ustr, u"@selection"_ustr
    };

    for (const auto& rMention : aKnownMentions)
    {
        if (rMention.startsWith(rPrefix))
            aSuggestions.push_back(rMention);
    }

    if (rPrefix.startsWith(u"@connector"_ustr))
    {
        const std::vector<OUString> aConnectors = {
            u"@connector:google-drive"_ustr,
            u"@connector:sharepoint"_ustr,
            u"@connector:onedrive"_ustr,
        };
        for (const auto& rConn : aConnectors)
        {
            if (rConn.startsWith(rPrefix))
                aSuggestions.push_back(rConn);
        }
    }

    return aSuggestions;
}

void AIChatComposer::ShowMentionCompletion()
{
    if (!m_pInput)
        return;

    const OUString sToken = GetCurrentMentionToken();
    if (sToken.isEmpty() || sToken.getLength() < 2)
    {
        DismissMentionCompletion();
        return;
    }

    m_aCurrentSuggestions = BuildSuggestions(sToken);
    if (m_aCurrentSuggestions.empty())
    {
        DismissMentionCompletion();
        return;
    }

    m_nHighlightedSuggestion = 0;
    m_bCompletionVisible = true;
}

void AIChatComposer::DismissMentionCompletion()
{
    m_bCompletionVisible = false;
    m_aCurrentSuggestions.clear();
    m_nHighlightedSuggestion = 0;
}

void AIChatComposer::SelectCompletion()
{
    if (!m_bCompletionVisible || m_aCurrentSuggestions.empty())
        return;

    if (!m_pInput)
        return;

    const OUString sSelected = m_aCurrentSuggestions[m_nHighlightedSuggestion];

    sal_Int32 nStart = -1, nEnd = -1;
    FindMentionBounds(nStart, nEnd);

    if (nStart < 0 || nEnd < 0)
        return;

    OUString sText = m_pInput->get_text();
    OUString sNewText = sText.replaceAt(nStart, nEnd - nStart, sSelected + u" ");
    m_pInput->set_text(sNewText);
    m_pInput->set_position(nStart + sSelected.getLength() + 1);

    DismissMentionCompletion();
}

IMPL_LINK_NOARG(AIChatComposer, OnInputChanged, weld::Entry&, void)
{
    if (IsInsideMention())
        ShowMentionCompletion();
    else
        DismissMentionCompletion();
}

IMPL_LINK(AIChatComposer, OnKeyPress, const KeyEvent&, rEvent, bool)
{
    const sal_uInt16 nKeyCode = rEvent.GetKeyCode().GetCode();

    if (!m_bCompletionVisible)
        return false;

    switch (nKeyCode)
    {
        case KEY_RETURN:
            SelectCompletion();
            return true;

        case KEY_ESCAPE:
            DismissMentionCompletion();
            return true;

        case KEY_DOWN:
            if (!m_aCurrentSuggestions.empty())
            {
                m_nHighlightedSuggestion = (m_nHighlightedSuggestion + 1)
                                           % m_aCurrentSuggestions.size();
            }
            return true;

        case KEY_UP:
            if (!m_aCurrentSuggestions.empty())
            {
                m_nHighlightedSuggestion
                    = (m_nHighlightedSuggestion + m_aCurrentSuggestions.size() - 1)
                      % m_aCurrentSuggestions.size();
            }
            return true;

        default:
            break;
    }

    return false;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */