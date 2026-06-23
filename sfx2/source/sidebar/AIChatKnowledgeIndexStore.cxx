/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge Index sidecar storage).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatKnowledgeIndexStore.hxx"

#include <sfx2/docfile.hxx>
#include <sfx2/objsh.hxx>
#include <comphelper/hash.hxx>
#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <unotools/pathoptions.hxx>
#include <tools/urlobj.hxx>

#include <string_view>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral KNOWLEDGE_INDEX_DIR_NAME = u"kqoffice-v3-knowledge-index";
constexpr OUStringLiteral KNOWLEDGE_INDEX_CHUNK_FILE_NAME = u"chunks.tsv";
constexpr sal_uInt64 MAX_KNOWLEDGE_INDEX_BYTES = 2 * 1024 * 1024;

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

bool ReadUtf8File(const OUString& rUrl, OString& rContent)
{
    osl::File aFile(rUrl);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_KNOWLEDGE_INDEX_BYTES)
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

bool IsLowerHex64(const OUString& rValue)
{
    if (rValue.getLength() != 64)
        return false;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    }
    return true;
}

bool ParseChunkLine(const OUString& rLine, AIChatKnowledgeIndexChunk& rChunk)
{
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 16 || aFields[0].isEmpty())
        return false;

    rChunk.ChunkId = aFields[0];
    rChunk.WorkspaceHash = aFields[1];
    rChunk.SourceKind = aFields[2];
    rChunk.SourceUriHash = aFields[3];
    rChunk.SourceId = aFields[4];
    rChunk.SnapshotId = aFields[5];
    rChunk.ContentHash = aFields[6];
    rChunk.TextHash = aFields[7];
    rChunk.Granularity = aFields[8];
    rChunk.Ordinal = aFields[9].toInt32();
    rChunk.TokenCount = aFields[10].toInt32();
    rChunk.Language = aFields[11];
    rChunk.RetrievalMode = aFields[12];
    rChunk.Backend = aFields[13];
    rChunk.EvidenceId = aFields[14];
    rChunk.HashReference = aFields[15];
    return true;
}
}

AIChatKnowledgeIndexStore::AIChatKnowledgeIndexStore(const OUString& rWorkspaceIdentity)
    : m_sWorkspaceHash(MakeWorkspaceHash(rWorkspaceIdentity.isEmpty()
                                             ? ResolveCurrentWorkspaceIdentity()
                                             : rWorkspaceIdentity))
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + KNOWLEDGE_INDEX_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sWorkspaceSidecarDirUrl = m_sStorageRootUrl + u"/"_ustr + m_sWorkspaceHash;
    osl::Directory::createPath(m_sWorkspaceSidecarDirUrl);
    m_sChunkSidecarUrl = m_sWorkspaceSidecarDirUrl + u"/"_ustr + KNOWLEDGE_INDEX_CHUNK_FILE_NAME;
}

OUString AIChatKnowledgeIndexStore::ResolveCurrentWorkspaceIdentity()
{
    if (SfxObjectShell* pShell = SfxObjectShell::Current())
    {
        if (SfxMedium* pMedium = pShell->GetMedium())
        {
            const OUString sUrl
                = pMedium->GetURLObject().GetMainURL(INetURLObject::DecodeMechanism::NONE);
            if (!sUrl.isEmpty())
                return u"workspace-url:"_ustr + sUrl;
        }

        return u"unsaved-workspace-shell:"_ustr
               + OUString::number(reinterpret_cast<sal_uInt64>(pShell));
    }

    return u"default-local-workspace"_ustr;
}

OUString AIChatKnowledgeIndexStore::MakeWorkspaceHash(const OUString& rWorkspaceIdentity)
{
    return MakeMetadataHash(u"workspace-hash:"_ustr + rWorkspaceIdentity);
}

OUString AIChatKnowledgeIndexStore::MakeMetadataHash(const OUString& rMetadata)
{
    const OString sMetadata = OUStringToOString(rMetadata, RTL_TEXTENCODING_UTF8);
    const std::vector<unsigned char> aHash = comphelper::Hash::calculateHash(
        sMetadata.getStr(), sMetadata.getLength(), comphelper::HashType::SHA256);
    return OUString::createFromAscii(comphelper::hashToString(aHash));
}

