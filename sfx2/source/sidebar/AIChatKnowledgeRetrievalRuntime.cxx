/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge retrieval guards).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatKnowledgeRetrievalRuntime.hxx"

#include <algorithm>

namespace sfx2::sidebar
{
namespace
{
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

bool SourceKindAllowed(const OUString& rSourceKind)
{
    return rSourceKind == u"document"_ustr || rSourceKind == u"connector"_ustr;
}
}

bool AIChatKnowledgeRetrievalRuntime::IsTopKAllowed(sal_Int32 nTopK)
{
    return nTopK >= 1 && nTopK <= 10;
}

bool AIChatKnowledgeRetrievalRuntime::IsFtsQuery(const AIChatKnowledgeRetrievalQuery& rQuery)
{
    return rQuery.Mode == u"fts"_ustr && !rQuery.VectorOptIn
           && !rQuery.UserConfirmedModelDownload;
}

bool AIChatKnowledgeRetrievalRuntime::IsHybridQuery(const AIChatKnowledgeRetrievalQuery& rQuery)
{
    return rQuery.Mode == u"hybrid"_ustr;
}

bool AIChatKnowledgeRetrievalRuntime::IsVectorPathAllowed(
    const AIChatKnowledgeRetrievalQuery& rQuery)
{
    return IsHybridQuery(rQuery) && rQuery.VectorOptIn && rQuery.UserConfirmedModelDownload
           && !rQuery.PublicEgressAllowed && rQuery.TenantPolicyApproved;
}

OUString AIChatKnowledgeRetrievalRuntime::MakeQueryId(const OUString& rWorkspaceHash,
                                                      const OUString& rQueryTextHash)
{
    return u"kbq-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rWorkspaceHash + u":query:"_ustr
                                                         + rQueryTextHash)
                 .copy(0, 16);
}

OUString AIChatKnowledgeRetrievalRuntime::MakeResultId(const OUString& rQueryId)
{
    return u"kbr-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rQueryId + u":result"_ustr).copy(0, 16);
}

OUString AIChatKnowledgeRetrievalRuntime::MakeSnippetHash(
    const AIChatKnowledgeIndexChunk& rChunk, const OUString& rQueryTextHash)
{
    return AIChatKnowledgeIndexStore::MakeMetadataHash(rChunk.ChunkId + u":snippet:"_ustr
                                                       + rChunk.TextHash + u":"_ustr
                                                       + rQueryTextHash);
}

AIChatKnowledgeRetrievalResult
AIChatKnowledgeRetrievalRuntime::Query(const AIChatKnowledgeRetrievalQuery& rQuery) const
{
    AIChatKnowledgeRetrievalResult aResult;
    AIChatKnowledgeIndexStore aStore(rQuery.WorkspaceIdentity);
    aResult.WorkspaceHash = aStore.GetWorkspaceHash();
    aResult.QueryId
        = rQuery.QueryId.isEmpty() ? MakeQueryId(aResult.WorkspaceHash, rQuery.QueryTextHash)
                                   : rQuery.QueryId;
    aResult.ResultId = MakeResultId(aResult.QueryId);

    if (!IsLowerHex64(rQuery.QueryTextHash))
    {
        aResult.Message = u"knowledge-query-failed reason=query-text-hash-required"_ustr;
        return aResult;
    }
    if (!IsTopKAllowed(rQuery.TopK))
    {
        aResult.Message = u"knowledge-query-failed reason=topk-out-of-range top-k-max=10"_ustr;
        return aResult;
    }
    if (!rQuery.TenantPolicyApproved)
    {
        aResult.Message = u"knowledge-query-failed reason=tenant-policy-required"_ustr;
        return aResult;
    }
    if (rQuery.PublicEgressAllowed)
    {
        aResult.Message = u"knowledge-query-failed reason=public-egress-forbidden"_ustr;
        return aResult;
    }

    if (IsFtsQuery(rQuery))
    {
        aResult.RetrievalMode = u"fts"_ustr;
        aResult.Backend = u"sqlite-fts5"_ustr;
        aResult.Rerank = u"none"_ustr;
    }
    else if (IsHybridQuery(rQuery))
    {
        if (!IsVectorPathAllowed(rQuery))
        {
            aResult.RetrievalMode = u"fts"_ustr;
            aResult.Backend = u"sqlite-fts5"_ustr;
            aResult.Rerank = u"none"_ustr;
            aResult.Message
                = u"knowledge-query-fallback reason=vector-path-not-authorized fallback=sqlite-fts5 model-download=explicit-user-confirmed vector-opt-in-required=true public-egress=false"_ustr;
        }
        else
        {
            aResult.RetrievalMode = u"hybrid"_ustr;
            aResult.Backend = u"sqlite-fts5+lancedb-local"_ustr;
            aResult.Rerank = u"bge-m3"_ustr;
        }
    }
    else
    {
        aResult.Message = u"knowledge-query-failed reason=unsupported-retrieval-mode"_ustr;
        return aResult;
    }

    std::vector<AIChatKnowledgeIndexChunk> aChunks = aStore.LoadChunks();
    sal_Int32 nRank = 1;
    for (const auto& rChunk : aChunks)
    {
        if (nRank > rQuery.TopK)
        {
            aResult.Truncated = true;
            break;
        }
        if (!SourceKindAllowed(rChunk.SourceKind))
            continue;
        if (!IsLowerHex64(rChunk.TextHash))
            continue;
        if (rChunk.EvidenceId.isEmpty())
            continue;

        AIChatKnowledgeRetrievalChunkResult aChunk;
        aChunk.ChunkId = rChunk.ChunkId;
        aChunk.Rank = nRank;
        aChunk.ScoreBasisPoints = std::max<sal_Int32>(0, 10000 - ((nRank - 1) * 500));
        aChunk.SourceKind = rChunk.SourceKind;
        aChunk.TextHash = rChunk.TextHash;
        aChunk.SnippetHash = MakeSnippetHash(rChunk, rQuery.QueryTextHash);
        aChunk.EvidenceId = rChunk.EvidenceId;
        aResult.Chunks.push_back(aChunk);
        ++nRank;
    }

    aResult.LatencyMs = 0;
    aResult.Success = true;
    const OUString sPrefix = aResult.Message.isEmpty() ? u"knowledge-query-complete"_ustr
                                                       : aResult.Message;
    aResult.Message = sPrefix + u" query-id="_ustr + aResult.QueryId + u" result-id="_ustr
                      + aResult.ResultId + u" retrieval-mode="_ustr + aResult.RetrievalMode
                      + u" backend="_ustr + aResult.Backend + u" rerank="_ustr + aResult.Rerank
                      + u" top-k<=10 result-count="_ustr
                      + OUString::number(static_cast<sal_Int32>(aResult.Chunks.size()))
                      + u" stores-query-text=false stores-document-content=false"_ustr
                      + u" snippet-hash-only=true raw-query-text=false raw-snippet=false"_ustr
                      + u" public-egress=false local-embedding=true metadata-only=true"_ustr
                      + u" silent-model-download=false runtime-vector-store-implementation=not-started"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
