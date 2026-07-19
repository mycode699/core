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

#include "PermissionCenter.hxx"

#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <utility>
#include <vector>
#include <osl/file.hxx>
#include <osl/security.hxx>
#include <osl/time.h>
#include <rtl/bootstrap.hxx>
#include <sal/log.hxx>

namespace kqoffice::ai::filemgr
{

namespace
{
OUString toFileUrl(const OUString& systemOrUrl)
{
    if (systemOrUrl.startsWith("file://"))
        return systemOrUrl;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemOrUrl, url) == osl::FileBase::E_None
        && !url.isEmpty())
        return url;
    return systemOrUrl;
}

sal_Int64 currentTimeMs()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
         + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

OUString projectKeyFromPath(const OUString& path)
{
    sal_Int32 slash = path.lastIndexOf('/');
    if (slash <= 0)
        return u""_ustr;
    OUString parent = path.copy(0, slash);
    sal_Int32 pslash = parent.lastIndexOf('/');
    if (pslash < 0)
        return parent;
    return parent.copy(pslash + 1);
}

bool writeTextFile(const OUString& path, const OUString& content)
{
    const OUString fileUrl = toFileUrl(path);
    osl::File::remove(fileUrl);
    osl::File out(fileUrl);
    if (out.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create) != osl::FileBase::E_None)
        return false;
    const OString utf8 = OUStringToOString(content, RTL_TEXTENCODING_UTF8);
    sal_uInt64 written = 0;
    out.write(utf8.getStr(), utf8.getLength(), written);
    out.close();
    return written > 0;
}

