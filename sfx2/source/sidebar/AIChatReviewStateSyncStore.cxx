/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: review state sync).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

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
constexpr OUStringLiteral REVIEW_STATE_SYNC_DIR_NAME = u"kqoffice-v3-ai-review-state-sync";
constexpr OUStringLiteral REVIEW_STATE_SYNC_FILE_NAME = u"state.tsv";
constexpr sal_uInt64 MAX_REVIEW_STATE_SYNC_BYTES = 1024 * 1024;

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
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_REVIEW_STATE_SYNC_BYTES)
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

bool ParseStateLine(const OUString& rLine, AIChatReviewStateSyncEntry& rEntry)
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

    rEntry.ReviewId = aFields[0];
    rEntry.State = aFields[1];
    rEntry.TransitionEvent = aFields[2];
    rEntry.SourceSurface = aFields[3];
    rEntry.EvidenceId = aFields[4];
    rEntry.HashReference = aFields[5];
    rEntry.OpenTarget = aFields[6];
    rEntry.PreviewMode = aFields[7];
    rEntry.VisibleState = aFields[8];
    rEntry.Conflict = false;
    return true;
}

bool HasEvidenceConflict(const AIChatReviewStateSyncEntry& rExisting,
                         const AIChatReviewStateSyncEntry& rNext)
{
    return (!rExisting.EvidenceId.isEmpty() && !rNext.EvidenceId.isEmpty()
            && rExisting.EvidenceId != rNext.EvidenceId)
           || (!rExisting.HashReference.isEmpty() && !rNext.HashReference.isEmpty()
               && rExisting.HashReference != rNext.HashReference);
}
}

AIChatReviewStateSyncStore::AIChatReviewStateSyncStore()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + REVIEW_STATE_SYNC_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sStateUrl = m_sStorageRootUrl + u"/"_ustr + REVIEW_STATE_SYNC_FILE_NAME;
}

bool AIChatReviewStateSyncStore::IsValidState(const OUString& rState)
{
    return rState == u"queued"_ustr || rState == u"open"_ustr || rState == u"approved"_ustr
           || rState == u"rejected"_ustr || rState == u"applied"_ustr
           || rState == u"failed"_ustr;
}

bool AIChatReviewStateSyncStore::IsValidTransitionEvent(const OUString& rTransitionEvent)
{
    return rTransitionEvent == u"open"_ustr || rTransitionEvent == u"approve"_ustr
           || rTransitionEvent == u"reject"_ustr || rTransitionEvent == u"apply"_ustr
           || rTransitionEvent == u"fail"_ustr;
}

bool AIChatReviewStateSyncStore::IsSyncedSurface(const OUString& rSurface)
{
    return rSurface == u"review-queue"_ustr || rSurface == u"diff-review"_ustr
           || rSurface == u"preview-matrix"_ustr || rSurface == u"evidence-inspector"_ustr
           || rSurface == u"task-progress"_ustr || rSurface == u"action-bar"_ustr;
}

OUString AIChatReviewStateSyncStore::NormalizeRegistryState(const OUString& rState)
{
    if (rState == u"in-review"_ustr)
        return u"queued"_ustr;
    if (IsValidState(rState))
        return rState;
    return u"queued"_ustr;
}

OUString AIChatReviewStateSyncStore::TransitionForState(const OUString& rState)
{
    if (rState == u"open"_ustr)
        return u"open"_ustr;
    if (rState == u"approved"_ustr)
        return u"approve"_ustr;
    if (rState == u"rejected"_ustr)
        return u"reject"_ustr;
    if (rState == u"applied"_ustr)
        return u"apply"_ustr;
    if (rState == u"failed"_ustr)
        return u"fail"_ustr;
    return u"open"_ustr;
}

