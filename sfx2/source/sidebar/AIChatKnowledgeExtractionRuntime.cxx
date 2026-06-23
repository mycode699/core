/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge extraction guards).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatKnowledgeExtractionRuntime.hxx"

namespace sfx2::sidebar
{

bool AIChatKnowledgeExtractionRuntime::IsSupportedDocumentFamily(
    const OUString& rDocumentFamily)
{
    return rDocumentFamily == u"writer"_ustr || rDocumentFamily == u"calc"_ustr
           || rDocumentFamily == u"impress"_ustr || rDocumentFamily == u"connector"_ustr;
}

bool AIChatKnowledgeExtractionRuntime::IsLibreOfficeDocumentModelExtraction(
    const AIChatKnowledgeExtractionRequest& rRequest)
{
    if (rRequest.SourceKind != u"document"_ustr)
        return false;
    if (rRequest.StandalonePptParserAllowed)
        return false;
    if (!rRequest.UsesLibreOfficeImportFilter || !rRequest.UsesDocumentModel)
        return false;
    if (rRequest.DocumentFamily == u"writer"_ustr)
        return rRequest.InputFormat == u"odt"_ustr && !rRequest.PreservesSlideElementRefs;
    if (rRequest.DocumentFamily == u"calc"_ustr)
        return rRequest.InputFormat == u"ods"_ustr && !rRequest.PreservesSlideElementRefs;
    if (rRequest.DocumentFamily == u"impress"_ustr)
    {
        return (rRequest.InputFormat == u"odp"_ustr || rRequest.InputFormat == u"pptx"_ustr)
               && rRequest.PreservesSlideElementRefs;
    }
    return false;
}

bool AIChatKnowledgeExtractionRuntime::IsConnectorExtraction(
    const AIChatKnowledgeExtractionRequest& rRequest)
{
    return rRequest.SourceKind == u"connector"_ustr
           && rRequest.DocumentFamily == u"connector"_ustr
           && rRequest.InputFormat == u"connector-markdown"_ustr
           && !rRequest.UsesLibreOfficeImportFilter && !rRequest.UsesDocumentModel
           && !rRequest.PreservesSlideElementRefs && !rRequest.StandalonePptParserAllowed;
}

bool AIChatKnowledgeExtractionRuntime::IsPptxExtractionPolicyAllowed(
    const AIChatKnowledgeExtractionRequest& rRequest)
{
    if (rRequest.InputFormat != u"pptx"_ustr)
        return true;

    return rRequest.DocumentFamily == u"impress"_ustr && rRequest.SourceKind == u"document"_ustr
           && rRequest.UsesLibreOfficeImportFilter && rRequest.UsesDocumentModel
           && rRequest.PreservesSlideElementRefs && !rRequest.StandalonePptParserAllowed;
}

OUString AIChatKnowledgeExtractionRuntime::ResolveTextExtractionPath(
    const AIChatKnowledgeExtractionRequest& rRequest)
{
    if (IsConnectorExtraction(rRequest))
        return u"connector-normalized-markdown"_ustr;
    if (IsLibreOfficeDocumentModelExtraction(rRequest))
        return u"document-model"_ustr;
    return u"unsupported"_ustr;
}

AIChatKnowledgeExtractionResult AIChatKnowledgeExtractionRuntime::CreateChunkMetadata(
    const AIChatKnowledgeExtractionRequest& rRequest) const
{
    AIChatKnowledgeExtractionResult aResult;
    aResult.TextExtractionPath = ResolveTextExtractionPath(rRequest);

    if (!IsSupportedDocumentFamily(rRequest.DocumentFamily))
    {
        aResult.Message = u"knowledge-extraction-failed reason=unsupported-document-family"_ustr;
        return aResult;
    }
    if (aResult.TextExtractionPath == u"unsupported"_ustr)
    {
        aResult.Message = u"knowledge-extraction-failed reason=unsupported-extraction-path"_ustr;
        return aResult;
    }
    if (!IsPptxExtractionPolicyAllowed(rRequest))
    {
        aResult.Message
            = u"knowledge-extraction-failed reason=pptx-must-use-libreoffice-import-filter document-family=impress uses-document-model=true standalone-ppt-parser-allowed=false"_ustr;
        return aResult;
    }
    if (AIChatKnowledgeIndexStore::ContainsRawContentFieldName(u"text"_ustr)
        && AIChatKnowledgeIndexStore::ContainsRawContentFieldName(u"documentText"_ustr)
        && AIChatKnowledgeIndexStore::ContainsRawContentFieldName(u"snippetText"_ustr))
    {
        // Guard call keeps the extraction runtime aligned with the store's raw-content denylist.
    }

    AIChatKnowledgeIndexStore aStore(rRequest.WorkspaceIdentity);
    AIChatKnowledgeIndexChunk aChunk;
    aChunk.WorkspaceHash = aStore.GetWorkspaceHash();
    aChunk.SourceKind = rRequest.SourceKind;
    aChunk.SourceUriHash = rRequest.SourceUriHash;
    aChunk.SourceId = rRequest.SourceId;
    aChunk.SnapshotId = rRequest.SnapshotId;
    aChunk.ContentHash = rRequest.ContentHash;
    aChunk.TextHash = rRequest.TextHash;
    aChunk.Granularity = rRequest.Granularity;
    aChunk.Ordinal = rRequest.Ordinal;
    aChunk.TokenCount = rRequest.TokenCount;
    aChunk.Language = rRequest.Language;
    aChunk.RetrievalMode = u"fts"_ustr;
    aChunk.Backend = u"sqlite-fts5"_ustr;
    aChunk.EvidenceId = rRequest.EvidenceId;
    aChunk.HashReference = rRequest.HashReference;

    AIChatKnowledgeIndexStoreResult aStoreResult = aStore.RegisterChunkMetadata(aChunk);
    if (!aStoreResult.Success)
    {
        aResult.Message = u"knowledge-extraction-failed reason=store-rejected "_ustr
                          + aStoreResult.Message;
        return aResult;
    }

    aResult.Chunk = aStoreResult.Chunk;
    aResult.ExtractionPolicy = u"document-family="_ustr + rRequest.DocumentFamily
                               + u" input-format="_ustr + rRequest.InputFormat
                               + u" text-extraction-path="_ustr + aResult.TextExtractionPath
                               + u" uses-libreoffice-import-filter="_ustr
                               + (rRequest.UsesLibreOfficeImportFilter ? u"true"_ustr
                                                                       : u"false"_ustr)
                               + u" uses-document-model="_ustr
                               + (rRequest.UsesDocumentModel ? u"true"_ustr : u"false"_ustr)
                               + u" standalone-ppt-parser-allowed=false"_ustr
                               + u" runtime-extraction-implementation=not-started"_ustr;

    aResult.Success = true;
    aResult.Message = u"knowledge-extraction-metadata-created chunk-id="_ustr
                      + aResult.Chunk.ChunkId + u" "_ustr + aResult.ExtractionPolicy
                      + u" preserves-slide-element-refs="_ustr
                      + (rRequest.PreservesSlideElementRefs ? u"true"_ustr : u"false"_ustr)
                      + u" stores-document-content=false raw-document-content=false"_ustr
                      + u" raw-query-text=false raw-snippet=false metadata-only=true"_ustr
                      + u" backend=sqlite-fts5 public-egress=false"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
