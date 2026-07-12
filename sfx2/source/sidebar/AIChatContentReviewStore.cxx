/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: content review runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatContentReviewStore.hxx"

#include "AIChatSourceProvenance.hxx"
#include "AIChatReviewQueueStore.hxx"

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
constexpr OUStringLiteral CONTENT_REVIEW_DIR_NAME = u"kqoffice-v3-ai-content-review";
constexpr OUStringLiteral CONTENT_REVIEW_FILE_NAME = u"reviews.tsv";
constexpr sal_uInt64 MAX_REVIEW_STORE_BYTES = 1024 * 1024;

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
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_REVIEW_STORE_BYTES)
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

bool ParseReviewLine(const OUString& rLine, AIChatContentReviewEntry& rEntry)
{
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 11 || aFields[0].isEmpty())
        return false;

    rEntry.ReviewId = aFields[0];
    rEntry.SourceObjectId = aFields[1];
    rEntry.SourceType = aFields[2];
    rEntry.State = aFields[3];
    rEntry.ReviewMode = aFields[4];
    rEntry.EvidenceId = aFields[5];
    rEntry.HashReference = aFields[6];
    rEntry.OpenTarget = aFields[7];
    rEntry.PreviewMode = aFields[8];
    rEntry.RequiresHumanApproval = aFields[9] == u"true"_ustr;
    rEntry.MainDocumentMutationAllowed = aFields[10] == u"true"_ustr;
    return true;
}

OUString BoolToField(bool bValue) { return bValue ? u"true"_ustr : u"false"_ustr; }
}

AIChatContentReviewStore::AIChatContentReviewStore()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + CONTENT_REVIEW_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sReviewStoreUrl = m_sStorageRootUrl + u"/"_ustr + CONTENT_REVIEW_FILE_NAME;
}

bool AIChatContentReviewStore::IsSupportedSourceType(const OUString& rSourceType)
{
    return rSourceType == u"selection"_ustr || rSourceType == u"document-section"_ustr
           || rSourceType == u"connector-result"_ustr
           || rSourceType == u"knowledge-index-result"_ustr
           || rSourceType == u"evidence-record"_ustr || rSourceType == u"task-step"_ustr
           || rSourceType == u"assistant-output"_ustr || rSourceType == u"apply-plan"_ustr;
}

OUString AIChatContentReviewStore::MakeReviewId(const OUString& rSourceObjectId)
{
    return u"review:"_ustr + rSourceObjectId;
}

