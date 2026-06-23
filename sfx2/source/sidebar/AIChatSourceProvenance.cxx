/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace source provenance).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatSourceProvenance.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <unotools/pathoptions.hxx>

#include <string_view>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral SOURCE_PROVENANCE_DIR_NAME = u"kqoffice-v3-ai-source-provenance";
constexpr OUStringLiteral SOURCE_PROVENANCE_FILE_NAME = u"provenance.tsv";
constexpr sal_uInt64 MAX_PROVENANCE_BYTES = 1024 * 1024;

OUString EnsureNoTrailingSlash(OUString sUrl)
{
    while (sUrl.endsWith(u"/"))
        sUrl = sUrl.copy(0, sUrl.getLength() - 1);
    return sUrl;
}

OUString EscapeField(const OUString& rValue)
{
    OUStringBuffer aBuffer;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        switch (c)
        {
            case '\\':
                aBuffer.append(u"\\\\"_ustr);
                break;
            case '\n':
                aBuffer.append(u"\\n"_ustr);
                break;
            case '\r':
                aBuffer.append(u"\\r"_ustr);
                break;
            case '\t':
                aBuffer.append(u"\\t"_ustr);
                break;
            default:
                aBuffer.append(c);
                break;
        }
    }
    return aBuffer.makeStringAndClear();
}

OUString UnescapeField(std::u16string_view aValue)
{
    OUStringBuffer aBuffer;
    for (size_t i = 0; i < aValue.size(); ++i)
    {
        if (aValue[i] != u'\\' || i + 1 >= aValue.size())
        {
            aBuffer.append(aValue[i]);
            continue;
        }

        const char16_t cNext = aValue[++i];
        switch (cNext)
        {
            case u'n':
                aBuffer.append(u'\n');
                break;
            case u'r':
                aBuffer.append(u'\r');
                break;
            case u't':
                aBuffer.append(u'\t');
                break;
            case u'\\':
                aBuffer.append(u'\\');
                break;
            default:
                aBuffer.append(cNext);
                break;
        }
    }
    return aBuffer.makeStringAndClear();
}

bool AppendUtf8Line(const OUString& rUrl, const OUString& rLine)
{
    const OString sUtf8 = OUStringToOString(rLine, RTL_TEXTENCODING_UTF8);
    osl::File aFile(rUrl);
    osl::FileBase::RC eError = aFile.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (eError != osl::FileBase::E_None)
        eError = aFile.open(osl_File_OpenFlag_Write);
    if (eError != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    aFile.getSize(nSize);
    aFile.setPos(osl_Pos_Absolut, nSize);

    sal_uInt64 nWritten = 0;
    const bool bWritten
        = aFile.write(sUtf8.getStr(), sUtf8.getLength(), nWritten) == osl::FileBase::E_None
          && nWritten == static_cast<sal_uInt64>(sUtf8.getLength());
    aFile.close();
    return bWritten;
}

bool ReadUtf8File(const OUString& rUrl, OString& rContent)
{
    osl::File aFile(rUrl);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_PROVENANCE_BYTES)
    {
        aFile.close();
        return false;
    }

    std::vector<char> aBuffer(static_cast<size_t>(nSize));
    sal_uInt64 nRead = 0;
    if (nSize > 0
        && aFile.read(aBuffer.data(), nSize, nRead) != osl::FileBase::E_None)
    {
        aFile.close();
        return false;
    }
    aFile.close();

    if (nRead != nSize)
        return false;

    rContent = OString(aBuffer.data(), static_cast<sal_Int32>(aBuffer.size()));
    return true;
}

bool ParseProvenanceLine(const OUString& rLine, AIChatSourceProvenanceEntry& rEntry)
{
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 9 || aFields[0].isEmpty())
        return false;

    rEntry.SourceId = aFields[0];
    rEntry.SourceType = aFields[1];
    rEntry.CitationId = aFields[2];
    rEntry.EvidenceId = aFields[3];
    rEntry.HashReference = aFields[4];
    rEntry.SourceSurface = aFields[5];
    rEntry.OpenTarget = aFields[6];
    rEntry.SpanReference = aFields[7];
    rEntry.ReviewId = aFields[8];
    return true;
}
}

AIChatSourceProvenance::AIChatSourceProvenance()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + SOURCE_PROVENANCE_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sProvenanceUrl = m_sStorageRootUrl + u"/"_ustr + SOURCE_PROVENANCE_FILE_NAME;
}

bool AIChatSourceProvenance::RegisterSource(const AIChatSourceProvenanceEntry& rEntry) const
{
    const OUString sLine
        = EscapeField(rEntry.SourceId) + u"\t"_ustr + EscapeField(rEntry.SourceType)
          + u"\t"_ustr + EscapeField(rEntry.CitationId) + u"\t"_ustr
          + EscapeField(rEntry.EvidenceId) + u"\t"_ustr + EscapeField(rEntry.HashReference)
          + u"\t"_ustr + EscapeField(rEntry.SourceSurface) + u"\t"_ustr
          + EscapeField(rEntry.OpenTarget) + u"\t"_ustr + EscapeField(rEntry.SpanReference)
          + u"\t"_ustr + EscapeField(rEntry.ReviewId) + u"\n"_ustr;
    return AppendUtf8Line(m_sProvenanceUrl, sLine);
}

std::vector<AIChatSourceProvenanceEntry> AIChatSourceProvenance::LoadEntries() const
{
    OString sContent;
    if (!ReadUtf8File(m_sProvenanceUrl, sContent) || sContent.isEmpty())
        return {};

    const OUString sUtf16 = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    std::vector<AIChatSourceProvenanceEntry> aEntries;

    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sLine = sUtf16.getToken(0, '\n', nIndex);
        if (sLine.isEmpty())
            continue;

        AIChatSourceProvenanceEntry aEntry;
        if (ParseProvenanceLine(sLine, aEntry))
            aEntries.push_back(aEntry);
    }

    return aEntries;
}

OUString AIChatSourceProvenance::MakeSourceId(const OUString& rObjectId)
{
    return u"source:"_ustr + rObjectId;
}

OUString AIChatSourceProvenance::MakeCitationId(const OUString& rObjectId)
{
    return u"citation:"_ustr + rObjectId;
}

OUString AIChatSourceProvenance::MakeLocalEvidenceId(const OUString& rObjectId)
{
    return u"evidence:local-materialized:"_ustr + rObjectId;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
