/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "VaultCompile.hxx"
#include "VaultStore.hxx"

#include <AiResourceEnvelope.hxx>
#include <ModelRoutingConfig.hxx>
#include <OpenAICompatibleAdapter.hxx>

#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
// std::min used with maxCompileItemsPerPass

namespace kqoffice::ai::vault
{
namespace
{
bool WriteUtf8(const OUString& rSys, const OUString& rBody)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    const sal_Int32 slash = rSys.lastIndexOf('/');
    if (slash > 0)
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(rSys.copy(0, slash), dirUrl)
            == osl::FileBase::E_None)
            (void)osl::Directory::createPath(dirUrl);
    }
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString b = OUStringToOString(rBody, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(b.getStr(), b.getLength(), n);
    f.close();
    return true;
}

OUString ReadUtf8File(const OUString& rSys, sal_Int32 nMax = 24000)
{
    const OString path = OUStringToOString(rSys, RTL_TEXTENCODING_UTF8);
    std::ifstream in(path.getStr(), std::ios::binary);
    if (!in)
        return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (static_cast<sal_Int32>(s.size()) > nMax)
        s.resize(static_cast<size_t>(nMax));
    return OStringToOUString(std::string_view(s.data(), s.size()), RTL_TEXTENCODING_UTF8);
}

OUString BaseName(const OUString& p)
{
    const sal_Int32 s = p.lastIndexOf('/');
    return s >= 0 ? p.copy(s + 1) : p;
}

std::vector<OUString> ListImportSnippets(const OUString& rImportsDir)
{
    std::vector<OUString> out;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rImportsDir, url) != osl::FileBase::E_None)
        return out;
    osl::Directory dir(url);
    if (dir.open() != osl::FileBase::E_None)
        return out;
    for (;;)
    {
        osl::DirectoryItem item;
        if (dir.getNextItem(item) != osl::FileBase::E_None)
            break;
        osl::FileStatus st(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileName
                           | osl_FileStatus_Mask_FileURL);
        if (item.getFileStatus(st) != osl::FileBase::E_None)
            continue;
        if (st.getFileType() != osl::FileStatus::Regular)
            continue;
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(st.getFileURL(), sys) != osl::FileBase::E_None)
            continue;
        if (sys.endsWith(u".txt"_ustr) || sys.endsWith(u".md"_ustr))
            out.push_back(sys);
    }
    dir.close();
    return out;
}

OUString SourcePagePath(const VaultPaths& vp, const OUString& rImportBase)
{
    return vp.wiki + u"/sources/"_ustr + rImportBase + u".md"_ustr;
}

bool FileExistsSys(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    f.close();
    return true;
}

void AppendLog(const VaultPaths& vp, const OUString& rLine)
{
    const OUString logPath = vp.wiki + u"/log.md"_ustr;
    OUString cur = ReadUtf8File(logPath, 200000);
    if (cur.isEmpty())
        cur = u"# 资料盘日志\n\n"_ustr;
    cur += rLine + u"\n"_ustr;
    WriteUtf8(logPath, cur);
}

void RebuildIndex(const VaultPaths& vp, const std::vector<OUString>& rSourceTitles)
{
    OUStringBuffer b;
    b.append(u"# 资料盘目录\n\n## 来源摘要\n\n"_ustr);
    for (const auto& t : rSourceTitles)
    {
        b.append(u"- "_ustr);
        b.append(t);
        b.append(u"\n"_ustr);
    }
    b.append(u"\n## 主题\n\n见 `concepts/`。\n"_ustr);
    WriteUtf8(vp.wiki + u"/index.md"_ustr, b.makeStringAndClear());
}

OUString LocalStubSummary(const OUString& rTitle, const OUString& rBody, const OUString& rRawPath)
{
    OUString clip = rBody;
    if (clip.getLength() > 1200)
        clip = clip.copy(0, 1200) + u"…"_ustr;
    OUStringBuffer b;
    b.append(u"# "_ustr);
    b.append(rTitle);
    b.append(u"\n\n> 来源：`"_ustr);
    b.append(rRawPath);
    b.append(u"`\n\n## 摘要（本地摘录）\n\n"_ustr);
    b.append(clip);
    b.append(u"\n\n## 概念候选\n\n- （待整理）\n"_ustr);
    return b.makeStringAndClear();
}