OUString AIChatReviewStateSyncStore::BuildVisibleState(const AIChatReviewStateSyncEntry& rEntry)
{
    return u"review-state-sync review-id="_ustr + rEntry.ReviewId + u" state="_ustr
           + rEntry.State + u" transition="_ustr + rEntry.TransitionEvent
           + u" source="_ustr + rEntry.SourceSurface + u" evidence-id="_ustr
           + rEntry.EvidenceId + u" hash="_ustr + rEntry.HashReference
           + u" visible-state=true metadata-only=true hash-only=true redacted=true"_ustr
           + u" review-queue=true diff-review=true preview-matrix=true"_ustr
           + u" evidence-inspector=true task-progress=true action-bar=true"_ustr
           + u" requires-human-approval=true main-document-mutation=false auto-apply=false"_ustr;
}

AIChatReviewStateSyncResult AIChatReviewStateSyncStore::RecordTransition(
    const OUString& rReviewId, const OUString& rTransitionEvent, const OUString& rState,
    const OUString& rSourceSurface, const OUString& rEvidenceId, const OUString& rHashReference,
    const OUString& rOpenTarget, const OUString& rPreviewMode) const
{
    AIChatReviewStateSyncResult aResult;
    aResult.Entry.ReviewId = rReviewId;
    aResult.Entry.State = rState;
    aResult.Entry.TransitionEvent = rTransitionEvent;
    aResult.Entry.SourceSurface = rSourceSurface;
    aResult.Entry.EvidenceId = rEvidenceId;
    aResult.Entry.HashReference = rHashReference;
    aResult.Entry.OpenTarget = rOpenTarget;
    aResult.Entry.PreviewMode = rPreviewMode;

    if (rReviewId.isEmpty())
    {
        aResult.Message = u"review-state-sync-failed reason=missing-review-id"_ustr;
        return aResult;
    }
    if (!IsValidState(rState))
    {
        aResult.Message = u"review-state-sync-failed reason=invalid-state state="_ustr + rState;
        return aResult;
    }
    if (!IsValidTransitionEvent(rTransitionEvent))
    {
        aResult.Message
            = u"review-state-sync-failed reason=invalid-transition transition="_ustr
              + rTransitionEvent;
        return aResult;
    }
    if (!IsSyncedSurface(rSourceSurface))
    {
        aResult.Message = u"review-state-sync-failed reason=unsupported-surface surface="_ustr
                          + rSourceSurface;
        return aResult;
    }
    if (rEvidenceId.isEmpty() || rHashReference.isEmpty())
    {
        aResult.Message = u"review-state-sync-failed reason=missing-evidence-link review-id="_ustr
                          + rReviewId;
        return aResult;
    }

    const AIChatReviewStateSyncResult aLatest = GetLatestState(rReviewId);
    if (aLatest.Success && HasEvidenceConflict(aLatest.Entry, aResult.Entry))
    {
        aResult.Entry.State = u"failed"_ustr;
        aResult.Entry.TransitionEvent = u"fail"_ustr;
        aResult.Entry.SourceSurface = u"review-queue"_ustr;
        aResult.Entry.OpenTarget = u"review-queue"_ustr;
        aResult.Entry.PreviewMode = u"metadata-summary"_ustr;
        aResult.Entry.Conflict = true;
        aResult.Entry.VisibleState = BuildVisibleState(aResult.Entry)
                                     + u" conflict=true conflict-behavior=fail-closed-user-visible"_ustr;
        const OUString sConflictLine
            = EscapeField(aResult.Entry.ReviewId) + u"\t"_ustr + EscapeField(aResult.Entry.State)
              + u"\t"_ustr + EscapeField(aResult.Entry.TransitionEvent) + u"\t"_ustr
              + EscapeField(aResult.Entry.SourceSurface) + u"\t"_ustr
              + EscapeField(aResult.Entry.EvidenceId) + u"\t"_ustr
              + EscapeField(aResult.Entry.HashReference) + u"\t"_ustr
              + EscapeField(aResult.Entry.OpenTarget) + u"\t"_ustr
              + EscapeField(aResult.Entry.PreviewMode) + u"\t"_ustr
              + EscapeField(aResult.Entry.VisibleState) + u"\n"_ustr;
        AppendUtf8Line(m_sStateUrl, sConflictLine);
        aResult.Message = u"review-state-sync-failed reason=conflict review-id="_ustr + rReviewId
                          + u" conflict-behavior=fail-closed-user-visible visible-state=true"_ustr;
        return aResult;
    }

    aResult.Entry.VisibleState = BuildVisibleState(aResult.Entry);
    const OUString sLine
        = EscapeField(aResult.Entry.ReviewId) + u"\t"_ustr + EscapeField(aResult.Entry.State)
          + u"\t"_ustr + EscapeField(aResult.Entry.TransitionEvent) + u"\t"_ustr
          + EscapeField(aResult.Entry.SourceSurface) + u"\t"_ustr
          + EscapeField(aResult.Entry.EvidenceId) + u"\t"_ustr
          + EscapeField(aResult.Entry.HashReference) + u"\t"_ustr
          + EscapeField(aResult.Entry.OpenTarget) + u"\t"_ustr
          + EscapeField(aResult.Entry.PreviewMode) + u"\t"_ustr
          + EscapeField(aResult.Entry.VisibleState) + u"\n"_ustr;
    if (!AppendUtf8Line(m_sStateUrl, sLine))
    {
        aResult.Message = u"review-state-sync-failed reason=store-write-failed review-id="_ustr
                          + rReviewId;
        return aResult;
    }

    aResult.Success = true;
    aResult.Message = aResult.Entry.VisibleState;
    return aResult;
}

