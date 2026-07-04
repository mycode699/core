/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V6: AI File Manager).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AIFileManager.
 */

#include "AIFileManager.hxx"

#include <algorithm>
#include <ctime>
#include <osl/file.hxx>
#include <osl/security.hxx>
#include <osl/time.h>
#include <rtl/bootstrap.hxx>
#include <sal/log.hxx>

namespace kqoffice::ai::filemgr
{

namespace
{
sal_Int64 currentTimeMs()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
         + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}
}

AIFileManager::AIFileManager() {}
AIFileManager::~AIFileManager() {}

// ── Scanning ─────────────────────────────────────────────────────────────

ScanResult AIFileManager::scan(const ScanFilter& filter)
{
    ScanResult result;
    sal_Int64 startMs = currentTimeMs();

    std::vector<OUString> roots = filter.scanRoots;
    if (roots.empty())
        roots = commonDirectories();

    for (const auto& root : roots)
    {
        result.scanRoot = root;
        scanRecursive(root, 0, filter, result);
    }

    result.scanDurationMs = currentTimeMs() - startMs;
    SAL_INFO("kqoffice.ai.filemgr",
             "Scan complete: scanned=" << result.totalScanned
                 << " matched=" << result.totalMatched
                 << " duration=" << result.scanDurationMs << "ms");
    return result;
}

ScanResult AIFileManager::quickScan()
{
    ScanFilter filter;
    filter.maxDepth = 3;
    filter.scanRoots = commonDirectories();
    return scan(filter);
}

ScanResult AIFileManager::scanDirectory(const OUString& rootPath, sal_Int32 maxDepth)
{
    ScanFilter filter;
    filter.scanRoots.push_back(rootPath);
    filter.maxDepth = maxDepth;
    return scan(filter);
}

// ── Categorization ───────────────────────────────────────────────────────

FileCategory AIFileManager::categorize(const OUString& extension)
{
    OUString ext = extension.toAsciiLowerCase();

    // Writer formats
    if (ext == "odt" || ext == "doc" || ext == "docx" || ext == "rtf"
        || ext == "txt" || ext == "md" || ext == "markdown" || ext == "fodt")
        return FileCategory::Writer;

    // Calc formats
    if (ext == "ods" || ext == "xls" || ext == "xlsx" || ext == "csv"
        || ext == "tsv" || ext == "fods")
        return FileCategory::Calc;

    // Impress formats
    if (ext == "odp" || ext == "ppt" || ext == "pptx" || ext == "fodp")
        return FileCategory::Impress;

    // PDF
    if (ext == "pdf")
        return FileCategory::PDF;

    // Image formats
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif"
        || ext == "svg" || ext == "bmp" || ext == "webp" || ext == "tiff")
        return FileCategory::Image;

    return FileCategory::Other;
}

OUString AIFileManager::categoryLabel(FileCategory cat)
{
    switch (cat)
    {
        case FileCategory::Writer:  return u"Writer 文档"_ustr;
        case FileCategory::Calc:    return u"Calc 表格"_ustr;
        case FileCategory::Impress: return u"Impress 演示"_ustr;
        case FileCategory::PDF:     return u"PDF 文件"_ustr;
        case FileCategory::Image:   return u"图片"_ustr;
        case FileCategory::Other:   return u"其他"_ustr;
    }
    return u""_ustr;
}

std::vector<OUString> AIFileManager::extensionsFor(FileCategory cat)
{
    switch (cat)
    {
        case FileCategory::Writer:
            return { u"odt"_ustr, u"doc"_ustr, u"docx"_ustr, u"rtf"_ustr,
                     u"txt"_ustr, u"md"_ustr };
        case FileCategory::Calc:
            return { u"ods"_ustr, u"xls"_ustr, u"xlsx"_ustr, u"csv"_ustr, u"tsv"_ustr };
        case FileCategory::Impress:
            return { u"odp"_ustr, u"ppt"_ustr, u"pptx"_ustr };
        case FileCategory::PDF:
            return { u"pdf"_ustr };
        case FileCategory::Image:
            return { u"png"_ustr, u"jpg"_ustr, u"jpeg"_ustr, u"gif"_ustr,
                     u"svg"_ustr, u"bmp"_ustr };
        case FileCategory::Other:
            return {};
    }
    return {};
}