OUString AIChatKnowledgeIndexStore::MakeChunkId(const OUString& rWorkspaceHash,
                                                const OUString& rSourceId, sal_Int32 nOrdinal)
{
    return u"kbch-"_ustr
           + MakeMetadataHash(rWorkspaceHash + u":"_ustr + rSourceId + u":"_ustr
                              + OUString::number(nOrdinal))
                 .copy(0, 16);
}

OUString AIChatKnowledgeIndexStore::MakeSourceUriHash(const OUString& rSourceUri)
{
    return MakeMetadataHash(u"source-uri:"_ustr + rSourceUri);
}

bool AIChatKnowledgeIndexStore::IsValidStoragePolicy()
{
    const OUString sIndexRoot = u"application-data-directory"_ustr;
    const OUString sWorkspacePartition = u"per-workspace"_ustr;
    const OUString sPathIdentity = u"workspace-hash"_ustr;
    const bool bColocatedWithUserDocuments = false;
    const bool bSyncsWithUserDocuments = false;
    const bool bStoresDocumentContent = false;
    const OUString sRuntimeStorageImplementation = u"not-started"_ustr;

    return sIndexRoot == u"application-data-directory"_ustr
           && sWorkspacePartition == u"per-workspace"_ustr
           && sPathIdentity == u"workspace-hash"_ustr && !bColocatedWithUserDocuments
           && !bSyncsWithUserDocuments && !bStoresDocumentContent
           && sRuntimeStorageImplementation == u"not-started"_ustr;
}

bool AIChatKnowledgeIndexStore::ContainsRawContentFieldName(const OUString& rFieldName)
{
    return rFieldName == u"text"_ustr || rFieldName == u"content"_ustr
           || rFieldName == u"rawText"_ustr || rFieldName == u"body"_ustr
           || rFieldName == u"documentText"_ustr || rFieldName == u"contentText"_ustr
           || rFieldName == u"queryText"_ustr || rFieldName == u"snippetText"_ustr;
}

