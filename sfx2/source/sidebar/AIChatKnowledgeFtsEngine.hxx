/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 M9: real local SQLite FTS5).
 *
 * Local-only full-text index for Knowledge Index / document RAG.
 * No public egress. Query text is runtime-only and not written to registry fixtures.
 */

#pragma once

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatKnowledgeFtsHit
{
    OUString ChunkId;
    OUString Position;
    OUString SourceKind;
    OUString EvidenceId;
    OUString TextHash;
    OUString Snippet; ///< local prompt-only snippet (not registry raw payload)
    sal_Int32 Rank = 0;
    sal_Int32 ScoreBasisPoints = 0;
};

struct AIChatKnowledgeFtsIndexResult
{
    bool Success = false;
    sal_Int32 IndexedCount = 0;
    OUString WorkspaceHash;
    OUString Message;
};

struct AIChatKnowledgeFtsSearchResult
{
    bool Success = false;
    OUString WorkspaceHash;
    OUString Backend; ///< sqlite-fts5
    sal_Int32 LatencyMs = 0;
    std::vector<AIChatKnowledgeFtsHit> Hits;
    OUString Message;
};

/// Per-workspace FTS database status (M11 multi-workspace admin).
struct AIChatKnowledgeFtsWorkspaceInfo
{
    OUString WorkspaceHash;
    OUString DbUrl;
    OUString DocSnapshot;
    sal_Int32 ChunkCount = 0;
    sal_Int32 ExternalFileCount = 0;
    OUString LastIndexedAt; ///< ISO-ish local stamp if recorded
    bool Exists = false;
};

/// Result of deleting a local FTS workspace database (M12).
struct AIChatKnowledgeFtsPurgeResult
{
    bool Success = false;
    bool Removed = false;
    OUString WorkspaceHash;
    OUString DbUrl;
    OUString Message;
};

/// Bounded material-path poll watcher status (M12; no per-file FD, no FSEvents daemon).
struct AIChatKnowledgeFtsWatchStatus
{
    sal_Int32 TrackedPathCount = 0;
    sal_Int32 MaxPaths = 256;
    sal_Int32 DebounceMs = 5000;
    sal_Int32 PollIntervalSeconds = 60;
    bool Overflow = false;
    bool PerFileDescriptorWatch = false; ///< always false (contract)
    OUString Strategy; ///< bounded-poll-no-per-file-fd
    OUString LastPollMessage;
    OUString Message;
};

/// SQLite FTS5 engine for local knowledge / open-document / external file search.
class AIChatKnowledgeFtsEngine final
{
public:
    /// Index the currently open Writer/Calc/Impress document (read-only capture).
    /// Skips rebuild when document skeleton snapshot hash is unchanged (M10 incremental).
    static AIChatKnowledgeFtsIndexResult IndexOpenDocument(
        const OUString& rWorkspaceIdentity = OUString());

    /// Force full rebuild even when snapshot hash matches.
    static AIChatKnowledgeFtsIndexResult ForceReindexOpenDocument(
        const OUString& rWorkspaceIdentity = OUString());

    /// Index a local file/folder extract into FTS (mtime-incremental per path hash).
    /// rBody is already-extracted local text; empty body is ignored.
    static AIChatKnowledgeFtsIndexResult IndexExternalText(
        const OUString& rSystemPath, const OUString& rBody,
        const OUString& rWorkspaceIdentity = OUString());

    /// Cheap mtime check: true if path is already indexed at current mtime (skip ReadUtf8).
    static bool IsExternalPathCurrent(const OUString& rSystemPath,
                                      const OUString& rWorkspaceIdentity = OUString());

    /// Index all @文件/@文件夹 materials referenced in a prompt (bounded).
    static AIChatKnowledgeFtsIndexResult IndexMaterialsFromPrompt(
        const OUString& rPrompt, const OUString& rWorkspaceIdentity = OUString());

    /// Search local FTS5 index. rQueryText is runtime-only (not persisted as raw query).
    static AIChatKnowledgeFtsSearchResult Search(const OUString& rQueryText, sal_Int32 nTopK = 6,
                                                 const OUString& rWorkspaceIdentity = OUString());

    /// Build a prompt-ready block from hits (local only).
    static OUString BuildPromptBlock(const AIChatKnowledgeFtsSearchResult& rSearch,
                                     sal_Int32 nMaxChars = 4500);

    /// List known FTS workspaces under the local sidecar root.
    static std::vector<AIChatKnowledgeFtsWorkspaceInfo> ListWorkspaces();

    /// Status for one workspace (current if empty identity).
    static AIChatKnowledgeFtsWorkspaceInfo DescribeWorkspace(
        const OUString& rWorkspaceIdentity = OUString());

    /// Delete local FTS sqlite for a workspace (M12). Empty = current workspace.
    /// rWorkspaceHashOrIdentity: full workspace identity, or short hash prefix (≥8 hex).
    static AIChatKnowledgeFtsPurgeResult PurgeWorkspace(
        const OUString& rWorkspaceHashOrIdentity = OUString());

    /// Register material system paths for bounded poll watch (no held FDs; cap 256).
    static AIChatKnowledgeFtsIndexResult RegisterWatchPaths(
        const std::vector<OUString>& rSystemPaths,
        const OUString& rWorkspaceIdentity = OUString());

    /// Register @文件/@文件夹 paths from a prompt into the watch list.
    static AIChatKnowledgeFtsIndexResult RegisterWatchFromPrompt(
        const OUString& rPrompt, const OUString& rWorkspaceIdentity = OUString());

    /// Poll watched paths; reindex when mtime changes. Debounced 5s unless bForce.
    static AIChatKnowledgeFtsIndexResult PollWatchedPaths(
        const OUString& rWorkspaceIdentity = OUString(), bool bForce = false);

    /// Clear in-process watch list (does not delete FTS databases).
    static AIChatKnowledgeFtsIndexResult ClearWatchList();

    static AIChatKnowledgeFtsWatchStatus DescribeWatchStatus();

    /// Human-readable multi-line status for slash / UI.
    static OUString FormatWorkspaceStatusZh(const OUString& rWorkspaceIdentity = OUString());

    /// Human-readable multi-workspace listing for slash / UI.
    static OUString FormatWorkspaceListZh();

    /// Human-readable purge result for slash / UI.
    static OUString FormatPurgeResultZh(const AIChatKnowledgeFtsPurgeResult& rResult);

    /// Human-readable watch status for slash / UI.
    static OUString FormatWatchStatusZh();

    static OUString MakeWorkspaceHash(const OUString& rWorkspaceIdentity);
    static OUString ResolveDbUrl(const OUString& rWorkspaceHash);
    static OUString ResolveStorageRootUrl();
    static bool IsSqliteAvailable();
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
