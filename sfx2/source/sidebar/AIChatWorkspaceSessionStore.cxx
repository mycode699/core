/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: workspace session state).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatWorkspaceSessionStore.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <unotools/pathoptions.hxx>

#include <ctime>
#include <string_view>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral WORKSPACE_SESSION_DIR_NAME = u"kqoffice-v3-ai-workspace-session";
constexpr OUStringLiteral TIMELINE_FILE_SUFFIX = u".timeline.tsv";
constexpr OUStringLiteral SNAPSHOT_FILE_SUFFIX = u".snapshot.tsv";
constexpr sal_uInt64 MAX_SNAPSHOT_BYTES = 128 * 1024;

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

bool WriteUtf8File(const OUString& rUrl, const OUString& rText)
{
    const OString sUtf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    osl::File aFile(rUrl);
    osl::FileBase::RC eError = aFile.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (eError != osl::FileBase::E_None)
        eError = aFile.open(osl_File_OpenFlag_Write);
    if (eError != osl::FileBase::E_None)
        return false;

    aFile.setSize(0);
    sal_uInt64 nWritten = 0;
    const bool bWritten
        = aFile.write(sUtf8.getStr(), sUtf8.getLength(), nWritten) == osl::FileBase::E_None
          && nWritten == static_cast<sal_uInt64>(sUtf8.getLength());
    aFile.close();
    return bWritten;
}

bool AppendUtf8Line(const OUString& rUrl, const OUString& rLine)
{
    osl::File aFile(rUrl);
    osl::FileBase::RC eError = aFile.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (eError != osl::FileBase::E_None)
        eError = aFile.open(osl_File_OpenFlag_Write);
    if (eError != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    aFile.getSize(nSize);
    aFile.setPos(osl_Pos_Absolut, nSize);

    const OString sUtf8 = OUStringToOString(rLine, RTL_TEXTENCODING_UTF8);
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
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_SNAPSHOT_BYTES)
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
}

AIChatWorkspaceSessionStore::AIChatWorkspaceSessionStore(const OUString& rDocumentBinding)
    : m_sDocumentBinding(rDocumentBinding)
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + WORKSPACE_SESSION_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sTimelineUrl = m_sStorageRootUrl + u"/"_ustr + m_sDocumentBinding + TIMELINE_FILE_SUFFIX;
    m_sSnapshotUrl = m_sStorageRootUrl + u"/"_ustr + m_sDocumentBinding + SNAPSHOT_FILE_SUFFIX;
}

bool AIChatWorkspaceSessionStore::RecordActivity(
    const AIChatWorkspaceActivityEntry& rEntry) const
{
    const OUString sLine
        = EscapeField(rEntry.Timestamp) + u"\t"_ustr + EscapeField(rEntry.Actor) + u"\t"_ustr
          + EscapeField(rEntry.Event) + u"\t"_ustr + EscapeField(rEntry.Surface) + u"\t"_ustr
          + EscapeField(rEntry.ArtifactId) + u"\t"_ustr + EscapeField(rEntry.ReviewId)
          + u"\t"_ustr + EscapeField(rEntry.EvidenceId) + u"\t"_ustr
          + EscapeField(rEntry.HashReference) + u"\t"_ustr + EscapeField(rEntry.OpenTarget)
          + u"\n"_ustr;
    return AppendUtf8Line(m_sTimelineUrl, sLine);
}

bool AIChatWorkspaceSessionStore::SaveSnapshot(const AIChatSessionSnapshot& rSnapshot) const
{
    const OUString sLine
        = EscapeField(rSnapshot.DocumentBinding) + u"\t"_ustr + EscapeField(rSnapshot.Timestamp)
          + u"\t"_ustr + EscapeField(rSnapshot.ActiveTaskId) + u"\t"_ustr
          + EscapeField(rSnapshot.OpenArtifactId) + u"\t"_ustr
          + EscapeField(rSnapshot.OpenReviewId) + u"\t"_ustr
          + EscapeField(rSnapshot.ActiveEvidenceId) + u"\t"_ustr
          + EscapeField(rSnapshot.PreviewMode) + u"\t"_ustr + EscapeField(rSnapshot.ReviewState)
          + u"\t"_ustr + EscapeField(rSnapshot.ActivityCursor) + u"\t"_ustr
          + EscapeField(rSnapshot.FailureState) + u"\t"_ustr
          + EscapeField(rSnapshot.HashReference) + u"\n"_ustr;
    return WriteUtf8File(m_sSnapshotUrl, sLine);
}

AIChatSessionSnapshot AIChatWorkspaceSessionStore::LoadSnapshot() const
{
    AIChatSessionSnapshot aSnapshot;
    OString sContent;
    if (!ReadUtf8File(m_sSnapshotUrl, sContent) || sContent.isEmpty())
        return aSnapshot;

    const OUString sLine = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8).getToken(0, '\n');
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = sLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 11 || aFields[0] != m_sDocumentBinding)
        return AIChatSessionSnapshot();

    aSnapshot.DocumentBinding = aFields[0];
    aSnapshot.Timestamp = aFields[1];
    aSnapshot.ActiveTaskId = aFields[2];
    aSnapshot.OpenArtifactId = aFields[3];
    aSnapshot.OpenReviewId = aFields[4];
    aSnapshot.ActiveEvidenceId = aFields[5];
    aSnapshot.PreviewMode = aFields[6];
    aSnapshot.ReviewState = aFields[7];
    aSnapshot.ActivityCursor = aFields[8];
    aSnapshot.FailureState = aFields[9];
    aSnapshot.HashReference = aFields[10];
    return aSnapshot;
}

OUString AIChatWorkspaceSessionStore::MakeTimestamp()
{
    return OUString::number(static_cast<sal_Int64>(std::time(nullptr)));
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