// ── Sorting ──────────────────────────────────────────────────────────────

void AIFileManager::sort(std::vector<FileEntry>& files, SortOrder order)
{
    switch (order)
    {
        case SortOrder::NameAsc:
            std::sort(files.begin(), files.end(),
                [](const FileEntry& a, const FileEntry& b) {
                    return a.name.compareTo(b.name) < 0;
                });
            break;
        case SortOrder::NameDesc:
            std::sort(files.begin(), files.end(),
                [](const FileEntry& a, const FileEntry& b) {
                    return a.name.compareTo(b.name) > 0;
                });
            break;
        case SortOrder::TimeAsc:
            std::sort(files.begin(), files.end(),
                [](const FileEntry& a, const FileEntry& b) {
                    return a.modifiedTime < b.modifiedTime;
                });
            break;
        case SortOrder::TimeDesc:
            std::sort(files.begin(), files.end(),
                [](const FileEntry& a, const FileEntry& b) {
                    return a.modifiedTime > b.modifiedTime;
                });
            break;
        case SortOrder::SizeAsc:
            std::sort(files.begin(), files.end(),
                [](const FileEntry& a, const FileEntry& b) {
                    return a.sizeBytes < b.sizeBytes;
                });
            break;
        case SortOrder::SizeDesc:
            std::sort(files.begin(), files.end(),
                [](const FileEntry& a, const FileEntry& b) {
                    return a.sizeBytes > b.sizeBytes;
                });
            break;
        case SortOrder::TypeAsc:
            std::sort(files.begin(), files.end(),
                [](const FileEntry& a, const FileEntry& b) {
                    if (a.category != b.category)
                        return static_cast<int>(a.category) < static_cast<int>(b.category);
                    return a.name.compareTo(b.name) < 0;
                });
            break;
    }
}

void AIFileManager::groupByCategory(std::vector<FileEntry>& files, SortOrder innerOrder)
{
    sort(files, SortOrder::TypeAsc);
    // Files are now grouped by category. Apply inner sort within each group.
    auto rangeStart = files.begin();
    while (rangeStart != files.end())
    {
        auto rangeEnd = rangeStart;
        while (rangeEnd != files.end() && rangeEnd->category == rangeStart->category)
            ++rangeEnd;

        std::vector<FileEntry> group(rangeStart, rangeEnd);
        sort(group, innerOrder);
        std::copy(group.begin(), group.end(), rangeStart);
        rangeStart = rangeEnd;
    }
}

// ── Navigation index ─────────────────────────────────────────────────────

