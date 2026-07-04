/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V6: AI File Manager).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AIFileSearchUI — formats file search results for chat panel display.
 */

#include "AIFileSearchUI.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::filemgr
{

OUString AIFileSearchUI::formatScanSummary(const ScanResult& result)
{
    return u"扫描完成: "_ustr
        + OUString::number(result.totalMatched) + u" 个文件, "
        + AIFileManager::formatSize(result.totalSizeBytes)
        + u" (" + OUString::number(result.scanDurationMs) + u"ms)";
}

OUString AIFileSearchUI::formatSearchResults(const std::vector<SemanticMatch>& results)
{
    OUStringBuffer buf;
    buf.append(u"找到 "_ustr);
    buf.append(OUString::number(static_cast<sal_Int32>(results.size())));
    buf.append(u" 个匹配文件:\n\n"_ustr);

    for (size_t i = 0; i < results.size(); i++)
    {
        const auto& r = results[i];
        OUString scoreBar = formatScoreBar(r.relevanceScore);

        buf.append(OUString::number(static_cast<sal_Int32>(i + 1)));
        buf.append(u". "_ustr);
        buf.append(scoreBar);
        buf.append(u" "_ustr);
        buf.append(r.file.name);
        buf.append(u"\n   "_ustr);
        buf.append(AIFileManager::categoryLabel(r.file.category));
        buf.append(u" | "_ustr);
        buf.append(AIFileManager::formatSize(r.file.sizeBytes));
        buf.append(u" | "_ustr);
        buf.append(AIFileManager::formatTime(r.file.modifiedTime));
        buf.append(u"\n   路径: "_ustr);
        buf.append(r.file.path);
        buf.append(u"\n   匹配: "_ustr);
        buf.append(r.matchReason);
        buf.append(u"\n\n"_ustr);
    }

    return buf.makeStringAndClear();
}

OUString AIFileSearchUI::formatFileList(const std::vector<FileEntry>& files,
                                        SortOrder order)
{
    FileListFormatOptions opts;
    opts.sortOrder = order;
    return formatFileList(files, opts);
}

OUString AIFileSearchUI::formatFileList(const std::vector<FileEntry>& files,
                                        const FileListFormatOptions& opts)
{
    std::vector<FileEntry> sorted = files;
    AIFileManager::sort(sorted, opts.sortOrder);

    // Build directory tree view or flat list
    OUStringBuffer buf;

    if (opts.groupByCategory)
    {
        std::vector<FileEntry> grouped = files;
        AIFileManager::groupByCategory(grouped, opts.sortOrder);

        FileCategory lastCat = FileCategory::Other;
        bool firstCat = true;

        for (const auto& f : grouped)
        {
            if (!firstCat && f.category != lastCat)
                buf.append(u"\n"_ustr);
            firstCat = false;

            if (f.category != lastCat)
            {
                buf.append(u"## "_ustr);
                buf.append(AIFileManager::categoryLabel(f.category));
                buf.append(u"\n"_ustr);
                lastCat = f.category;
            }

            sal_Int32 idx = opts.showPath ? 0 : f.path.lastIndexOf('/');
            OUString displayPath = opts.showPath
                ? f.path
                : (idx >= 0 ? f.path.copy(0, idx) : u""_ustr);

            buf.append(u"- "_ustr);
            buf.append(f.name);
            buf.append(u"  ("_ustr);
            buf.append(AIFileManager::formatSize(f.sizeBytes));
            buf.append(u" | "_ustr);
            buf.append(AIFileManager::formatTime(f.modifiedTime));
            buf.append(u")\n"_ustr);

            if (opts.showPath)
            {
                buf.append(u"  → "_ustr);
                buf.append(displayPath);
                buf.append(u"\n"_ustr);
            }
        }
    }
    else
    {
        for (const auto& f : sorted)
        {
            buf.append(u"- "_ustr);
            buf.append(f.name);
            buf.append(u"  | "_ustr);
            buf.append(AIFileManager::categoryLabel(f.category));
            buf.append(u" | "_ustr);
            buf.append(AIFileManager::formatSize(f.sizeBytes));
            buf.append(u" | "_ustr);
            buf.append(AIFileManager::formatTime(f.modifiedTime));
            buf.append(u"\n"_ustr);
        }
    }

    return buf.makeStringAndClear();
}

OUString AIFileSearchUI::formatScoreBar(double score)
{
    sal_Int32 filled = static_cast<sal_Int32>(score * 5);
    OUString result;
    for (sal_Int32 i = 0; i < filled; i++)
        result += u"★"_ustr;
    for (sal_Int32 i = filled; i < 5; i++)
        result += u"☆"_ustr;
    return result;
}

} // namespace kqoffice::ai::filemgr

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
