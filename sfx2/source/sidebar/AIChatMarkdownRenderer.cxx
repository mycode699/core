/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: In-app AI chat).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatMarkdownRenderer.hxx"

#include <rtl/ustrbuf.hxx>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral CODE_FENCE_MARKER = u"\x60\x60\x60";

bool ContainsRawHtml(const OUString& rText)
{
    const OUString aLower = rText.toAsciiLowerCase();
    return aLower.indexOf(u"<script"_ustr) >= 0 || aLower.indexOf(u"<iframe"_ustr) >= 0
           || aLower.indexOf(u"<img"_ustr) >= 0 || aLower.indexOf(u"<a "_ustr) >= 0
           || aLower.indexOf(u"<div"_ustr) >= 0 || aLower.indexOf(u"<span"_ustr) >= 0
           || aLower.indexOf(u"<table"_ustr) >= 0 || aLower.indexOf(u"</"_ustr) >= 0;
}

bool ContainsRemoteImage(const OUString& rText)
{
    const OUString aLower = rText.toAsciiLowerCase();
    return aLower.indexOf(u"!["_ustr) >= 0
           && (aLower.indexOf(u"](http://"_ustr) >= 0
               || aLower.indexOf(u"](https://"_ustr) >= 0);
}

OUString StripHeadingMarker(OUString aLine)
{
    sal_Int32 nIndex = 0;
    while (nIndex < aLine.getLength() && nIndex < 6 && aLine[nIndex] == '#')
        ++nIndex;
    if (nIndex > 0 && nIndex < aLine.getLength() && aLine[nIndex] == ' ')
        return aLine.copy(nIndex + 1).trim();
    return aLine;
}

OUString StripListMarker(OUString aLine)
{
    aLine = aLine.trim();
    if (aLine.startsWith(u"- "_ustr) || aLine.startsWith(u"* "_ustr))
        return u"* "_ustr + aLine.copy(2).trim();

    const sal_Int32 nDot = aLine.indexOf('.');
    if (nDot > 0 && nDot < 4)
    {
        bool bDigits = true;
        for (sal_Int32 i = 0; i < nDot; ++i)
        {
            if (aLine[i] < '0' || aLine[i] > '9')
            {
                bDigits = false;
                break;
            }
        }
        if (bDigits && nDot + 1 < aLine.getLength() && aLine[nDot + 1] == ' ')
            return u"* "_ustr + aLine.copy(nDot + 2).trim();
    }
    return aLine;
}

OUString RenderTableLine(const OUString& rLine)
{
    OUStringBuffer aBuffer;
    sal_Int32 nStart = 0;
    bool bFirstCell = true;
    for (sal_Int32 nIndex = 0; nIndex <= rLine.getLength(); ++nIndex)
    {
        if (nIndex != rLine.getLength() && rLine[nIndex] != '|')
            continue;

        OUString aCell = rLine.copy(nStart, nIndex - nStart).trim();
        nStart = nIndex + 1;
        if (aCell.isEmpty())
            continue;
        if (!bFirstCell)
            aBuffer.append(u" | "_ustr);
        aBuffer.append(aCell);
        bFirstCell = false;
    }
    return aBuffer.makeStringAndClear();
}

bool IsTableSeparator(const OUString& rLine)
{
    const OUString aLine = rLine.trim();
    if (aLine.isEmpty())
        return false;
    for (sal_Int32 i = 0; i < aLine.getLength(); ++i)
    {
        const sal_Unicode c = aLine[i];
        if (c != '|' && c != '-' && c != ':' && c != ' ')
            return false;
    }
    return aLine.indexOf('-') >= 0 && aLine.indexOf('|') >= 0;
}
}

AIChatMarkdownRenderResult RenderMarkdownSubset(const OUString& rMarkdown)
{
    AIChatMarkdownRenderResult aResult;
    if (ContainsRawHtml(rMarkdown))
    {
        aResult.Rejected = true;
        aResult.RejectionReason = u"raw-html"_ustr;
        return aResult;
    }
    if (ContainsRemoteImage(rMarkdown))
    {
        aResult.Rejected = true;
        aResult.RejectionReason = u"remote-image"_ustr;
        return aResult;
    }

    OUStringBuffer aOutput;
    bool bInCodeFence = false;
    sal_Int32 nStart = 0;
    while (nStart <= rMarkdown.getLength())
    {
        sal_Int32 nBreak = rMarkdown.indexOf('\n', nStart);
        if (nBreak < 0)
            nBreak = rMarkdown.getLength();
        const OUString aLine = rMarkdown.copy(nStart, nBreak - nStart);
        nStart = nBreak + 1;

        if (aLine.startsWith(CODE_FENCE_MARKER))
        {
            bInCodeFence = !bInCodeFence;
            aOutput.append(bInCodeFence ? u"[code]"_ustr : u"[/code]"_ustr);
        }
        else if (bInCodeFence)
        {
            aOutput.append(u"    "_ustr + aLine);
        }
        else if (aLine.startsWith(u"#"_ustr))
        {
            aOutput.append(StripHeadingMarker(aLine));
        }
        else if (aLine.trim().startsWith(u"|"_ustr) && !IsTableSeparator(aLine))
        {
            aOutput.append(RenderTableLine(aLine));
        }
        else if (IsTableSeparator(aLine))
        {
            continue;
        }
        else
        {
            aOutput.append(StripListMarker(aLine));
        }

        if (nStart <= rMarkdown.getLength())
            aOutput.append('\n');
    }

    aResult.Text = aOutput.makeStringAndClear().trim();
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
