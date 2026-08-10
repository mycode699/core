/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "AIChatVaultBridge.hxx"
#include "AIChatKnowledgeFtsEngine.hxx"

#include <VaultStore.hxx>
#include <VaultIngest.hxx>

#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>

#include <fstream>
#include <string>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
OUString ReadUtf8(const OUString& rSys, sal_Int32 nMax = 48000)
{
    const OString p = OUStringToOString(rSys, RTL_TEXTENCODING_UTF8);
    std::ifstream in(p.getStr(), std::ios::binary);
    if (!in)
        return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (static_cast<sal_Int32>(s.size()) > nMax)
        s.resize(static_cast<size_t>(nMax));
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
    sal_Int32 n = 0;
    for (const auto& f : files)
    {
        const OUString body = ReadUtf8(f);
        if (body.isEmpty())
            continue;
        const auto one = AIChatVaultIndexPath(f, body);
        if (one.Success)
            n += one.Indexed > 0 ? one.Indexed : 1;
    }
    // watch imports dir
    std::vector<OUString> watch;
    watch.push_back(VaultStore::pathsFor(OUString()).raw + u"/imports"_ustr);
    (void)AIChatKnowledgeFtsEngine::RegisterWatchPaths(watch, AIChatVaultWorkspaceId());

    r.Success = true;
    r.Indexed = n;
    r.MessageZh = u"资料盘已重建索引 · 条目贡献="_ustr + OUString::number(n) + u" · 主文档未改"_ustr;
    return r;
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
    VaultStore::ensureLayout();
    const auto recent = VaultIngest::listRecentTitles(OUString(), 5);
    const auto ws = AIChatKnowledgeFtsEngine::DescribeWorkspace(AIChatVaultWorkspaceId());
    OUStringBuffer b;
    b.append(u"资料盘 · 最近收录 "_ustr);
    b.append(static_cast<sal_Int32>(recent.size()));
    b.append(u" · FTS chunks≈"_ustr);
    b.append(ws.ChunkCount);
    b.append(u" · 主文档未改"_ustr);
    return b.makeStringAndClear();
}

} // namespace sfx2::sidebar