OUString AIFileManager::generateNavIndex(const std::vector<FileEntry>& files,
                                          const OUString& outputDir)
{
    auto entries = buildNavEntries(files);
    OUString indexPath = outputDir + u"/AI文件导航索引.odt"_ustr;

    // Build the document content as text
    OUString content;
    content += u"AI 文件智能管家 — 导航索引\n"_ustr;
    content += u"生成时间: " + formatTime(currentTimeMs()) + u"\n"_ustr;
    content += u"共 " + OUString::number(static_cast<sal_Int32>(files.size()))
        + u" 个文件\n\n"_ustr;

    // Table of contents by category
    FileCategory lastCat = FileCategory::Other;
    bool firstCat = true;
    for (const auto& cat : {
         FileCategory::Writer, FileCategory::Calc, FileCategory::Impress,
         FileCategory::PDF, FileCategory::Image, FileCategory::Other })
    {
        sal_Int32 count = 0;
        for (const auto& e : entries)
        {
            // Count files for this category
            for (const auto& f : files)
            {
                if (f.category == cat)
                    count++;
            }
        }
        if (count > 0)
        {
            content += u"## " + categoryLabel(cat) + u" (" + OUString::number(count)
                + u")\n"_ustr;

            for (const auto& f : files)
            {
                if (f.category == cat)
                    content += u"- " + f.name + u"  →  " + f.path + u"\n"_ustr;
            }
            content += u"\n"_ustr;
        }
    }

    // Write the content to the output file
    osl::File outFile(indexPath);
    if (outFile.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create) == osl::FileBase::E_None)
    {
        OString utf8Content = OUStringToOString(content, RTL_TEXTENCODING_UTF8);
        sal_uInt64 written = 0;
        outFile.write(utf8Content.getStr(), utf8Content.getLength(), written);
        outFile.close();
    }

    SAL_INFO("kqoffice.ai.filemgr",
             "generateNavIndex: " << indexPath << " entries=" << entries.size());
    return indexPath;
}

std::vector<NavIndexEntry> AIFileManager::buildNavEntries(
    const std::vector<FileEntry>& files)
{
    std::vector<NavIndexEntry> entries;
    for (const auto& f : files)
    {
        NavIndexEntry entry;
        entry.displayName = f.name;
        entry.filePath = f.path;
        entry.categoryLabel = categoryLabel(f.category);
        entry.timeLabel = formatTime(f.modifiedTime);
        entry.sizeLabel = formatSize(f.sizeBytes);
        entry.indentLevel = f.depth;
        entries.push_back(entry);
    }
    return entries;
}

OUString AIFileManager::formatNavLine(const NavIndexEntry& entry)
{
    OUString indent;
    for (sal_Int32 i = 0; i < entry.indentLevel; i++)
        indent += u"  ";
    return indent + u"[" + entry.categoryLabel + u"] " + entry.displayName
        + u"\n  → " + entry.filePath
        + u"\n  " + entry.timeLabel + u" | " + entry.sizeLabel;
}

// ── AI semantic search ───────────────────────────────────────────────────

std::vector<SemanticMatch> AIFileManager::semanticSearch(
    const OUString& query, const std::vector<FileEntry>& pool, sal_Int32 maxResults)
{
    std::vector<SemanticMatch> results;

    for (const auto& f : pool)
    {
        SemanticMatch match;
        match.file = f;

        double score = 0.0;
        OUString reason;

        // Exact name match
        OUString lowerName = f.name.toAsciiLowerCase();
        OUString lowerQuery = query.toAsciiLowerCase();
        if (lowerName.indexOf(lowerQuery) >= 0)
        {
            score += 0.6;
            if (!reason.isEmpty()) reason += u"; ";
            reason += u"文件名匹配"_ustr;
        }

        // Path contains query
        OUString lowerPath = f.path.toAsciiLowerCase();
        if (lowerPath.indexOf(lowerQuery) >= 0 && lowerName.indexOf(lowerQuery) < 0)
        {
            score += 0.3;
            if (!reason.isEmpty()) reason += u"; ";
            reason += u"路径匹配"_ustr;
        }

        // Category match
        OUString catLabel = categoryLabel(f.category).toAsciiLowerCase();
        if (catLabel.indexOf(lowerQuery) >= 0)
        {
            score += 0.2;
            if (!reason.isEmpty()) reason += u"; ";
            reason += u"类型匹配"_ustr;
        }

        // Recent files bonus
        if (f.modifiedTime > currentTimeMs() - 7 * 24 * 3600) // last 7 days
        {
            score += 0.1;
        }

        if (score > 0.0)
        {
            match.relevanceScore = std::min(score, 1.0);
            match.matchReason = reason;
            match.contentSnippet = u"路径: "_ustr + f.path;
            results.push_back(match);
        }
    }

    // Sort by relevance descending
    std::sort(results.begin(), results.end(),
        [](const SemanticMatch& a, const SemanticMatch& b) {
            return a.relevanceScore > b.relevanceScore;
        });

    if (static_cast<sal_Int32>(results.size()) > maxResults)
        results.resize(maxResults);

    SAL_INFO("kqoffice.ai.filemgr",
             "semanticSearch: query=\"" << query
                 << "\" pool=" << pool.size()
                 << " results=" << results.size());
    return results;
}

