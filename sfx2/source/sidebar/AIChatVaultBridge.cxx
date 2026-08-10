/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "AIChatVaultBridge.hxx"
#include "AIChatKnowledgeFtsEngine.hxx"

#include <AiResourceEnvelope.hxx>
#include <VaultStore.hxx>
#include <VaultIngest.hxx>
#include <VaultManager.hxx>

#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
OUString ReadUtf8(const OUString& rSys, sal_Int32 nMax)
{
    if (nMax <= 0)
        nMax = kqoffice::ai::control::AiResourceEnvelope::maxVaultIndexCharsPerFile();
    const OString p = OUStringToOString(rSys, RTL_TEXTENCODING_UTF8);
    std::ifstream in(p.getStr(), std::ios::binary);
    if (!in)
        return {};
    // Cap read size early to bound IO.
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<sal_Int64>(in.tellg());
    in.seekg(0, std::ios::beg);
    if (sz <= 0)
        return {};
    const size_t toRead = static_cast<size_t>(
        std::min<sal_Int64>(sz, static_cast<sal_Int64>(nMax)));
    std::string s(toRead, '\0');
    in.read(s.data(), static_cast<std::streamsize>(toRead));
    return OStringToOUString(std::string_view(s.data(), s.size()), RTL_TEXTENCODING_UTF8);
}

std::vector<OUString> ListImportFiles()
{
    std::vector<OUString> out;
    using kqoffice::ai::vault::VaultStore;
    if (!VaultStore::ensureLayout())
        return out;
    const auto paths = VaultStore::pathsFor(OUString());
    const OUString dir = paths.raw + u"/imports"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) != osl::FileBase::E_None)
        return out;
    osl::Directory d(url);
    if (d.open() != osl::FileBase::E_None)
        return out;
    for (;;)
    {
        osl::DirectoryItem item;
        if (d.getNextItem(item) != osl::FileBase::E_None)
            break;
        osl::FileStatus st(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileURL);
        if (item.getFileStatus(st) != osl::FileBase::E_None)
            continue;
        if (st.getFileType() != osl::FileStatus::Regular)
            continue;
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(st.getFileURL(), sys) == osl::FileBase::E_None)
            out.push_back(sys);
    }
    d.close();
    return out;
}

} // namespace

OUString AIChatVaultWorkspaceId()
{
    using kqoffice::ai::vault::VaultStore;
    VaultStore::ensureLayout();
    const auto p = VaultStore::pathsFor(OUString());
    // Persist a stable id once
    const OUString idPath = p.internalState + u"/fts-workspace-id"_ustr;
    OUString existing = ReadUtf8(idPath, 200);
    existing = existing.trim();
    if (!existing.isEmpty())
        return existing;
    const OUString id = u"kq-vault-main"_ustr;
    // write
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(idPath, url) == osl::FileBase::E_None)
    {
        osl::File f(url);
        auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
        if (e != osl::FileBase::E_None)
            e = f.open(osl_File_OpenFlag_Write);
        if (e == osl::FileBase::E_None)
        {
            f.setSize(0);
            const OString b = OUStringToOString(id, RTL_TEXTENCODING_UTF8);
            sal_uInt64 n = 0;
            f.write(b.getStr(), b.getLength(), n);
            f.close();
        }
    }
    return id;
}

AIChatVaultIndexResult AIChatVaultIndexPath(const OUString& rSystemPath, const OUString& rBody)
{
    AIChatVaultIndexResult r;
    if (rSystemPath.isEmpty() || rBody.isEmpty())
    {
        r.MessageZh = u"无内容可索引"_ustr;
        return r;
    }
    const auto fr = AIChatKnowledgeFtsEngine::IndexExternalText(rSystemPath, rBody,
                                                                AIChatVaultWorkspaceId());
    r.Success = fr.Success;
    r.Indexed = fr.IndexedCount;
    r.MessageZh = fr.Success ? (u"已索引资料盘条目 · chunks="_ustr + OUString::number(fr.IndexedCount))
                            : (fr.Message.isEmpty() ? u"索引失败"_ustr : fr.Message);
    return r;
}

