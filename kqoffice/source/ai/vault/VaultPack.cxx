/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "VaultPack.hxx"
#include "VaultStore.hxx"

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/ustrbuf.hxx>

#include <fstream>
#include <string>
#include <vector>

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
        OUString d;
        if (osl::FileBase::getFileURLFromSystemPath(rSys.copy(0, slash), d) == osl::FileBase::E_None)
            (void)osl::Directory::createPath(d);
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

OUString ReadUtf8(const OUString& rSys, sal_Int32 nMax = 20000)
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

void CopyTextFile(const OUString& rFrom, const OUString& rTo)
{
    const OUString body = ReadUtf8(rFrom);
    if (!body.isEmpty())
        WriteUtf8(rTo, body);
}

std::vector<OUString> ListMd(const OUString& rDir)
{
    std::vector<OUString> out;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rDir, url) != osl::FileBase::E_None)
        return out;
    osl::Directory dir(url);
    if (dir.open() != osl::FileBase::E_None)
        return out;
    for (;;)
    {
        osl::DirectoryItem item;
        if (dir.getNextItem(item) != osl::FileBase::E_None)
            break;
        osl::FileStatus st(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileURL
                           | osl_FileStatus_Mask_FileName);
        if (item.getFileStatus(st) != osl::FileBase::E_None)
            continue;
        if (st.getFileType() != osl::FileStatus::Regular)
            continue;
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(st.getFileURL(), sys) != osl::FileBase::E_None)
            continue;
        if (sys.endsWith(u".md"_ustr))
            out.push_back(sys);
    }
    dir.close();
    return out;
}

OUString BaseName(const OUString& p)
{
    const sal_Int32 s = p.lastIndexOf('/');
    return s >= 0 ? p.copy(s + 1) : p;
}
} // namespace

VaultPackResult VaultPack::exportThemePack(const OUString& rTitle, const OUString& rVaultRoot)
{
    VaultPackResult r;
    if (!VaultStore::ensureLayout(rVaultRoot))
    {
        r.messageZh = u"资料盘未就绪"_ustr;
        return r;
    }
    const VaultPaths vp = VaultStore::pathsFor(rVaultRoot);
    const OUString title = rTitle.isEmpty() ? u"资料包"_ustr : rTitle;
    TimeValue tv{};
    osl_getSystemTime(&tv);
    const OUString id = u"vpack-"_ustr + OUString::number(static_cast<sal_Int64>(tv.Seconds), 16);
    OUString safe = title;
    safe = safe.replaceAll(u"/"_ustr, u"-"_ustr).replaceAll(u" "_ustr, u"_"_ustr);
    if (safe.getLength() > 40)
        safe = safe.copy(0, 40);
    r.packDir = vp.outputs + u"/packs/"_ustr + id + u"-"_ustr + safe;
    OUString dirUrl;
    if (osl::FileBase::getFileURLFromSystemPath(r.packDir, dirUrl) != osl::FileBase::E_None
        || osl::Directory::createPath(dirUrl) != osl::FileBase::E_None)
    {
        // createPath returns EXIST ok sometimes
        (void)osl::Directory::createPath(dirUrl);
    }

    CopyTextFile(vp.wiki + u"/index.md"_ustr, r.packDir + u"/index.md"_ustr);
    const auto sources = ListMd(vp.wiki + u"/sources"_ustr);
    const auto concepts = ListMd(vp.wiki + u"/concepts"_ustr);
    sal_Int32 n = 0;
    for (const auto& s : sources)
    {
        if (n >= 40)
            break;
        CopyTextFile(s, r.packDir + u"/sources-"_ustr + BaseName(s));
        ++n;
    }
    for (const auto& c : concepts)
    {
        if (n >= 80)
            break;
        CopyTextFile(c, r.packDir + u"/concept-"_ustr + BaseName(c));
        ++n;
    }

    OUStringBuffer man;
    man.append(u"{\n  \"id\": \""_ustr);
    man.append(id);
    man.append(u"\",\n  \"schemaVersion\": \"vault-pack-manifest/0.1\",\n"_ustr);
    man.append(u"  \"title\": \""_ustr);
    man.append(title);
    man.append(u"\",\n  \"userVisibleKind\": \"资料包\",\n"_ustr);
    man.append(u"  \"boundary\": {\"includesVectorIndex\": false, \"includesApiKeys\": false, "
               u"\"mainDocumentMutation\": false}\n}\n"_ustr);
    WriteUtf8(r.packDir + u"/manifest.json"_ustr, man.makeStringAndClear());
    WriteUtf8(r.packDir + u"/README.md"_ustr,
              u"# 资料包："_ustr + title
                  + u"\n\n本包为本地 Markdown 导出，不含向量索引与密钥。\n主文档未改。\n"_ustr);

    r.ok = true;
    r.messageZh = u"资料包已导出："_ustr + r.packDir;
    return r;
}

} // namespace kqoffice::ai::vault