std::vector<SemanticMatch> AIFileManager::quickSearch(const OUString& query)
{
    auto files = quickScan().files;
    return semanticSearch(query, files, 20);
}

// ── Path utilities ───────────────────────────────────────────────────────

std::vector<OUString> AIFileManager::commonDirectories()
{
    std::vector<OUString> dirs;

    // Get home directory
    OUString homeDir;
    osl::Security().getHomeDir(homeDir);

    dirs.push_back(homeDir + u"/Desktop"_ustr);
    dirs.push_back(homeDir + u"/Documents"_ustr);
    dirs.push_back(homeDir + u"/Downloads"_ustr);
    dirs.push_back(homeDir + u"/可点office"_ustr);

    // Common project directories
    dirs.push_back(u"/Users/lu/kdoffice-src/kqoffice"_ustr);

    return dirs;
}

OUString AIFileManager::formatSize(sal_Int64 bytes)
{
    if (bytes < 1024)
        return OUString::number(bytes) + u" B"_ustr;
    if (bytes < 1024 * 1024)
        return OUString::number(bytes / 1024) + u" KB"_ustr;
    if (bytes < 1024 * 1024 * 1024)
    {
        double mb = static_cast<double>(bytes) / (1024 * 1024);
        return OUString::number(mb) + u" MB"_ustr;
    }
    double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    return OUString::number(gb) + u" GB"_ustr;
}

OUString AIFileManager::formatTime(sal_Int64 epochSeconds)
{
    std::time_t seconds = static_cast<std::time_t>(epochSeconds);
    std::tm timeInfo;
#if defined(_WIN32)
    localtime_s(&timeInfo, &seconds);
#else
    localtime_r(&seconds, &timeInfo);
#endif

    sal_Int32 year = timeInfo.tm_year + 1900;
    sal_Int32 month = timeInfo.tm_mon + 1;
    sal_Int32 day = timeInfo.tm_mday;
    sal_Int32 hour = timeInfo.tm_hour;
    sal_Int32 minute = timeInfo.tm_min;

    OUString result;
    result += OUString::number(year) + u"-"_ustr;
    if (month < 10) result += u"0"_ustr;
    result += OUString::number(month) + u"-"_ustr;
    if (day < 10) result += u"0"_ustr;
    result += OUString::number(day) + u" "_ustr;
    if (hour < 10) result += u"0"_ustr;
    result += OUString::number(hour) + u":"_ustr;
    if (minute < 10) result += u"0"_ustr;
    result += OUString::number(minute);

    return result;
}

OUString AIFileManager::relativePath(const OUString& fullPath,
                                      const OUString& baseDir)
{
    if (fullPath.startsWith(baseDir))
        return fullPath.copy(baseDir.getLength() + 1);
    return fullPath;
}

bool AIFileManager::isSupportedDocument(const OUString& path)
{
    OUString ext;
    sal_Int32 dotPos = path.lastIndexOf('.');
    if (dotPos >= 0)
        ext = path.copy(dotPos + 1).toAsciiLowerCase();

    return !ext.isEmpty() && categorize(ext) != FileCategory::Other;
}

// ── Private ──────────────────────────────────────────────────────────────

