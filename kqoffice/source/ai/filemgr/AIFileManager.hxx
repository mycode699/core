/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V6: AI File Manager).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AIFileManager — full-disk file scanning, categorization, sorting,
 * navigation index generation, and AI semantic search.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_FILEMGR_AIFILEMANAGER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_FILEMGR_AIFILEMANAGER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <atomic>
#include <vector>

#include "PermissionCenter.hxx"

namespace kqoffice::ai::filemgr
{

/// Supported document file extensions for scanning.
enum class FileCategory : sal_uInt8
{
    Writer = 0,    // .odt .doc .docx .rtf .txt .md
    Calc,          // .ods .xls .xlsx .csv .tsv
    Impress,       // .odp .ppt .pptx
    PDF,           // .pdf
    Image,         // .png .jpg .jpeg .gif .svg .bmp
    Other,         // everything else
};

/// A single scanned file entry.
struct FileEntry
{
    OUString path;              // Full absolute path
    OUString name;              // File name only
    OUString extension;         // Lowercase extension without dot
    FileCategory category = FileCategory::Other;
    sal_Int64 sizeBytes = 0;    // File size
    sal_Int64 modifiedTime = 0; // Last modified timestamp (epoch seconds)
    sal_Int64 createdTime = 0;  // Creation timestamp
    OUString parentDir;         // Parent directory path
    sal_Int32 depth = 0;        // Directory depth from scan root
    bool pinned = false;        // User pin / 置顶
    bool favorite = false;      // User favorite / 收藏
    OUString tag;               // Single primary tag (P1 expands to multi-tag)
    OUString projectKey;        // Project aggregation key (parent dir name)
};

/// Sort order for file listing.
enum class SortOrder : sal_uInt8
{
    NameAsc = 0,
    NameDesc,
    TimeAsc,        // Oldest first
    TimeDesc,       // Newest first
    SizeAsc,
    SizeDesc,
    TypeAsc,        // Group by category, then name
};

/// Filter for file scanning.
struct ScanFilter
{
    std::vector<OUString> scanRoots;       // Directories to scan (empty = authorized/common)
    std::vector<FileCategory> categories;  // Categories to include (empty = all)
    sal_Int32 maxDepth = 5;               // Max directory depth
    sal_Int64 maxFileSize = 100 * 1024 * 1024; // 100MB max per file
    bool followSymlinks = false;
    bool includeHidden = false;
    /// Optional cancellation flag for background scans. The caller owns it.
    const std::atomic_bool* cancelFlag = nullptr;
    /// When true (default), refuse to scan roots that are not user-authorized.
    /// Full-disk scanning is never the product default.
    bool requireAuthorizedRoots = true;
};

/// Soft-delete (trash) entry — never hard-delete from product actions.
struct TrashEntry
{
    OUString originalPath;
    OUString trashPath;
    sal_Int64 deletedAtMs = 0;
    OUString evidenceNote; // why / who triggered
};

/// Local version snapshot metadata (content lives beside the file).
struct LocalSnapshot
{
    OUString sourcePath;
    OUString snapshotPath;
    sal_Int64 createdAtMs = 0;
    OUString label;
};

/// Result of a file scan.
struct ScanResult
{
    std::vector<FileEntry> files;
    sal_Int32 totalScanned = 0;
    sal_Int32 totalMatched = 0;
    sal_Int64 totalSizeBytes = 0;
    sal_Int64 scanDurationMs = 0;
    OUString scanRoot;
};

/// A navigation index entry for the generated quick-access file.
struct NavIndexEntry
{
    OUString displayName;       // Human-readable name
    OUString filePath;          // Clickable path
    OUString categoryLabel;     // "Writer", "Calc", etc.
    OUString timeLabel;         // "2026-06-24 15:30"
    OUString sizeLabel;         // "2.3 MB"
    sal_Int32 indentLevel = 0;  // For directory grouping
};

/// AI semantic search result.
struct SemanticMatch
{
    FileEntry file;
    double relevanceScore = 0.0; // 0.0 - 1.0
    OUString matchReason;        // Why this file matched
    OUString contentSnippet;     // Matching content preview
};

class SAL_DLLPUBLIC_EXPORT AIFileManager
{
public:
    AIFileManager();
    ~AIFileManager();

    // ── Scanning ─────────────────────────────────────────────────────

    /// Scan files matching the given filter. Returns aggregated results.
    ScanResult scan(const ScanFilter& filter);

    /// Quick scan of common document directories (Desktop, Documents, Downloads).
    ScanResult quickScan();

    /// Scan a single directory recursively.
    ScanResult scanDirectory(const OUString& rootPath, sal_Int32 maxDepth = 3);

    // ── Categorization ───────────────────────────────────────────────

