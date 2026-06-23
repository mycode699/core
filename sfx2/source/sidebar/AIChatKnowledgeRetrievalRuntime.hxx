/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge retrieval guards).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatKnowledgeIndexStore.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatKnowledgeRetrievalQuery
{
    OUString WorkspaceIdentity;
    OUString QueryId;
    OUString QueryTextHash;
    OUString Intent;
    OUString Language;
    OUString Mode;
    sal_Int32 TopK = 10;
    bool IncludeMetadata = false;
    bool UserConfirmedModelDownload = false;
    bool VectorOptIn = false;
    bool PublicEgressAllowed = false;
    bool TenantPolicyApproved = false;
};

struct AIChatKnowledgeRetrievalChunkResult
{
    OUString ChunkId;
    sal_Int32 Rank = 0;
    sal_Int32 ScoreBasisPoints = 0;
    OUString SourceKind;
    OUString TextHash;
    OUString SnippetHash;
    OUString EvidenceId;
};

struct AIChatKnowledgeRetrievalResult
{
    bool Success = false;
    OUString QueryId;
    OUString ResultId;
    OUString WorkspaceHash;
    OUString RetrievalMode;
    OUString Backend;
    OUString Rerank;
    sal_Int32 LatencyMs = 0;
    bool Truncated = false;
    std::vector<AIChatKnowledgeRetrievalChunkResult> Chunks;
    OUString Message;
};

class AIChatKnowledgeRetrievalRuntime final
{
public:
    AIChatKnowledgeRetrievalResult Query(const AIChatKnowledgeRetrievalQuery& rQuery) const;

    static bool IsTopKAllowed(sal_Int32 nTopK);
    static bool IsFtsQuery(const AIChatKnowledgeRetrievalQuery& rQuery);
    static bool IsHybridQuery(const AIChatKnowledgeRetrievalQuery& rQuery);
    static bool IsVectorPathAllowed(const AIChatKnowledgeRetrievalQuery& rQuery);
    static OUString MakeQueryId(const OUString& rWorkspaceHash, const OUString& rQueryTextHash);
    static OUString MakeResultId(const OUString& rQueryId);
    static OUString MakeSnippetHash(const AIChatKnowledgeIndexChunk& rChunk,
                                    const OUString& rQueryTextHash);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