bool readTextFile(const OUString& path, OUString& outContent)
{
    osl::File file(toFileUrl(path));
    if (file.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    sal_uInt64 size = 0;
    file.getSize(size);
    std::vector<char> buf(static_cast<size_t>(size) + 1, 0);
    sal_uInt64 read = 0;
    file.read(buf.data(), size, read);
    file.close();
    outContent = OUString(buf.data(), static_cast<sal_Int32>(read), RTL_TEXTENCODING_UTF8);
    return true;
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
    kqoffice::ai::control::PermissionCenter perms;

    if (roots.empty())
    {
        // Never default to full-disk. Prefer authorized roots; otherwise no-op
        // until the user grants directories in Permission Center.
        if (perms.hasAnyAuthorizedDirectory())
        {
            for (const auto& d : perms.authorizedDirectories())
                roots.push_back(d.path);
        }
        else if (!filter.requireAuthorizedRoots)
        {
            roots = commonDirectories();
        }
    }

    for (const auto& root : roots)
    {
        if (filter.cancelFlag && filter.cancelFlag->load())
            break;
        if (filter.requireAuthorizedRoots && !perms.isPathAuthorized(root))
        {
            SAL_INFO("kqoffice.ai.filemgr",
                     "Skip unauthorized scan root: " << root);
            continue;
        }
        result.scanRoot = root;
        scanRecursive(root, 0, filter, result);
    }

    applyWorkbenchMetadata(result.files);

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
    // Authorized-only by default. Empty authorized set => empty result (safe).
    filter.requireAuthorizedRoots = true;
    return scan(filter);
}

ScanResult AIFileManager::scanDirectory(const OUString& rootPath, sal_Int32 maxDepth)
{
    ScanFilter filter;
    filter.scanRoots.push_back(rootPath);
    filter.maxDepth = maxDepth;
    filter.requireAuthorizedRoots = true;
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
    OUString indexPath = outputDir + u"/AI文件导航索引.odt"_ustr;

    // Build the document content as text
    OUString content;
    content += u"AI 文件智能管家 — 导航索引\n"_ustr;
    content += u"生成时间: " + formatTime(currentTimeMs() / 1000) + u"\n"_ustr;
    content += u"共 " + OUString::number(static_cast<sal_Int32>(files.size()))
        + u" 个文件\n\n"_ustr;

    // Table of contents by category.
    for (const auto& cat : {
         FileCategory::Writer, FileCategory::Calc, FileCategory::Impress,
         FileCategory::PDF, FileCategory::Image, FileCategory::Other })
    {
        sal_Int32 count = 0;
        for (const auto& f : files)
        {
            if (f.category == cat)
                count++;
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
    osl::File outFile(toFileUrl(indexPath));
    if (outFile.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create) == osl::FileBase::E_None)
    {
        OString utf8Content = OUStringToOString(content, RTL_TEXTENCODING_UTF8);
        sal_uInt64 written = 0;
        outFile.write(utf8Content.getStr(), utf8Content.getLength(), written);
        outFile.close();
    }

    SAL_INFO("kqoffice.ai.filemgr",
             "generateNavIndex: " << indexPath << " entries=" << files.size());
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
        if (f.modifiedTime > currentTimeMs() / 1000 - 7 * 24 * 3600) // last 7 days
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
    // Candidates only — product scanning still requires PermissionCenter grant.
    // Never include developer trees, full home, or full disk.
    std::vector<OUString> dirs;

    OUString homeDir;
    osl::Security().getHomeDir(homeDir);
    if (homeDir.isEmpty())
        return dirs;

    dirs.push_back(homeDir + u"/Desktop"_ustr);
    dirs.push_back(homeDir + u"/Documents"_ustr);
    dirs.push_back(homeDir + u"/Downloads"_ustr);
    return dirs;
}

OUString AIFileManager::workbenchStoreDir()
{
    if (const char* env = std::getenv("KQOFFICE_AI_FILEMGR_DIR"))
    {
        if (env[0] != '\0')
            return OUString::createFromAscii(env);
    }
    osl::Security security;
    OUString configDir;
    if (security.getConfigDir(configDir) && !configDir.isEmpty())
        return configDir + u"/KQOffice/AI/file-workbench"_ustr;

    OUString homeDir;
    if (security.getHomeDir(homeDir) && !homeDir.isEmpty())
        return homeDir + u"/.kqoffice/ai/file-workbench"_ustr;

    return u".kqoffice/ai/file-workbench"_ustr;
}

void AIFileManager::loadWorkbenchMeta()
{
    if (m_metaLoaded)
        return;
    m_metaLoaded = true;
    m_meta.clear();

    OUString content;
    if (!readTextFile(workbenchStoreDir() + u"/meta.tsv"_ustr, content))
        return;

    sal_Int32 from = 0;
    while (from <= content.getLength())
    {
        sal_Int32 to = content.indexOf('\n', from);
        if (to < 0)
            to = content.getLength();
        OUString line = content.copy(from, to - from).trim();
        from = to + 1;
        if (line.isEmpty() || line.startsWith("#"))
            continue;
        // path\tpinned\tfavorite\ttag
        const sal_Int32 p1 = line.indexOf('\t');
        if (p1 < 0)
            continue;
        const sal_Int32 p2 = line.indexOf('\t', p1 + 1);
        const sal_Int32 p3 = (p2 < 0) ? -1 : line.indexOf('\t', p2 + 1);
        MetaFlags flags;
        OUString path = line.copy(0, p1);
        flags.pinned = (p2 >= 0) && line.copy(p1 + 1, p2 - p1 - 1) == u"1"_ustr;
        flags.favorite = (p3 >= 0) && line.copy(p2 + 1, p3 - p2 - 1) == u"1"_ustr;
        if (p3 >= 0)
            flags.tag = line.copy(p3 + 1);
        m_meta.emplace_back(path, flags);
    }
}

void AIFileManager::saveWorkbenchMeta() const
{
    OUString dir = workbenchStoreDir();
    osl::Directory::createPath(toFileUrl(dir));
    OUString body = u"# path\tpinned\tfavorite\ttag\n"_ustr;
    for (const auto& item : m_meta)
    {
        body += item.first + u"\t"_ustr
            + (item.second.pinned ? u"1"_ustr : u"0"_ustr) + u"\t"_ustr
            + (item.second.favorite ? u"1"_ustr : u"0"_ustr) + u"\t"_ustr
            + item.second.tag + u"\n"_ustr;
    }
    writeTextFile(dir + u"/meta.tsv"_ustr, body);
}

bool AIFileManager::pin(const OUString& path, bool pinned)
{
    loadWorkbenchMeta();
    for (auto& item : m_meta)
    {
        if (item.first == path)
        {
            item.second.pinned = pinned;
            saveWorkbenchMeta();
            return true;
        }
    }
    MetaFlags flags;
    flags.pinned = pinned;
    m_meta.emplace_back(path, flags);
    saveWorkbenchMeta();
    return true;
}

bool AIFileManager::favorite(const OUString& path, bool favorited)
{
    loadWorkbenchMeta();
    for (auto& item : m_meta)
    {
        if (item.first == path)
        {
            item.second.favorite = favorited;
            saveWorkbenchMeta();
            return true;
        }
    }
    MetaFlags flags;
    flags.favorite = favorited;
    m_meta.emplace_back(path, flags);
    saveWorkbenchMeta();
    return true;
}

bool AIFileManager::setTag(const OUString& path, const OUString& tag)
{
    loadWorkbenchMeta();
    for (auto& item : m_meta)
    {
        if (item.first == path)
        {
            item.second.tag = tag;
            saveWorkbenchMeta();
            return true;
        }
    }
    MetaFlags flags;
    flags.tag = tag;
    m_meta.emplace_back(path, flags);
    saveWorkbenchMeta();
    return true;
}

bool AIFileManager::isPinned(const OUString& path) const
{
    const_cast<AIFileManager*>(this)->loadWorkbenchMeta();
    for (const auto& item : m_meta)
        if (item.first == path)
            return item.second.pinned;
    return false;
}

bool AIFileManager::isFavorite(const OUString& path) const
{
    const_cast<AIFileManager*>(this)->loadWorkbenchMeta();
    for (const auto& item : m_meta)
        if (item.first == path)
            return item.second.favorite;
    return false;
}

OUString AIFileManager::tagOf(const OUString& path) const
{
    const_cast<AIFileManager*>(this)->loadWorkbenchMeta();
    for (const auto& item : m_meta)
        if (item.first == path)
            return item.second.tag;
    return u""_ustr;
}

void AIFileManager::applyWorkbenchMetadata(std::vector<FileEntry>& files) const
{
    const_cast<AIFileManager*>(this)->loadWorkbenchMeta();
    for (auto& f : files)
    {
        f.projectKey = projectKeyFromPath(f.path);
        for (const auto& item : m_meta)
        {
            if (item.first == f.path)
            {
                f.pinned = item.second.pinned;
                f.favorite = item.second.favorite;
                f.tag = item.second.tag;
                break;
            }
        }
    }
    // Pinned first, then favorites, then original order.
    std::stable_sort(files.begin(), files.end(),
        [](const FileEntry& a, const FileEntry& b) {
            if (a.pinned != b.pinned)
                return a.pinned && !b.pinned;
            if (a.favorite != b.favorite)
                return a.favorite && !b.favorite;
            return false;
        });
}

void AIFileManager::groupByProject(std::vector<FileEntry>& files)
{
    std::sort(files.begin(), files.end(),
        [](const FileEntry& a, const FileEntry& b) {
            const int cmp = a.projectKey.compareTo(b.projectKey);
            if (cmp != 0)
                return cmp < 0;
            return a.name.compareTo(b.name) < 0;
        });
}

bool AIFileManager::moveToTrash(const OUString& path, const OUString& evidenceNote,
                                kqoffice::ai::control::PermissionDecision confirm)
{
    if (path.isEmpty())
        return false;

    kqoffice::ai::control::PermissionCenter perms;
    if (!perms.isPathAuthorized(path))
    {
        SAL_WARN("kqoffice.ai.filemgr",
                 "moveToTrash blocked — path not in authorized workspace: " << path);
        return false;
    }
    if (!perms.resolveRiskyOp(kqoffice::ai::control::RiskOperation::Delete, path, confirm))
    {
        SAL_INFO("kqoffice.ai.filemgr",
                 "moveToTrash needs 拒绝/本次/本轮 confirmation: " << path);
        return false;
    }

    const OUString trashDir = workbenchStoreDir() + u"/trash"_ustr;
    osl::Directory::createPath(toFileUrl(trashDir));

    sal_Int32 slash = path.lastIndexOf('/');
    OUString name = (slash >= 0) ? path.copy(slash + 1) : path;
    OUString trashPath = trashDir + u"/"_ustr + OUString::number(currentTimeMs())
        + u"_"_ustr + name;

    if (osl::File::move(toFileUrl(path), toFileUrl(trashPath)) != osl::FileBase::E_None)
        return false;

    OUString line = path + u"\t"_ustr + trashPath + u"\t"_ustr
        + OUString::number(currentTimeMs()) + u"\t"_ustr + evidenceNote + u"\n"_ustr;
    OUString logPath = trashDir + u"/trash.tsv"_ustr;
    OUString existing;
    readTextFile(logPath, existing);
    writeTextFile(logPath, existing + line);
    SAL_INFO("kqoffice.ai.filemgr",
             "moveToTrash: " << path << " -> " << trashPath
                 << " note=" << evidenceNote);
    return true;
}

bool AIFileManager::writeAuthorizedFile(const OUString& path, const OUString& content,
                                        kqoffice::ai::control::PermissionDecision confirm)
{
    if (path.isEmpty())
        return false;

    kqoffice::ai::control::PermissionCenter perms;
    if (!perms.isPathAuthorized(path))
    {
        SAL_WARN("kqoffice.ai.filemgr",
                 "writeAuthorizedFile blocked — path not in authorized workspace: " << path);
        return false;
    }

    // Create or overwrite under authorized root always requires risk resolve
    // so agent write paths never silently materialize / clobber files.
    if (!perms.resolveRiskyOp(kqoffice::ai::control::RiskOperation::Overwrite, path, confirm))
    {
        SAL_INFO("kqoffice.ai.filemgr",
                 "writeAuthorizedFile needs 拒绝/本次/本轮 confirmation: " << path);
        return false;
    }

    // Ensure parent directory exists under the authorized tree.
    sal_Int32 slash = path.lastIndexOf('/');
    if (slash > 0)
    {
        const OUString parent = path.copy(0, slash);
        osl::Directory::createPath(toFileUrl(parent));
    }
    if (!writeTextFile(path, content))
        return false;
    SAL_INFO("kqoffice.ai.filemgr", "writeAuthorizedFile ok: " << path);
    return true;
}

bool AIFileManager::restoreFromTrash(const OUString& originalPath)
{
    const OUString logPath = workbenchStoreDir() + u"/trash/trash.tsv"_ustr;
    OUString content;
    if (!readTextFile(logPath, content))
        return false;

    OUString rebuilt;
    bool restored = false;
    sal_Int32 from = 0;
    while (from <= content.getLength())
    {
        sal_Int32 to = content.indexOf('\n', from);
        if (to < 0)
            to = content.getLength();
        OUString line = content.copy(from, to - from);
        from = to + 1;
        if (line.trim().isEmpty())
            continue;
        const sal_Int32 p1 = line.indexOf('\t');
        const sal_Int32 p2 = (p1 < 0) ? -1 : line.indexOf('\t', p1 + 1);
        if (p1 < 0 || p2 < 0)
        {
            rebuilt += line + u"\n"_ustr;
            continue;
        }
        OUString orig = line.copy(0, p1);
        OUString trashPath = line.copy(p1 + 1, p2 - p1 - 1);
        if (!restored && orig == originalPath)
        {
            if (osl::File::move(toFileUrl(trashPath), toFileUrl(originalPath)) == osl::FileBase::E_None)
            {
                restored = true;
                continue; // drop from trash log
            }
        }
        rebuilt += line + u"\n"_ustr;
    }
    if (restored)
        writeTextFile(logPath, rebuilt);
    return restored;
}

std::vector<TrashEntry> AIFileManager::listTrash() const
{
    std::vector<TrashEntry> out;
    OUString content;
    if (!readTextFile(workbenchStoreDir() + u"/trash/trash.tsv"_ustr, content))
        return out;

    sal_Int32 from = 0;
    while (from <= content.getLength())
    {
        sal_Int32 to = content.indexOf('\n', from);
        if (to < 0)
            to = content.getLength();
        OUString line = content.copy(from, to - from).trim();
        from = to + 1;
        if (line.isEmpty())
            continue;
        const sal_Int32 p1 = line.indexOf('\t');
        const sal_Int32 p2 = (p1 < 0) ? -1 : line.indexOf('\t', p1 + 1);
        const sal_Int32 p3 = (p2 < 0) ? -1 : line.indexOf('\t', p2 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0)
            continue;
        TrashEntry e;
        e.originalPath = line.copy(0, p1);
        e.trashPath = line.copy(p1 + 1, p2 - p1 - 1);
        e.deletedAtMs = line.copy(p2 + 1, p3 - p2 - 1).toInt64();
        e.evidenceNote = line.copy(p3 + 1);
        out.push_back(e);
    }
    return out;
}

bool AIFileManager::createLocalSnapshot(const OUString& path, const OUString& label)
{
    if (path.isEmpty())
        return false;
    const OUString snapDir = workbenchStoreDir() + u"/snapshots"_ustr;
    osl::Directory::createPath(toFileUrl(snapDir));

    sal_Int32 slash = path.lastIndexOf('/');
    OUString name = (slash >= 0) ? path.copy(slash + 1) : path;
    OUString snapPath = snapDir + u"/"_ustr + OUString::number(currentTimeMs())
        + u"_"_ustr + name;

    if (osl::File::copy(toFileUrl(path), toFileUrl(snapPath)) != osl::FileBase::E_None)
        return false;

    OUString line = path + u"\t"_ustr + snapPath + u"\t"_ustr
        + OUString::number(currentTimeMs()) + u"\t"_ustr + label + u"\n"_ustr;
    OUString logPath = snapDir + u"/snapshots.tsv"_ustr;
    OUString existing;
    readTextFile(logPath, existing);
    writeTextFile(logPath, existing + line);
    return true;
}

std::vector<LocalSnapshot> AIFileManager::listSnapshots(const OUString& path) const
{
    std::vector<LocalSnapshot> out;
    OUString content;
    if (!readTextFile(workbenchStoreDir() + u"/snapshots/snapshots.tsv"_ustr, content))
        return out;

    sal_Int32 from = 0;
    while (from <= content.getLength())
    {
        sal_Int32 to = content.indexOf('\n', from);
        if (to < 0)
            to = content.getLength();
        OUString line = content.copy(from, to - from).trim();
        from = to + 1;
        if (line.isEmpty())
            continue;
        const sal_Int32 p1 = line.indexOf('\t');
        const sal_Int32 p2 = (p1 < 0) ? -1 : line.indexOf('\t', p1 + 1);
        const sal_Int32 p3 = (p2 < 0) ? -1 : line.indexOf('\t', p2 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0)
            continue;
        LocalSnapshot s;
        s.sourcePath = line.copy(0, p1);
        if (!path.isEmpty() && s.sourcePath != path)
            continue;
        s.snapshotPath = line.copy(p1 + 1, p2 - p1 - 1);
        s.createdAtMs = line.copy(p2 + 1, p3 - p2 - 1).toInt64();
        s.label = line.copy(p3 + 1);
        out.push_back(s);
    }
    return out;
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
    if (filter.cancelFlag && filter.cancelFlag->load())
        return;
    if (currentDepth > filter.maxDepth)
        return;

    // osl::Directory requires a file:// URL on macOS/Windows.
    const OUString dirUrl = toFileUrl(dirPath);
    osl::Directory dir(dirUrl);
    if (dir.open() != osl::FileBase::E_None)
        return;

    osl::DirectoryItem item;
    while (dir.getNextItem(item) == osl::FileBase::E_None)
    {
        if (filter.cancelFlag && filter.cancelFlag->load())
            break;
        result.totalScanned++;

        osl::FileStatus status(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileName
                               | osl_FileStatus_Mask_FileURL
                               | osl_FileStatus_Mask_LinkTargetURL);
        if (item.getFileStatus(status) != osl::FileBase::E_None)
            continue;

        OUString fullUrl = status.getFileURL();
        if (fullUrl.isEmpty())
            fullUrl = dirUrl + u"/"_ustr + status.getFileName();

        // Prefer system path for product-facing entries (open/pin/display).
        OUString fullPath;
        if (osl::FileBase::getSystemPathFromFileURL(fullUrl, fullPath) != osl::FileBase::E_None
            || fullPath.isEmpty())
            fullPath = fullUrl;

        if (status.isLink() && !filter.followSymlinks)
            continue;

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
            scanRecursive(fullUrl, currentDepth + 1, filter, result);
            continue;
        }

        if (status.getFileType() != osl::FileStatus::Regular)
            continue;

        // Check file against filter
        if (!matchesFilter(fullPath, filter) && !matchesFilter(fullUrl, filter))
            continue;

        // Get file info via file URL (reliable with osl), store system path.
        FileEntry entry = getFileInfo(fullUrl);
        if (entry.path.isEmpty() || entry.path.startsWith("file://"))
            entry.path = fullPath;
        entry.depth = currentDepth;
        entry.parentDir = dirPath.startsWith("file://") ? fullPath.copy(0, fullPath.lastIndexOf('/'))
                                                        : dirPath;

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

    // Extract name and extension (works for system path or file URL).
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
    const OUString fileUrl = toFileUrl(path);
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