AIChatKnowledgeIndexStoreResult AIChatKnowledgeIndexStore::RegisterChunkMetadata(
    const AIChatKnowledgeIndexChunk& rChunk) const
{
    AIChatKnowledgeIndexStoreResult aResult;
    aResult.Chunk = rChunk;
    aResult.Chunk.WorkspaceHash = m_sWorkspaceHash;

    if (!IsValidStoragePolicy())
    {
        aResult.Message = u"knowledge-index-store-failed reason=storage-policy-invalid"_ustr;
        return aResult;
    }
    if (aResult.Chunk.SourceKind != u"document"_ustr && aResult.Chunk.SourceKind != u"connector"_ustr)
    {
        aResult.Message = u"knowledge-index-store-failed reason=unsupported-source-kind"_ustr;
        return aResult;
    }
    if (!IsLowerHex64(aResult.Chunk.SourceUriHash) || !IsLowerHex64(aResult.Chunk.ContentHash)
        || !IsLowerHex64(aResult.Chunk.TextHash))
    {
        aResult.Message = u"knowledge-index-store-failed reason=hash-reference-required"_ustr;
        return aResult;
    }
    if (aResult.Chunk.Granularity != u"paragraph"_ustr
        && aResult.Chunk.Granularity != u"sentence-fallback"_ustr)
    {
        aResult.Message = u"knowledge-index-store-failed reason=unsupported-granularity"_ustr;
        return aResult;
    }
    if (aResult.Chunk.TokenCount <= 0 || aResult.Chunk.TokenCount > 2048)
    {
        aResult.Message = u"knowledge-index-store-failed reason=token-count-out-of-range"_ustr;
        return aResult;
    }
    if (aResult.Chunk.RetrievalMode != u"fts"_ustr && aResult.Chunk.RetrievalMode != u"hybrid"_ustr)
    {
        aResult.Message = u"knowledge-index-store-failed reason=unsupported-retrieval-mode"_ustr;
        return aResult;
    }
    if (aResult.Chunk.Backend != u"sqlite-fts5"_ustr
        && aResult.Chunk.Backend != u"lancedb-local"_ustr)
    {
        aResult.Message = u"knowledge-index-store-failed reason=unsupported-backend"_ustr;
        return aResult;
    }
    if (aResult.Chunk.Backend == u"lancedb-local"_ustr
        && aResult.Chunk.RetrievalMode != u"hybrid"_ustr)
    {
        aResult.Message = u"knowledge-index-store-failed reason=vector-backend-requires-hybrid"_ustr;
        return aResult;
    }
    if (aResult.Chunk.EvidenceId.isEmpty() || aResult.Chunk.HashReference.isEmpty())
    {
        aResult.Message = u"knowledge-index-store-failed reason=evidence-and-hash-required"_ustr;
        return aResult;
    }

    if (aResult.Chunk.ChunkId.isEmpty())
        aResult.Chunk.ChunkId
            = MakeChunkId(m_sWorkspaceHash, aResult.Chunk.SourceId, aResult.Chunk.Ordinal);

    const OUString sLine
        = EscapeField(aResult.Chunk.ChunkId) + u"\t"_ustr
          + EscapeField(aResult.Chunk.WorkspaceHash) + u"\t"_ustr
          + EscapeField(aResult.Chunk.SourceKind) + u"\t"_ustr
          + EscapeField(aResult.Chunk.SourceUriHash) + u"\t"_ustr
          + EscapeField(aResult.Chunk.SourceId) + u"\t"_ustr
          + EscapeField(aResult.Chunk.SnapshotId) + u"\t"_ustr
          + EscapeField(aResult.Chunk.ContentHash) + u"\t"_ustr
          + EscapeField(aResult.Chunk.TextHash) + u"\t"_ustr
          + EscapeField(aResult.Chunk.Granularity) + u"\t"_ustr
          + OUString::number(aResult.Chunk.Ordinal) + u"\t"_ustr
          + OUString::number(aResult.Chunk.TokenCount) + u"\t"_ustr
          + EscapeField(aResult.Chunk.Language) + u"\t"_ustr
          + EscapeField(aResult.Chunk.RetrievalMode) + u"\t"_ustr
          + EscapeField(aResult.Chunk.Backend) + u"\t"_ustr
          + EscapeField(aResult.Chunk.EvidenceId) + u"\t"_ustr
          + EscapeField(aResult.Chunk.HashReference) + u"\n"_ustr;

    if (!AppendUtf8Line(m_sChunkSidecarUrl, sLine))
    {
        aResult.Message = u"knowledge-index-store-failed reason=sidecar-write-failed"_ustr;
        return aResult;
    }

    aResult.Success = true;
    aResult.Message = u"knowledge-index-chunk-stored chunk-id="_ustr + aResult.Chunk.ChunkId
                      + u" workspace-hash="_ustr + m_sWorkspaceHash
                      + u" index-root=application-data-directory workspace-partition=per-workspace"_ustr
                      + u" path-identity=workspace-hash colocated-with-user-documents=false"_ustr
                      + u" syncs-with-user-documents=false stores-document-content=false"_ustr
                      + u" runtime-storage-implementation=not-started metadata-only=true"_ustr
                      + u" raw-document-content=false raw-query-text=false raw-snippet=false"_ustr
                      + u" public-egress=false silent-model-download=false vector-default=sqlite-fts5"_ustr;
    return aResult;
}

std::vector<AIChatKnowledgeIndexChunk> AIChatKnowledgeIndexStore::LoadChunks() const
{
    OString sContent;
    if (!ReadUtf8File(m_sChunkSidecarUrl, sContent) || sContent.isEmpty())
        return {};

    const OUString sUtf16 = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    std::vector<AIChatKnowledgeIndexChunk> aChunks;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sLine = sUtf16.getToken(0, '\n', nIndex);
        if (sLine.isEmpty())
            continue;

        AIChatKnowledgeIndexChunk aChunk;
        if (ParseChunkLine(sLine, aChunk))
            aChunks.push_back(aChunk);
    }
    return aChunks;
}

bool AIChatKnowledgeIndexStore::ClearWorkspaceSidecar() const
{
    const osl::FileBase::RC eResult = osl::File::remove(m_sChunkSidecarUrl);
    return eResult == osl::FileBase::E_None || eResult == osl::FileBase::E_NOENT;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
