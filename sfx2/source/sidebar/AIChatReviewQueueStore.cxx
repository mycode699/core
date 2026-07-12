/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: review queue runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatReviewQueueStore.hxx"

#include "AIChatReviewStateSyncStore.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <unotools/pathoptions.hxx>

#include <algorithm>
#include <string_view>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral REVIEW_QUEUE_DIR_NAME = u"kqoffice-v3-ai-review-queue";
constexpr OUStringLiteral REVIEW_QUEUE_FILE_NAME = u"queue.tsv";
constexpr sal_uInt64 MAX_REVIEW_QUEUE_BYTES = 1024 * 1024;

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
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_REVIEW_QUEUE_BYTES)
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

bool ParseQueueLine(const OUString& rLine, AIChatReviewQueueEntry& rEntry)
{
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 8 || aFields[0].isEmpty())
        return false;

    rEntry.ReviewId = aFields[0];
    rEntry.ItemType = aFields[1];
    rEntry.State = aFields[2];
    rEntry.SourceSurface = aFields[3];
    rEntry.EvidenceId = aFields[4];
    rEntry.HashReference = aFields[5];
    rEntry.OpenTarget = aFields[6];
    rEntry.PreviewMode = aFields[7];
    return true;
}

OUString StateFromRegistryState(const OUString& rState)
{
    if (rState == u"in-review"_ustr)
        return u"queued"_ustr;
    if (AIChatReviewQueueStore::IsValidState(rState))
        return rState;
    return u"queued"_ustr;
}
}

AIChatReviewQueueStore::AIChatReviewQueueStore()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + REVIEW_QUEUE_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sQueueUrl = m_sStorageRootUrl + u"/"_ustr + REVIEW_QUEUE_FILE_NAME;
}

bool AIChatReviewQueueStore::IsReviewQueueEntry(const AIChatContentRegistryEntry& rEntry)
{
    return ResolveItemType(rEntry) != u"unsupported"_ustr;
}

OUString AIChatReviewQueueStore::ResolveItemType(const AIChatContentRegistryEntry& rEntry)
{
    if (rEntry.Type == u"review-item"_ustr || rEntry.SourceSurface == u"content-review"_ustr
        || rEntry.Type == u"assistant-output"_ustr)
        return u"content-review"_ustr;
    if (rEntry.Type == u"formatting-preview"_ustr
        || rEntry.SourceSurface == u"formatting-review"_ustr)
        return u"formatting-review"_ustr;
    if (rEntry.Type == u"task-step"_ustr)
        return u"task-step"_ustr;
    if (rEntry.Type == u"apply-plan"_ustr || rEntry.SourceSurface == u"apply-plan"_ustr)
        return u"apply-plan"_ustr;
    return u"unsupported"_ustr;
}

bool AIChatReviewQueueStore::IsValidState(const OUString& rState)
{
    return rState == u"queued"_ustr || rState == u"open"_ustr || rState == u"approved"_ustr
           || rState == u"rejected"_ustr || rState == u"applied"_ustr
           || rState == u"failed"_ustr;
}

bool AIChatReviewQueueStore::IsBulkActionAllowed(const OUString& rAction)
{
    return rAction == u"approve-selected"_ustr || rAction == u"reject-selected"_ustr;
}

bool AIChatReviewQueueStore::EnqueueFromRegistry(const AIChatContentRegistryEntry& rEntry) const
{
    if (!IsReviewQueueEntry(rEntry) || rEntry.ObjectId.isEmpty() || rEntry.EvidenceId.isEmpty())
        return false;

    AIChatReviewStateSyncStore aStateSync;
    const OUString sState = StateFromRegistryState(rEntry.State);
    aStateSync.RecordFromRegistry(rEntry, AIChatReviewStateSyncStore::TransitionForState(sState),
                                  sState, u"review-queue"_ustr);

    const OUString sLine
        = EscapeField(rEntry.ObjectId) + u"\t"_ustr + EscapeField(ResolveItemType(rEntry))
          + u"\t"_ustr + EscapeField(sState) + u"\t"_ustr
          + EscapeField(rEntry.SourceSurface) + u"\t"_ustr + EscapeField(rEntry.EvidenceId)
          + u"\t"_ustr + EscapeField(rEntry.HashReference) + u"\t"_ustr
          + EscapeField(rEntry.OpenTarget) + u"\t"_ustr + EscapeField(rEntry.PreviewMode)
          + u"\n"_ustr;
    return AppendUtf8Line(m_sQueueUrl, sLine);
}