OUString TryLlmSummary(const OUString& rTitle, const OUString& rBody)
{
    const auto snap = kqoffice::ai::loadModelRoutingSnapshot();
    OUString base = snap.baseUrl;
    if (base.isEmpty())
        base = u"https://api.03122.com"_ustr;
    const OUString key = kqoffice::ai::OpenAICompatibleAdapter::apiKeyFromEnv();
    if (key.isEmpty())
        return {};
    kqoffice::ai::OpenAICompatibleAdapter ad(base, key);
    OUString model = snap.primaryModel;
    if (model.isEmpty() || model == u"auto"_ustr)
        model = u"gpt-4o-mini"_ustr;
    OUString body = rBody;
    const sal_Int32 maxEx
        = kqoffice::ai::control::AiResourceEnvelope::maxCompileExcerptChars();
    if (body.getLength() > maxEx)
        body = body.copy(0, maxEx);
    const OUString prompt
        = u"你是资料盘整理助手。根据以下材料写中文 Markdown：\n"
          u"1) 标题行 # 标题\n2) ## 摘要（不超过200字）\n3) ## 要点（3-6条）\n"
          u"4) ## 概念（3-8个词，逗号分隔）\n"
          u"不要编造材料中没有的事实。材料标题："_ustr
          + rTitle + u"\n\n---\n"_ustr + body;
    return ad.chat(model, prompt);
}

void TouchConcepts(const VaultPaths& vp, const OUString& rConceptsCsv, sal_Int32& rTouched)
{
    if (rConceptsCsv.isEmpty())
        return;
    OUString s = rConceptsCsv;
    // split by comma / Chinese comma
    sal_Int32 start = 0;
    while (start < s.getLength() && rTouched < 12)
    {
        sal_Int32 comma = s.indexOf(u',', start);
        sal_Int32 comma2 = s.indexOf(u'，', start);
        sal_Int32 cut = -1;
        if (comma >= 0 && comma2 >= 0)
            cut = std::min(comma, comma2);
        else
            cut = comma >= 0 ? comma : comma2;
        OUString tok = (cut < 0 ? s.copy(start) : s.copy(start, cut - start)).trim();
        if (cut < 0)
            start = s.getLength();
        else
            start = cut + 1;
        if (tok.isEmpty() || tok.getLength() > 40)
            continue;
        // sanitize filename
        OUString safe = tok;
        safe = safe.replaceAll(u"/"_ustr, u"-"_ustr).replaceAll(u" "_ustr, u"_"_ustr);
        const OUString path = vp.wiki + u"/concepts/"_ustr + safe + u".md"_ustr;
        if (!FileExistsSys(path))
        {
            WriteUtf8(path, u"# "_ustr + tok + u"\n\n相关来源见 sources/。\n"_ustr);
            ++rTouched;
        }
    }
}
} // namespace

