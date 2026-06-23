/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace content objects).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatContentObjectStore.hxx"

#include "AIChatContentRegistry.hxx"
#include "AIChatSourceProvenance.hxx"

#include <comphelper/hash.hxx>
#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <unotools/pathoptions.hxx>

#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral CONTENT_OBJECT_DIR_NAME = u"kqoffice-v3-ai-chat-objects";
constexpr OUStringLiteral CONTENT_OBJECT_SUFFIX = u".content";
constexpr sal_Int32 LARGE_TEXT_THRESHOLD = 2000;
constexpr sal_Int32 STRUCTURED_TEXT_THRESHOLD = 40;

OUString EnsureNoTrailingSlash(OUString sUrl)
{
    while (sUrl.endsWith(u"/"))
        sUrl = sUrl.copy(0, sUrl.getLength() - 1);
    return sUrl;
}

bool LooksStructured(const OUString& rText)
{
    return rText.indexOf(u'<') >= 0 || rText.indexOf(u'\t') >= 0 || rText.indexOf(u'|') >= 0
           || rText.indexOf(u"\n#"_ustr) >= 0 || rText.indexOf(u"\n-"_ustr) >= 0;
}

OUString MakeContentHash(const OUString& rText)
{
    const OString sText = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    const std::vector<unsigned char> aHash = comphelper::Hash::calculateHash(
        sText.getStr(), sText.getLength(), comphelper::HashType::SHA256);
    return OUString::createFromAscii(comphelper::hashToString(aHash));
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
}

AIChatContentObjectStore::AIChatContentObjectStore()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + CONTENT_OBJECT_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
}

bool AIChatContentObjectStore::ShouldMaterializeText(const OUString& rText) const
{
    return rText.getLength() >= LARGE_TEXT_THRESHOLD
           || (rText.getLength() >= STRUCTURED_TEXT_THRESHOLD && LooksStructured(rText));
}

AIChatMaterializedContent AIChatContentObjectStore::MaterializeText(const OUString& rText) const
{
    AIChatMaterializedContent aContent;
    aContent.Type = LooksStructured(rText) ? AIChatContentObjectType::StructuredText
                                           : AIChatContentObjectType::PlainTextLarge;
    aContent.ObjectId = MakeContentHash(rText);
    aContent.Reference = u"@artifact:"_ustr + aContent.ObjectId;
    aContent.SidecarUrl = m_sStorageRootUrl + u"/"_ustr + aContent.ObjectId + CONTENT_OBJECT_SUFFIX;
    WriteUtf8File(aContent.SidecarUrl, rText);

    AIChatContentRegistry aRegistry;
    AIChatContentRegistryEntry aEntry;
    aEntry.ObjectId = aContent.ObjectId;
    aEntry.Type = DetectTypeLabel(aContent.Type);
    aEntry.SourceSurface = u"chat-composer"_ustr;
    aEntry.State = u"registered"_ustr;
    aEntry.EvidenceId = AIChatSourceProvenance::MakeLocalEvidenceId(aContent.ObjectId);
    aEntry.HashReference = aContent.Reference;
    aEntry.OpenTarget = u"sidebar-preview"_ustr;
    aEntry.PreviewMode = u"metadata-summary"_ustr;
    aRegistry.RegisterObject(aEntry);

    AIChatSourceProvenance aProvenance;
    AIChatSourceProvenanceEntry aSource;
    aSource.SourceId = AIChatSourceProvenance::MakeSourceId(aContent.ObjectId);
    aSource.SourceType = u"content-suggestion"_ustr;
    aSource.CitationId = AIChatSourceProvenance::MakeCitationId(aContent.ObjectId);
    aSource.EvidenceId = aEntry.EvidenceId;
    aSource.HashReference = aContent.Reference;
    aSource.SourceSurface = u"composer"_ustr;
    aSource.OpenTarget = aEntry.OpenTarget;
    aSource.SpanReference = u"span:whole-object"_ustr;
    aSource.ReviewId = u"review:pending"_ustr;
    aProvenance.RegisterSource(aSource);

    return aContent;
}

OUString AIChatContentObjectStore::DetectTypeLabel(AIChatContentObjectType eType)
{
    switch (eType)
    {
        case AIChatContentObjectType::PlainTextLarge:
            return u"plain-text-large"_ustr;
        case AIChatContentObjectType::StructuredText:
            return u"structured-text"_ustr;
    }
    return u"plain-text-large"_ustr;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
