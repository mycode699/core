/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge result content bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatKnowledgeResultContentBridge.hxx"

#include "AIChatSourceProvenance.hxx"

namespace sfx2::sidebar
{

OUString AIChatKnowledgeResultContentBridge::MakeKnowledgeResultEvidenceId(
    const AIChatKnowledgeRetrievalResult& rResult)
{
    if (!rResult.Chunks.empty() && !rResult.Chunks.front().EvidenceId.isEmpty())
        return rResult.Chunks.front().EvidenceId;

    return u"evidence:knowledge-query:"_ustr + rResult.ResultId;
}

OUString AIChatKnowledgeResultContentBridge::MakeKnowledgeResultHashReference(
    const AIChatKnowledgeRetrievalResult& rResult)
{
    if (!rResult.Chunks.empty() && !rResult.Chunks.front().SnippetHash.isEmpty())
        return u"sha256:"_ustr + rResult.Chunks.front().SnippetHash;

    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rResult.QueryId + u":"_ustr
                                                         + rResult.ResultId);
}

AIChatKnowledgeResultContentBridgeResult
AIChatKnowledgeResultContentBridge::RegisterResult(
    const AIChatKnowledgeRetrievalResult& rResult) const
{
    AIChatKnowledgeResultContentBridgeResult aResult;

    if (!rResult.Success)
    {
        aResult.Message = u"knowledge-result-register-failed reason=query-not-successful"_ustr;
        return aResult;
    }
    if (rResult.ResultId.isEmpty() || rResult.QueryId.isEmpty())
    {
        aResult.Message = u"knowledge-result-register-failed reason=missing-result-or-query-id"_ustr;
        return aResult;
    }
    if (rResult.Chunks.empty())
    {
        aResult.Message = u"knowledge-result-register-failed reason=missing-result-chunks"_ustr;
        return aResult;
    }

    aResult.EvidenceId = MakeKnowledgeResultEvidenceId(rResult);
    aResult.HashReference = MakeKnowledgeResultHashReference(rResult);
    if (aResult.EvidenceId.isEmpty() || aResult.HashReference.isEmpty())
    {
        aResult.Message = u"knowledge-result-register-failed reason=missing-evidence-or-hash"_ustr;
        return aResult;
    }

    aResult.RegistryEntry.ObjectId = rResult.ResultId;
    aResult.RegistryEntry.Type = u"knowledge-index-result"_ustr;
    aResult.RegistryEntry.SourceSurface = u"knowledge-index-query"_ustr;
    aResult.RegistryEntry.State = u"registered"_ustr;
    aResult.RegistryEntry.EvidenceId = aResult.EvidenceId;
    aResult.RegistryEntry.HashReference = aResult.HashReference;
    aResult.RegistryEntry.OpenTarget = u"sidebar-preview"_ustr;
    aResult.RegistryEntry.PreviewMode = u"metadata-summary"_ustr;

    AIChatContentRegistry aRegistry;
    if (!aRegistry.RegisterObject(aResult.RegistryEntry))
    {
        aResult.Message = u"knowledge-result-register-failed reason=registry-write-failed"_ustr;
        return aResult;
    }

    AIChatSourceProvenance aProvenance;
    AIChatSourceProvenanceEntry aSource;
    aSource.SourceId = AIChatSourceProvenance::MakeSourceId(rResult.ResultId);
    aSource.SourceType = u"knowledge-index-result"_ustr;
    aSource.CitationId = AIChatSourceProvenance::MakeCitationId(rResult.ResultId);
    aSource.EvidenceId = aResult.EvidenceId;
    aSource.HashReference = aResult.HashReference;
    aSource.SourceSurface = u"knowledge-index-query"_ustr;
    aSource.OpenTarget = u"sidebar-preview"_ustr;
    aSource.SpanReference = u"span:knowledge-query:"_ustr + rResult.QueryId;
    aSource.ReviewId = OUString();
    aProvenance.RegisterSource(aSource);

    aResult.SourceId = aSource.SourceId;
    aResult.CitationId = aSource.CitationId;
    aResult.Success = true;
    aResult.Message = u"knowledge-result-registered result-id="_ustr + rResult.ResultId
                      + u" query-id="_ustr + rResult.QueryId
                      + u" type=knowledge-index-result registry=true provenance=true"_ustr
                      + u" open-target=sidebar-preview preview-mode=metadata-summary"_ustr
                      + u" evidence-id="_ustr + aResult.EvidenceId
                      + u" hash-reference="_ustr + aResult.HashReference
                      + u" citation-id="_ustr + aResult.CitationId
                      + u" metadata-only=true raw-query-text=false raw-snippet=false"_ustr
                      + u" stores-document-content=false read-only=true main-document-mutation=false"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
