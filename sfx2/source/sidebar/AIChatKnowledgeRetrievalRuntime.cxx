/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge retrieval guards).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatKnowledgeRetrievalRuntime.hxx"

#include "AIChatKnowledgeFtsEngine.hxx"

#include <comphelper/hash.hxx>
#include <rtl/ustrbuf.hxx>

#include <algorithm>
#include <vector>

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
    return rSourceKind == u"document"_ustr || rSourceKind == u"connector"_ustr
           || rSourceKind == u"file"_ustr;
}

OUString Sha256Hex(const OUString& rText)
{
    const OString sUtf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    const std::vector<unsigned char> aHash = comphelper::Hash::calculateHash(
        sUtf8.getStr(), sUtf8.getLength(), comphelper::HashType::SHA256);
    return OUString::createFromAscii(comphelper::hashToString(aHash));
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

OUString AIChatKnowledgeRetrievalRuntime::MakeQueryTextHash(const OUString& rQueryText)
{
    return Sha256Hex(rQueryText);
}

AIChatKnowledgeRetrievalResult
AIChatKnowledgeRetrievalRuntime::QueryOpenDocumentFts(const OUString& rQueryText, sal_Int32 nTopK)
{
    AIChatKnowledgeRetrievalQuery q;
    q.WorkspaceIdentity = AIChatKnowledgeIndexStore::ResolveCurrentWorkspaceIdentity();
    q.RuntimeQueryText = rQueryText;
    q.QueryTextHash = MakeQueryTextHash(rQueryText);
    q.Mode = u"fts"_ustr;
    q.TopK = nTopK;
    q.TenantPolicyApproved = true;
    q.PublicEgressAllowed = false;
    q.IndexOpenDocument = true;
    q.Intent = u"document-rag"_ustr;
    return AIChatKnowledgeRetrievalRuntime().Query(q);
}

OUString AIChatKnowledgeRetrievalRuntime::BuildFtsPromptBlock(
    const AIChatKnowledgeRetrievalResult& rResult, const OUString& rQueryText, sal_Int32 nMaxChars)
{
    if (!rResult.Success)
        return {};
    // Prefer real FTS engine block when query text is available.
    if (!rQueryText.isEmpty())
    {
        const auto search = AIChatKnowledgeFtsEngine::Search(rQueryText, rResult.Chunks.empty()
                                                                              ? 6
                                                                              : static_cast<sal_Int32>(rResult.Chunks.size()));
        const OUString block = AIChatKnowledgeFtsEngine::BuildPromptBlock(search, nMaxChars);
        if (!block.isEmpty())
            return block;
    }
    // Fallback: hash-only listing (no raw snippets).
    OUStringBuffer b;
    b.append(u"【本地知识检索 · 仅 hash 元数据 · 无外传】\n"_ustr);
    for (const auto& c : rResult.Chunks)
    {
        b.append(u"["_ustr);
        b.append(c.Rank);
        b.append(u"] chunk="_ustr);
        b.append(c.ChunkId);
        b.append(u" text-hash="_ustr);
        b.append(c.TextHash);
        b.append(u"\n"_ustr);
    }
    return b.makeStringAndClear();
}

AIChatKnowledgeRetrievalResult
AIChatKnowledgeRetrievalRuntime::Query(const AIChatKnowledgeRetrievalQuery& rQuery) const
{
    AIChatKnowledgeRetrievalResult aResult;
    AIChatKnowledgeIndexStore aStore(rQuery.WorkspaceIdentity);
    aResult.WorkspaceHash = aStore.GetWorkspaceHash();

    OUString queryTextHash = rQuery.QueryTextHash;
    if (queryTextHash.isEmpty() && !rQuery.RuntimeQueryText.isEmpty())
        queryTextHash = MakeQueryTextHash(rQuery.RuntimeQueryText);

    aResult.QueryId
        = rQuery.QueryId.isEmpty() ? MakeQueryId(aResult.WorkspaceHash, queryTextHash)
                                   : rQuery.QueryId;
    aResult.ResultId = MakeResultId(aResult.QueryId);

    // Keep the historical guard token for hash-only callers; runtime FTS may fill hash from text.
    if (!IsLowerHex64(rQuery.QueryTextHash) && rQuery.RuntimeQueryText.isEmpty())
    {
        aResult.Message = u"knowledge-query-failed reason=query-text-hash-required"_ustr;
        return aResult;
    }
    if (!IsLowerHex64(queryTextHash))
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

    // M9 real FTS path when runtime query text is provided (never persisted as raw query).
    if (aResult.RetrievalMode == u"fts"_ustr && !rQuery.RuntimeQueryText.isEmpty()
        && AIChatKnowledgeFtsEngine::IsSqliteAvailable())
    {
        if (rQuery.IndexOpenDocument)
            AIChatKnowledgeFtsEngine::IndexOpenDocument(rQuery.WorkspaceIdentity);

        const auto fts = AIChatKnowledgeFtsEngine::Search(rQuery.RuntimeQueryText, rQuery.TopK,
                                                          rQuery.WorkspaceIdentity);
        aResult.LatencyMs = fts.LatencyMs;
        aResult.Backend = u"sqlite-fts5"_ustr;
        if (fts.Success)
        {
            for (const auto& hit : fts.Hits)
            {
                AIChatKnowledgeRetrievalChunkResult aChunk;
                aChunk.ChunkId = hit.ChunkId;
                aChunk.Rank = hit.Rank;
                aChunk.ScoreBasisPoints = hit.ScoreBasisPoints;
                aChunk.SourceKind = hit.SourceKind.isEmpty() ? u"document"_ustr : hit.SourceKind;
                aChunk.TextHash = hit.TextHash;
                aChunk.SnippetHash
                    = AIChatKnowledgeIndexStore::MakeMetadataHash(hit.TextHash + u":"_ustr
                                                                  + queryTextHash);
                aChunk.EvidenceId = hit.EvidenceId;
                aResult.Chunks.push_back(aChunk);
            }
            aResult.Success = true;
            aResult.Message
                = u"knowledge-query-complete query-id="_ustr + aResult.QueryId + u" result-id="_ustr
                  + aResult.ResultId + u" retrieval-mode=fts backend=sqlite-fts5 real-fts=true"_ustr
                  + u" result-count="_ustr
                  + OUString::number(static_cast<sal_Int32>(aResult.Chunks.size()))
                  + u" stores-query-text=false stores-document-content=false"_ustr
                  + u" snippet-hash-only=true raw-query-text=false public-egress=false"_ustr
                  + u" silent-model-download=false runtime-fts-implementation=sqlite-fts5"_ustr;
            return aResult;
        }
        // Fall through to metadata ranking if FTS failed.
        aResult.Message = fts.Message;
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
        aChunk.SnippetHash = MakeSnippetHash(rChunk, queryTextHash);
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