sal_Int32 AIChatVaultImportFileCount()
{
    return static_cast<sal_Int32>(ListImportFiles().size());
}

namespace
{
AIChatVaultIndexResult ReindexOnePass(const std::vector<OUString>& files, sal_Int32 passNo)
{
    AIChatVaultIndexResult r;
    const sal_Int32 maxFiles
        = kqoffice::ai::control::AiResourceEnvelope::maxVaultIndexFilesPerPass();
    const sal_Int32 maxChars
        = kqoffice::ai::control::AiResourceEnvelope::maxVaultIndexCharsPerFile();
    const OUString ws = AIChatVaultWorkspaceId();
    sal_Int32 n = 0;
    sal_Int32 considered = 0;
    sal_Int32 skipped = 0;
    sal_Int32 dirtySeen = 0;
    for (const auto& f : files)
    {
        // Cheap skip: already indexed at current mtime — no ReadUtf8.
        if (AIChatKnowledgeFtsEngine::IsExternalPathCurrent(f, ws))
        {
            ++skipped;
            continue;
        }
        if (considered >= maxFiles)
        {
            r.MoreRemaining = true;
            break;
        }
        ++considered;
        ++dirtySeen;
        const OUString body = ReadUtf8(f, maxChars);
        if (body.isEmpty())
            continue;
        const auto one = AIChatVaultIndexPath(f, body);
        if (one.Success)
            n += one.Indexed > 0 ? one.Indexed : 1;
    }
    r.Success = true;
    r.Indexed = n;
    r.FilesConsidered = considered;
    r.FilesSkippedCurrent = skipped;
    r.FilesTotal = static_cast<sal_Int32>(files.size());
    r.Passes = passNo;
    if (!r.MoreRemaining && dirtySeen == 0 && considered == 0)
        r.MoreRemaining = false;
    return r;
}
} // namespace

AIChatVaultIndexResult AIChatVaultReindexAll()
{
    AIChatVaultIndexResult r;
    using kqoffice::ai::vault::VaultStore;
    if (!VaultStore::ensureLayout())
    {
        r.MessageZh = u"无法创建资料盘"_ustr;
        return r;
    }
    const auto files = ListImportFiles();
    r = ReindexOnePass(files, 1);
    std::vector<OUString> watch;
    watch.push_back(VaultStore::pathsFor(OUString()).raw + u"/imports"_ustr);
    (void)AIChatKnowledgeFtsEngine::RegisterWatchPaths(watch, AIChatVaultWorkspaceId());

    OUStringBuffer msg;
    msg.append(u"资料盘索引 · 第 1 轮 · 写入 "_ustr);
    msg.append(r.Indexed);
    msg.append(u" · 扫描新/变文件 "_ustr);
    msg.append(r.FilesConsidered);
    msg.append(u" · 已是最新跳过 "_ustr);
    msg.append(r.FilesSkippedCurrent);
    msg.append(u" / 共 "_ustr);
    msg.append(r.FilesTotal);
    msg.append(u" · "_ustr);
    msg.append(kqoffice::ai::control::AiResourceEnvelope::summaryLineZh());
    msg.append(u" · 主文档未改"_ustr);
    if (r.MoreRemaining)
        msg.append(u"\n\n还有待索引文件 — 再执行 `/资料盘重建索引` 或 `/资料盘全量索引` 继续。"_ustr);
    else
        msg.append(u"\n\n本轮后资料盘检索已是最新。"_ustr);
    r.MessageZh = msg.makeStringAndClear();
    return r;
}