bool AIChatReviewQueueStore::TransitionState(const OUString& rReviewId,
                                             const OUString& rNewState) const
{
    if (rReviewId.isEmpty() || !IsValidState(rNewState))
        return false;

    const std::vector<AIChatReviewQueueEntry> aEntries = LoadEntries();
    const auto it = std::find_if(aEntries.begin(), aEntries.end(),
                                 [&rReviewId](const AIChatReviewQueueEntry& rEntry) {
                                     return rEntry.ReviewId == rReviewId;
                                 });
    if (it != aEntries.end())
        return TransitionState(*it, rNewState,
                               AIChatReviewStateSyncStore::TransitionForState(rNewState));

    return false;
}

bool AIChatReviewQueueStore::TransitionState(const AIChatReviewQueueEntry& rEntry,
                                             const OUString& rNewState,
                                             const OUString& rTransitionEvent) const
{
    if (rEntry.ReviewId.isEmpty() || !IsValidState(rNewState)
        || !AIChatReviewStateSyncStore::IsValidTransitionEvent(rTransitionEvent)
        || rEntry.EvidenceId.isEmpty() || rEntry.HashReference.isEmpty())
    {
        return false;
    }

    AIChatReviewStateSyncStore aStateSync;
    const AIChatReviewStateSyncResult aSync = aStateSync.RecordTransition(
        rEntry.ReviewId, rTransitionEvent, rNewState, u"review-queue"_ustr, rEntry.EvidenceId,
        rEntry.HashReference,
        rEntry.OpenTarget.isEmpty() ? u"diff-review"_ustr : rEntry.OpenTarget,
        rEntry.PreviewMode.isEmpty() ? u"diff-preview"_ustr : rEntry.PreviewMode);
    if (!aSync.Success)
        return false;

    const OUString sLine
        = EscapeField(rEntry.ReviewId) + u"\t"_ustr + EscapeField(rEntry.ItemType) + u"\t"_ustr
          + EscapeField(rNewState) + u"\t"_ustr + EscapeField(u"review-queue"_ustr)
          + u"\t"_ustr + EscapeField(rEntry.EvidenceId) + u"\t"_ustr
          + EscapeField(rEntry.HashReference) + u"\t"_ustr
          + EscapeField(rEntry.OpenTarget.isEmpty() ? u"diff-review"_ustr : rEntry.OpenTarget)
          + u"\t"_ustr
          + EscapeField(rEntry.PreviewMode.isEmpty() ? u"diff-preview"_ustr
                                                     : rEntry.PreviewMode)
          + u"\n"_ustr;
    return AppendUtf8Line(m_sQueueUrl, sLine);
}

std::vector<AIChatReviewQueueEntry> AIChatReviewQueueStore::LoadEntries() const
{
    OString sContent;
    if (!ReadUtf8File(m_sQueueUrl, sContent) || sContent.isEmpty())
        return {};

    const OUString sUtf16 = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    std::vector<AIChatReviewQueueEntry> aOrderedEntries;

    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sLine = sUtf16.getToken(0, '\n', nIndex);
        if (sLine.isEmpty())
            continue;

        AIChatReviewQueueEntry aEntry;
        if (!ParseQueueLine(sLine, aEntry) || !IsValidState(aEntry.State))
            continue;

        auto it = std::find_if(aOrderedEntries.begin(), aOrderedEntries.end(),
                               [&aEntry](const AIChatReviewQueueEntry& rExisting) {
                                   return rExisting.ReviewId == aEntry.ReviewId;
                               });
        if (it == aOrderedEntries.end())
            aOrderedEntries.push_back(aEntry);
        else
            *it = aEntry;
    }

    std::reverse(aOrderedEntries.begin(), aOrderedEntries.end());
    return aOrderedEntries;
}

std::vector<AIChatReviewQueueEntry>
AIChatReviewQueueStore::FilterEntries(const AIChatReviewQueueFilter& rFilter) const
{
    std::vector<AIChatReviewQueueEntry> aEntries = LoadEntries();
    std::vector<AIChatReviewQueueEntry> aFiltered;
    for (const auto& rEntry : aEntries)
    {
        if (!rFilter.State.isEmpty() && rEntry.State != rFilter.State)
            continue;
        if (!rFilter.ItemType.isEmpty() && rEntry.ItemType != rFilter.ItemType)
            continue;
        if (!rFilter.Surface.isEmpty() && rEntry.SourceSurface != rFilter.Surface)
            continue;
        aFiltered.push_back(rEntry);
    }
    return aFiltered;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