VaultCompileResult VaultCompile::compilePending(const OUString& rVaultRoot, sal_Int32 nMax)
{
    VaultCompileResult r;
    if (!VaultStore::ensureLayout(rVaultRoot))
    {
        r.messageZh = u"资料盘未就绪"_ustr;
        return r;
    }
    const VaultPaths vp = VaultStore::pathsFor(rVaultRoot);
    const auto files = ListImportSnippets(vp.raw + u"/imports"_ustr);
    const sal_Int32 envLim = kqoffice::ai::control::AiResourceEnvelope::maxCompileItemsPerPass();
    const sal_Int32 lim = nMax > 0 ? std::min(nMax, envLim) : envLim;
    std::vector<OUString> titles;
    for (const auto& f : files)
    {
        if (r.processed >= lim)
            break;
        const OUString base = BaseName(f);
        OUString stem = base;
        const sal_Int32 dot = stem.lastIndexOf('.');
        if (dot > 0)
            stem = stem.copy(0, dot);
        const OUString srcPage = SourcePagePath(vp, stem);
        if (FileExistsSys(srcPage))
            continue; // already compiled
        const OUString body = ReadUtf8File(f);
        if (body.isEmpty())
            continue;
        OUString title = stem;
        if (body.startsWith(u"# "_ustr))
        {
            const sal_Int32 nl = body.indexOf(u'\n');
            title = (nl > 2 ? body.copy(2, nl - 2) : body.copy(2)).trim();
        }
        OUString page = TryLlmSummary(title, body);
        if (page.isEmpty())
            page = LocalStubSummary(title, body, f);
        // ensure cite
        if (page.indexOf(f) < 0 && page.indexOf(u"来源"_ustr) < 0)
            page += u"\n\n> 来源：`"_ustr + f + u"`\n"_ustr;
        if (!WriteUtf8(srcPage, page))
            continue;
        ++r.processed;
        ++r.sourcesWritten;
        titles.push_back(title);

        // crude concept line extract
        const sal_Int32 ci = page.indexOf(u"## 概念"_ustr);
        if (ci >= 0)
        {
            sal_Int32 lineStart = page.indexOf(u'\n', ci);
            if (lineStart >= 0)
            {
                sal_Int32 lineEnd = page.indexOf(u'\n', lineStart + 1);
                OUString line = lineEnd > lineStart ? page.copy(lineStart + 1, lineEnd - lineStart - 1)
                                                    : page.copy(lineStart + 1);
                line = line.replaceAll(u"-"_ustr, u""_ustr).trim();
                TouchConcepts(vp, line, r.conceptsTouched);
            }
        }

        AppendLog(vp, u"## ingest | "_ustr + title + u" | "_ustr + stem);
    }

    // rebuild index from sources dir
    std::vector<OUString> allTitles = titles;
    const OUString srcDir = vp.wiki + u"/sources"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(srcDir, url) == osl::FileBase::E_None)
    {
        osl::Directory dir(url);
        if (dir.open() == osl::FileBase::E_None)
        {
            for (;;)
            {
                osl::DirectoryItem item;
                if (dir.getNextItem(item) != osl::FileBase::E_None)
                    break;
                osl::FileStatus st(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileName);
                if (item.getFileStatus(st) != osl::FileBase::E_None)
                    continue;
                if (st.getFileType() == osl::FileStatus::Regular)
                    allTitles.push_back(st.getFileName());
            }
            dir.close();
        }
    }
    RebuildIndex(vp, allTitles);

    r.ok = true;
    if (r.processed == 0)
        r.messageZh = u"没有待整理的新材料（或均已整理）"_ustr;
    else
    {
        OUStringBuffer msg;
        msg.append(u"已整理 "_ustr);
        msg.append(r.processed);
        msg.append(u" 份 · 来源页 "_ustr);
        msg.append(r.sourcesWritten);
        msg.append(u" · 新主题 "_ustr);
        msg.append(r.conceptsTouched);
        r.messageZh = msg.makeStringAndClear();
    }
    return r;
}

bool VaultCompile::isAutoCompileEnabled(const OUString& rVaultRoot)
{
    const VaultPaths vp = VaultStore::pathsFor(rVaultRoot);
    const OUString s = ReadUtf8File(vp.stateJson, 4000);
    return s.indexOf(u"\"autoCompile\":true"_ustr) >= 0 || s.indexOf(u"\"autoCompile\": true"_ustr) >= 0;
}

bool VaultCompile::setAutoCompileEnabled(bool bOn, const OUString& rVaultRoot)
{
    if (!VaultStore::ensureLayout(rVaultRoot))
        return false;
    const VaultPaths vp = VaultStore::pathsFor(rVaultRoot);
    const OUString body
        = bOn ? u"{\"schemaVersion\":\"vault-state/0.1\",\"autoCompile\":true,"
                u"\"ftsIndexOnIngest\":true,\"officeLargeFile\":\"link\"}\n"_ustr
              : u"{\"schemaVersion\":\"vault-state/0.1\",\"autoCompile\":false,"
                u"\"ftsIndexOnIngest\":true,\"officeLargeFile\":\"link\"}\n"_ustr;
    return WriteUtf8(vp.stateJson, body);
}

} // namespace kqoffice::ai::vault