AIChatVaultIndexResult AIChatVaultReindexAllPasses(sal_Int32 nMaxPasses)
{
    AIChatVaultIndexResult total;
    using kqoffice::ai::vault::VaultStore;
    if (!VaultStore::ensureLayout())
    {
        total.MessageZh = u"无法创建资料盘"_ustr;
        return total;
    }
    if (nMaxPasses < 1)
        nMaxPasses = 1;
    if (nMaxPasses > 32)
        nMaxPasses = 32;

    const auto files = ListImportFiles();
    sal_Int32 pass = 0;
    sal_Int32 indexedSum = 0;
    sal_Int32 consideredSum = 0;
    bool more = false;
    for (pass = 1; pass <= nMaxPasses; ++pass)
    {
        const auto one = ReindexOnePass(files, pass);
        indexedSum += one.Indexed;
        consideredSum += one.FilesConsidered;
        total.FilesSkippedCurrent = one.FilesSkippedCurrent;
        total.FilesTotal = one.FilesTotal;
        more = one.MoreRemaining;
        if (!more || one.FilesConsidered == 0)
        {
            more = false;
            break;
        }
    }
    std::vector<OUString> watch;
    watch.push_back(VaultStore::pathsFor(OUString()).raw + u"/imports"_ustr);
    (void)AIChatKnowledgeFtsEngine::RegisterWatchPaths(watch, AIChatVaultWorkspaceId());

    total.Success = true;
    total.Indexed = indexedSum;
    total.FilesConsidered = consideredSum;
    total.Passes = pass;
    total.MoreRemaining = more;

    // Progress bar (text): filled by completed file share.
    const sal_Int32 totalFiles = total.FilesTotal > 0 ? total.FilesTotal : 1;
    const sal_Int32 doneApprox
        = std::min(totalFiles, total.FilesSkippedCurrent
                                   + std::max<sal_Int32>(0, totalFiles - (more ? totalFiles / 4 : 0)));
    // Prefer: skipped (current) + if no more remaining => 100%
    sal_Int32 pct = 100;
    if (more && totalFiles > 0)
    {
        // After multipass, remaining dirty ≈ files not current; approximate via last skip count.
        pct = static_cast<sal_Int32>(
            (static_cast<sal_Int64>(total.FilesSkippedCurrent) * 100) / totalFiles);
        if (pct > 99)
            pct = 99;
        if (pct < 1 && consideredSum > 0)
            pct = 1;
    }
    else if (totalFiles > 0)
    {
        pct = 100;
        (void)doneApprox;
    }
    const sal_Int32 bars = 12;
    const sal_Int32 filled = (pct * bars) / 100;
    OUStringBuffer bar;
    bar.append(u"["_ustr);
    for (sal_Int32 i = 0; i < bars; ++i)
        bar.append(i < filled ? u"█"_ustr : u"░"_ustr);
    bar.append(u"] "_ustr);
    bar.append(pct);
    bar.append(u"%"_ustr);

    OUStringBuffer msg;
    msg.append(u"## 资料盘全量索引\n\n"_ustr);
    msg.append(bar.makeStringAndClear());
    msg.append(u"\n\n- 轮次：**"_ustr);
    msg.append(total.Passes);
    msg.append(u"** / 上限 "_ustr);
    msg.append(nMaxPasses);
    msg.append(u"\n- 本会话写入贡献：**"_ustr);
    msg.append(indexedSum);
    msg.append(u"**\n- 处理变文件：**"_ustr);
    msg.append(consideredSum);
    msg.append(u"** · 已是最新：**"_ustr);
    msg.append(total.FilesSkippedCurrent);
    msg.append(u"** / 共 **"_ustr);
    msg.append(total.FilesTotal);
    msg.append(u"**\n- "_ustr);
    msg.append(kqoffice::ai::control::AiResourceEnvelope::summaryLineZh());
    msg.append(u"\n- 主文档未改\n"_ustr);
    if (more)
        msg.append(u"\n仍有待索引 — 再执行 `/资料盘全量索引` 继续（分轮保护 IO）。\n"_ustr);
    else
        msg.append(u"\n检索索引已对齐磁盘内容。\n"_ustr);
    total.MessageZh = msg.makeStringAndClear();
    return total;
}