AIChatReviewStateSyncResult
AIChatReviewStateSyncStore::RecordFromRegistry(const AIChatContentRegistryEntry& rEntry,
                                               const OUString& rTransitionEvent,
                                               const OUString& rState,
                                               const OUString& rSourceSurface) const
{
    return RecordTransition(rEntry.ObjectId, rTransitionEvent, NormalizeRegistryState(rState),
                            rSourceSurface, rEntry.EvidenceId, rEntry.HashReference,
                            rEntry.OpenTarget, rEntry.PreviewMode);
}

std::vector<AIChatReviewStateSyncEntry> AIChatReviewStateSyncStore::LoadEntries() const
{
    OString sContent;
    if (!ReadUtf8File(m_sStateUrl, sContent) || sContent.isEmpty())
        return {};

    const OUString sUtf16 = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    std::vector<AIChatReviewStateSyncEntry> aEntries;

    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sLine = sUtf16.getToken(0, '\n', nIndex);
        if (sLine.isEmpty())
            continue;

        AIChatReviewStateSyncEntry aEntry;
        if (ParseStateLine(sLine, aEntry) && IsValidState(aEntry.State)
            && IsValidTransitionEvent(aEntry.TransitionEvent)
            && IsSyncedSurface(aEntry.SourceSurface))
        {
            aEntries.push_back(aEntry);
        }
    }

    return aEntries;
}

AIChatReviewStateSyncResult
AIChatReviewStateSyncStore::GetLatestState(const OUString& rReviewId) const
{
    AIChatReviewStateSyncResult aResult;
    if (rReviewId.isEmpty())
    {
        aResult.Message = u"review-state-sync-failed reason=missing-review-id"_ustr;
        return aResult;
    }

    const std::vector<AIChatReviewStateSyncEntry> aEntries = LoadEntries();
    for (auto it = aEntries.rbegin(); it != aEntries.rend(); ++it)
    {
        if (it->ReviewId == rReviewId)
        {
            aResult.Success = true;
            aResult.Entry = *it;
            aResult.Message = it->VisibleState;
            return aResult;
        }
    }

    aResult.Message = u"review-state-sync-missing review-id="_ustr + rReviewId;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