void AIFileManager::scanRecursive(const OUString& dirPath, sal_Int32 currentDepth,
                                   const ScanFilter& filter, ScanResult& result)
{
    if (currentDepth > filter.maxDepth)
        return;

    osl::Directory dir(dirPath);
    if (dir.open() != osl::FileBase::E_None)
        return;

    osl::DirectoryItem item;
    while (dir.getNextItem(item) == osl::FileBase::E_None)
    {
        result.totalScanned++;

        osl::FileStatus status(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileName
                               | osl_FileStatus_Mask_FileURL);
        if (item.getFileStatus(status) != osl::FileBase::E_None)
            continue;

        OUString fullPath = dirPath + u"/"_ustr + status.getFileName();

        if (status.getFileType() == osl::FileStatus::Directory)
        {
            // Recurse into subdirectories
            OUString name = status.getFileName();
            if (!filter.includeHidden && name.startsWith("."))
                continue;
            // Skip common non-document directories
            if (name == "node_modules" || name == ".git" || name == ".svn"
                || name == "workdir" || name == "instdir")
                continue;
            scanRecursive(fullPath, currentDepth + 1, filter, result);
            continue;
        }

        if (status.getFileType() != osl::FileStatus::Regular)
            continue;

        // Check file against filter
        if (!matchesFilter(fullPath, filter))
            continue;

        // Get file info
        FileEntry entry = getFileInfo(fullPath);
        entry.depth = currentDepth;
        entry.parentDir = dirPath;

        result.files.push_back(entry);
        result.totalMatched++;
        result.totalSizeBytes += entry.sizeBytes;
    }
    dir.close();
}

bool AIFileManager::matchesFilter(const OUString& path,
                                   const ScanFilter& filter) const
{
    // Extract extension
    OUString ext;
    sal_Int32 dotPos = path.lastIndexOf('.');
    if (dotPos >= 0)
        ext = path.copy(dotPos + 1).toAsciiLowerCase();
    else
        return false; // No extension, skip

    // Check size limit
    if (filter.maxFileSize > 0)
    {
        osl::FileStatus status(osl_FileStatus_Mask_FileSize);
        OUString fileUrl;
        osl::FileBase::getFileURLFromSystemPath(path, fileUrl);
        osl::DirectoryItem item;
        if (osl::DirectoryItem::get(fileUrl, item) == osl::FileBase::E_None
            && item.getFileStatus(status) == osl::FileBase::E_None
            && static_cast<sal_Int64>(status.getFileSize()) > filter.maxFileSize)
        {
            return false;
        }
    }

    // Check category filter
    if (!filter.categories.empty())
    {
        FileCategory cat = categorize(ext);
        bool categoryOk = false;
        for (const auto& allowed : filter.categories)
        {
            if (cat == allowed)
            {
                categoryOk = true;
                break;
            }
        }
        if (!categoryOk)
            return false;
    }

    return true;
}

FileEntry AIFileManager::getFileInfo(const OUString& path)
{
    FileEntry entry;
    entry.path = path;

    // Extract name and extension
    sal_Int32 lastSlash = path.lastIndexOf('/');
    if (lastSlash >= 0)
        entry.name = path.copy(lastSlash + 1);
    else
        entry.name = path;

    sal_Int32 dotPos = entry.name.lastIndexOf('.');
    if (dotPos >= 0)
        entry.extension = entry.name.copy(dotPos + 1).toAsciiLowerCase();

    entry.category = categorize(entry.extension);

    // Get file stats
    osl::FileStatus status(osl_FileStatus_Mask_FileSize
                           | osl_FileStatus_Mask_ModifyTime);
    OUString fileUrl;
    osl::FileBase::getFileURLFromSystemPath(path, fileUrl);
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(fileUrl, item) == osl::FileBase::E_None)
    {
        if (item.getFileStatus(status) == osl::FileBase::E_None)
        {
            entry.sizeBytes = status.getFileSize();
            TimeValue tv = status.getModifyTime();
            entry.modifiedTime = static_cast<sal_Int64>(tv.Seconds);
        }
    }

    return entry;
}

} // namespace kqoffice::ai::filemgr

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