AIChatVaultSearchResult AIChatVaultSearch(const OUString& rQuery, sal_Int32 nTopK)
{
    AIChatVaultSearchResult r;
    r.WorkspaceId = AIChatVaultWorkspaceId();
    if (rQuery.trim().isEmpty())
    {
        r.MessageZh = u"请输入关键词"_ustr;
        return r;
    }
    const sal_Int32 cap = kqoffice::ai::control::AiResourceEnvelope::maxFtsSearchTopK();
    if (nTopK <= 0 || nTopK > cap)
        nTopK = cap;
    const auto sr = AIChatKnowledgeFtsEngine::Search(rQuery, nTopK, r.WorkspaceId);
    r.Success = sr.Success;
    r.MessageZh = sr.Message;
    for (const auto& h : sr.Hits)
    {
        AIChatVaultHit vh;
        vh.Title = h.Position.isEmpty() ? h.ChunkId : h.Position;
        vh.Snippet = h.Snippet;
        vh.Path = h.Position;
        vh.Rank = h.Rank;
        r.Hits.push_back(vh);
    }
    r.PromptBlock = AIChatKnowledgeFtsEngine::BuildPromptBlock(sr, 4500);
    if (r.Success && r.Hits.empty())
        r.MessageZh = u"资料盘无命中 · 可先收入材料或 /资料盘重建索引"_ustr;
    else if (r.Success)
        r.MessageZh = u"资料盘命中 "_ustr + OUString::number(static_cast<sal_Int32>(r.Hits.size()));
    return r;
}

AIChatVaultSearchResult AIChatVaultRelated(const OUString& rSeedQuery, sal_Int32 nTopK)
{
    OUString q = rSeedQuery.trim();
    if (q.getLength() > 80)
        q = q.copy(0, 80);
    if (q.isEmpty())
        q = u"资料 摘要 要点"_ustr;
    return AIChatVaultSearch(q, nTopK);
}

OUString AIChatVaultStatusLineZh()
{
    using kqoffice::ai::vault::VaultStore;
    using kqoffice::ai::vault::VaultIngest;
    using kqoffice::ai::vault::VaultManager;
    VaultStore::ensureLayout();
    const auto recent = VaultIngest::listRecentTitles(OUString(), 5);
    const auto ws = AIChatKnowledgeFtsEngine::DescribeWorkspace(AIChatVaultWorkspaceId());
    OUStringBuffer b;
    b.append(VaultManager::statusChipZh());
    b.append(u" · 最近 "_ustr);
    b.append(static_cast<sal_Int32>(recent.size()));
    b.append(u" · chunks≈"_ustr);
    b.append(ws.ChunkCount);
    return b.makeStringAndClear();
}

OUString AIChatVaultDashboardZh()
{
    using kqoffice::ai::vault::VaultManager;
    using kqoffice::ai::vault::VaultStore;
    using kqoffice::ai::vault::VaultIngest;

    OUStringBuffer b;
    b.append(VaultManager::managementSummaryZh());
    b.append(u"\n---\n\n## 索引与资源\n\n"_ustr);
    const sal_Int32 nFiles = AIChatVaultImportFileCount();
    const auto ws = AIChatKnowledgeFtsEngine::DescribeWorkspace(AIChatVaultWorkspaceId());
    const auto recent = VaultIngest::listRecentTitles(OUString(), 8);
    b.append(u"- imports 文件数：**"_ustr);
    b.append(nFiles);
    b.append(u"**\n- FTS chunks≈**"_ustr);
    b.append(ws.ChunkCount);
    b.append(u"**\n- "_ustr);
    b.append(kqoffice::ai::control::AiResourceEnvelope::summaryLineZh());
    b.append(u"\n\n**快捷**\n"_ustr);
    b.append(u"- `/资料盘重建索引` — 单轮（有界）\n"_ustr);
    b.append(u"- `/资料盘全量索引` — 多轮直到对齐或达上限\n"_ustr);
    b.append(u"- `/收入资料 /路径` · `/搜资料 关键词` · `/整理资料`\n"_ustr);
    if (!recent.empty())
    {
        b.append(u"\n**最近收录**\n"_ustr);
        for (const auto& t : recent)
        {
            b.append(u"- "_ustr);
            b.append(t);
            b.append(u"\n"_ustr);
        }
    }
    (void)VaultStore::ensureLayout();
    return b.makeStringAndClear();
}

} // namespace sfx2::sidebar
