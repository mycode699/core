/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge Index sidecar storage).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatKnowledgeIndexChunk
{
    OUString ChunkId;
    OUString WorkspaceHash;
    OUString SourceKind;
    OUString SourceUriHash;
    OUString SourceId;
    OUString SnapshotId;
    OUString ContentHash;
    OUString TextHash;
    OUString Granularity;
    sal_Int32 Ordinal = 0;
    sal_Int32 TokenCount = 0;
    OUString Language;
    OUString RetrievalMode;
    OUString Backend;
    OUString EvidenceId;
    OUString HashReference;
};

struct AIChatKnowledgeIndexStoreResult
{
    bool Success = false;
    AIChatKnowledgeIndexChunk Chunk;
    OUString Message;
};

class AIChatKnowledgeIndexStore final
{
public:
    explicit AIChatKnowledgeIndexStore(const OUString& rWorkspaceIdentity);

    const OUString& GetWorkspaceHash() const { return m_sWorkspaceHash; }
    const OUString& GetStorageRootUrl() const { return m_sStorageRootUrl; }
    const OUString& GetWorkspaceSidecarDirUrl() const { return m_sWorkspaceSidecarDirUrl; }
    const OUString& GetChunkSidecarUrl() const { return m_sChunkSidecarUrl; }

    AIChatKnowledgeIndexStoreResult RegisterChunkMetadata(
        const AIChatKnowledgeIndexChunk& rChunk) const;
    std::vector<AIChatKnowledgeIndexChunk> LoadChunks() const;
    bool ClearWorkspaceSidecar() const;

    static OUString ResolveCurrentWorkspaceIdentity();
    static OUString MakeWorkspaceHash(const OUString& rWorkspaceIdentity);
    static OUString MakeMetadataHash(const OUString& rMetadata);
    static OUString MakeChunkId(const OUString& rWorkspaceHash, const OUString& rSourceId,
                                sal_Int32 nOrdinal);
    static OUString MakeSourceUriHash(const OUString& rSourceUri);
    static bool IsValidStoragePolicy();
    static bool ContainsRawContentFieldName(const OUString& rFieldName);

private:
    OUString m_sWorkspaceHash;
    OUString m_sStorageRootUrl;
    OUString m_sWorkspaceSidecarDirUrl;
    OUString m_sChunkSidecarUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
