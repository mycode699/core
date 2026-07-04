/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V6: AI File Manager).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AIFileSearchUI — display formatting helpers for file search results.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_FILEMGR_AIFILESEARCHUI_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_FILEMGR_AIFILESEARCHUI_HXX

#include "AIFileManager.hxx"

#include <rtl/ustring.hxx>

namespace kqoffice::ai::filemgr
{

/// Display options for file list formatting.
struct FileListFormatOptions
{
    SortOrder sortOrder = SortOrder::TimeDesc;
    bool groupByCategory = true;
    bool showPath = true;
    sal_Int32 maxFiles = 100;
};

class SAL_DLLPUBLIC_EXPORT AIFileSearchUI
{
public:
    /// Format a scan result as a summary line.
    static OUString formatScanSummary(const ScanResult& result);

    /// Format semantic search results for chat display.
    static OUString formatSearchResults(const std::vector<SemanticMatch>& results);

    /// Format a file list for chat display (default options).
    static OUString formatFileList(const std::vector<FileEntry>& files,
                                   SortOrder order = SortOrder::TimeDesc);

    /// Format a file list with full options.
    static OUString formatFileList(const std::vector<FileEntry>& files,
                                   const FileListFormatOptions& opts);

    /// Format a relevance score as a star bar: ★★★★☆
    static OUString formatScoreBar(double score);
};

} // namespace kqoffice::ai::filemgr

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