    /// Categorize a file by its extension.
    static FileCategory categorize(const OUString& extension);

    /// Get a human-readable label for a category.
    static OUString categoryLabel(FileCategory cat);

    /// Get all known extensions for a category.
    static std::vector<OUString> extensionsFor(FileCategory cat);

    // ── Sorting ──────────────────────────────────────────────────────

    /// Sort file entries by the given order.
    static void sort(std::vector<FileEntry>& files, SortOrder order);

    /// Group files by category, then sort within each group.
    static void groupByCategory(std::vector<FileEntry>& files, SortOrder innerOrder);

    // ── Navigation index ─────────────────────────────────────────────

    /// Generate a navigation index file (Writer document) from scanned files.
    /// Returns the path to the generated .odt file.
    OUString generateNavIndex(const std::vector<FileEntry>& files,
                              const OUString& outputDir);

    /// Build navigation index entries from file entries.
    static std::vector<NavIndexEntry> buildNavEntries(
        const std::vector<FileEntry>& files);

    /// Format a NavIndexEntry as a display line.
    static OUString formatNavLine(const NavIndexEntry& entry);

    // ── AI semantic search ───────────────────────────────────────────

    /// Search files by natural language query using AI.
    /// Matches against file names, paths, and optionally content snippets.
    std::vector<SemanticMatch> semanticSearch(const OUString& query,
                                               const std::vector<FileEntry>& pool,
                                               sal_Int32 maxResults = 20);

    /// Quick AI search across common directories.
    std::vector<SemanticMatch> quickSearch(const OUString& query);

    // ── Path utilities ───────────────────────────────────────────────

    /// Suggest common document directories (Desktop/Documents/Downloads only).
    /// These are *candidates* — scanning still requires authorization.
    static std::vector<OUString> commonDirectories();

    /// Format a file size as human-readable string.
    static OUString formatSize(sal_Int64 bytes);

    /// Format a timestamp as human-readable string.
    static OUString formatTime(sal_Int64 epochSeconds);

    /// Get the relative path from a base directory.
    static OUString relativePath(const OUString& fullPath,
                                 const OUString& baseDir);

    /// Check if a path is a supported document file.
    static bool isSupportedDocument(const OUString& path);

    // ── Workbench metadata (pin / favorite / tag / project) ──────────

    bool pin(const OUString& path, bool pinned = true);
    bool favorite(const OUString& path, bool favorited = true);
    bool setTag(const OUString& path, const OUString& tag);
    bool isPinned(const OUString& path) const;
    bool isFavorite(const OUString& path) const;
    OUString tagOf(const OUString& path) const;

    /// Annotate scanned entries with pin/favorite/tag and project key.
    void applyWorkbenchMetadata(std::vector<FileEntry>& files) const;

    /// Group files by projectKey (parent directory name).
    static void groupByProject(std::vector<FileEntry>& files);

    // ── Soft delete + local snapshots ────────────────────────────────

    /// Move to product trash (never hard-delete). Records evidence note.
    /// Path must be under an authorized workspace; requires DuMate-like
    /// risk confirmation (拒绝/本次/本轮) unless a 本轮 grant already covers delete.
    bool moveToTrash(const OUString& path, const OUString& evidenceNote,
                     kqoffice::ai::control::PermissionDecision confirm);
    bool restoreFromTrash(const OUString& originalPath);
    std::vector<TrashEntry> listTrash() const;

    /// Overwrite an existing authorized file (or create if missing under root).
    /// Requires risk confirmation unless 本轮 already allows overwrite.
    bool writeAuthorizedFile(const OUString& path, const OUString& content,
                             kqoffice::ai::control::PermissionDecision confirm);

    /// Create a local sidecar snapshot before destructive edits.
    bool createLocalSnapshot(const OUString& path, const OUString& label);
    std::vector<LocalSnapshot> listSnapshots(const OUString& path) const;

    static OUString workbenchStoreDir();

private:
    /// Internal recursive directory scanner.
    void scanRecursive(const OUString& dirPath, sal_Int32 currentDepth,
                       const ScanFilter& filter, ScanResult& result);

    /// Match a file against the scan filter.
    bool matchesFilter(const OUString& path, const ScanFilter& filter) const;

    /// Get file metadata (size, timestamps).
    static FileEntry getFileInfo(const OUString& path);

    /// Load/save pin-favorite-tag TSV under workbench store.
    void loadWorkbenchMeta();
    void saveWorkbenchMeta() const;

    struct MetaFlags
    {
        bool pinned = false;
        bool favorite = false;
        OUString tag;
    };

    mutable bool m_metaLoaded = false;
    mutable std::vector<std::pair<OUString, MetaFlags>> m_meta;
};

} // namespace kqoffice::ai::filemgr

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