AIChatContentReviewCreateResult AIChatContentReviewStore::CreateReviewFromSource(
    const AIChatContentRegistryEntry& rSource) const
{
    AIChatContentReviewCreateResult aResult;

    if (rSource.ObjectId.isEmpty())
    {
        aResult.Message = u"review-create-failed reason=missing-source-object-id"_ustr;
        return aResult;
    }
    if (!IsSupportedSourceType(rSource.Type))
    {
        aResult.Message = u"review-create-failed reason=unsupported-source-type type="_ustr
                          + rSource.Type;
        return aResult;
    }
    if (rSource.EvidenceId.isEmpty())
    {
        aResult.Message = u"review-create-failed reason=missing-evidence-link source-id="_ustr
                          + rSource.ObjectId;
        return aResult;
    }
    if (rSource.HashReference.isEmpty())
    {
        aResult.Message = u"review-create-failed reason=missing-hash-reference source-id="_ustr
                          + rSource.ObjectId;
        return aResult;
    }

    aResult.Review.ReviewId = MakeReviewId(rSource.ObjectId);
    aResult.Review.SourceObjectId = rSource.ObjectId;
    aResult.Review.SourceType = rSource.Type;
    aResult.Review.State = u"queued"_ustr;
    aResult.Review.ReviewMode = u"evidence-linked-content-diff"_ustr;
    aResult.Review.EvidenceId = rSource.EvidenceId;
    aResult.Review.HashReference = rSource.HashReference;
    aResult.Review.OpenTarget = u"diff-review"_ustr;
    aResult.Review.PreviewMode = u"diff-preview"_ustr;
    aResult.Review.RequiresHumanApproval = true;
    aResult.Review.MainDocumentMutationAllowed = false;

    const OUString sLine
        = EscapeField(aResult.Review.ReviewId) + u"\t"_ustr
          + EscapeField(aResult.Review.SourceObjectId) + u"\t"_ustr
          + EscapeField(aResult.Review.SourceType) + u"\t"_ustr
          + EscapeField(aResult.Review.State) + u"\t"_ustr
          + EscapeField(aResult.Review.ReviewMode) + u"\t"_ustr
          + EscapeField(aResult.Review.EvidenceId) + u"\t"_ustr
          + EscapeField(aResult.Review.HashReference) + u"\t"_ustr
          + EscapeField(aResult.Review.OpenTarget) + u"\t"_ustr
          + EscapeField(aResult.Review.PreviewMode) + u"\t"_ustr
          + BoolToField(aResult.Review.RequiresHumanApproval) + u"\t"_ustr
          + BoolToField(aResult.Review.MainDocumentMutationAllowed) + u"\n"_ustr;
    if (!AppendUtf8Line(m_sReviewStoreUrl, sLine))
    {
        aResult.Message = u"review-create-failed reason=store-write-failed source-id="_ustr
                          + rSource.ObjectId;
        return aResult;
    }

    aResult.RegistryEntry.ObjectId = aResult.Review.ReviewId;
    aResult.RegistryEntry.Type = u"review-item"_ustr;
    aResult.RegistryEntry.SourceSurface = u"content-review"_ustr;
    aResult.RegistryEntry.State = u"in-review"_ustr;
    aResult.RegistryEntry.EvidenceId = aResult.Review.EvidenceId;
    aResult.RegistryEntry.HashReference = aResult.Review.HashReference;
    aResult.RegistryEntry.OpenTarget = aResult.Review.OpenTarget;
    aResult.RegistryEntry.PreviewMode = aResult.Review.PreviewMode;

    AIChatContentRegistry aRegistry;
    aRegistry.RegisterObject(aResult.RegistryEntry);

    AIChatReviewQueueStore aQueue;
    aQueue.EnqueueFromRegistry(aResult.RegistryEntry);

    AIChatSourceProvenance aProvenance;
    AIChatSourceProvenanceEntry aSource;
    aSource.SourceId = AIChatSourceProvenance::MakeSourceId(aResult.Review.ReviewId);
    aSource.SourceType = u"review-item"_ustr;
    aSource.CitationId = AIChatSourceProvenance::MakeCitationId(aResult.Review.ReviewId);
    aSource.EvidenceId = aResult.Review.EvidenceId;
    aSource.HashReference = aResult.Review.HashReference;
    aSource.SourceSurface = u"content-review"_ustr;
    aSource.OpenTarget = aResult.Review.OpenTarget;
    aSource.SpanReference = u"span:source-object:"_ustr + rSource.ObjectId;
    aSource.ReviewId = aResult.Review.ReviewId;
    aProvenance.RegisterSource(aSource);

    aResult.Success = true;
    aResult.Message = u"content-review-created review-id="_ustr + aResult.Review.ReviewId
                      + u" source-id="_ustr + rSource.ObjectId
                      + u" review-mode=evidence-linked-content-diff uses-diff-review=true"_ustr
                      + u" open-target=diff-review preview-mode=diff-preview"_ustr
                      + u" requires-human-approval=true main-document-mutation=false"_ustr;
    return aResult;
}

std::vector<AIChatContentReviewEntry> AIChatContentReviewStore::LoadEntries() const
{
    OString sContent;
    if (!ReadUtf8File(m_sReviewStoreUrl, sContent) || sContent.isEmpty())
        return {};

    const OUString sUtf16 = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    std::vector<AIChatContentReviewEntry> aEntries;

    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sLine = sUtf16.getToken(0, '\n', nIndex);
        if (sLine.isEmpty())
            continue;

        AIChatContentReviewEntry aEntry;
        if (ParseReviewLine(sLine, aEntry))
            aEntries.push_back(aEntry);
    }

    return aEntries;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
