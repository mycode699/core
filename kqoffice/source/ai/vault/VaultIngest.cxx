/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "VaultIngest.hxx"
#include "VaultStore.hxx"

#include <DocumentAIMaterialReader.hxx>
#include <NotebookMaterialStore.hxx>

#include <comphelper/hash.hxx>
#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdio>
#include <fstream>
#include <string>

namespace kqoffice::ai::vault
{
namespace
{
OUString IsoNow()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    // Compact ISO-ish UTC-ish local stamp for filenames / logs
    OUStringBuffer b;
    b.append(static_cast<sal_Int64>(tv.Seconds));
    return b.makeStringAndClear();
}

OUString Sha256HexOf(const OUString& rText)
{
    const OString u = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    const auto h = comphelper::Hash::calculateHash(u.getStr(), u.getLength(),
                                                   comphelper::HashType::SHA256);
    return u"sha256:"_ustr + OUString::createFromAscii(comphelper::hashToString(h));
}

OUString MakeId(const OUString& rSeed)
{
    const OUString hex = Sha256HexOf(rSeed + IsoNow()).copy(7); // drop sha256:
    return u"ving-"_ustr + hex.copy(0, 16);
}

bool WriteFileUtf8(const OUString& rSysPath, const OUString& rBody)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    const sal_Int32 slash = rSysPath.lastIndexOf('/');
    if (slash > 0)
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(rSysPath.copy(0, slash), dirUrl)
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

void AppendIngestLog(const OUString& rVaultRoot, const OUString& rJsonLine)
{
    const VaultPaths p = VaultStore::pathsFor(rVaultRoot);
    const OUString logPath = p.internalState + u"/ingest-log.jsonl"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(logPath, url) != osl::FileBase::E_None)
        return;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    (void)f.setPos(osl_Pos_Absolut, sal_uInt64(sz));
    const OUString lineU = rJsonLine + u"\n"_ustr;
    const OString line = OUStringToOString(lineU, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(line.getStr(), line.getLength(), n);
    f.close();
}

bool IsProbablyTextExt(const OUString& path)
{
    const sal_Int32 dot = path.lastIndexOf('.');
    if (dot < 0)
        return false;
    OUString ext = path.copy(dot + 1).toAsciiLowerCase();
    return ext == u"md" || ext == u"txt" || ext == u"csv" || ext == u"json" || ext == u"html"
           || ext == u"htm" || ext == u"xml" || ext == u"log" || ext == u"rst";
}

OUString BaseName(const OUString& path)
{
    const sal_Int32 s = path.lastIndexOf('/');
    return s >= 0 ? path.copy(s + 1) : path;
}

OUString JsonEscape(const OUString& s)
{
    OUStringBuffer b;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u'"' || c == u'\\')
        {
            b.append(u'\\');
            b.append(c);
        }
        else if (c == u'\n')
            b.append(u"\\n"_ustr);
        else if (c == u'\r')
            b.append(u"\\r"_ustr);
        else
            b.append(c);
    }
    return b.makeStringAndClear();
}
} // namespace

VaultIngestResult VaultIngest::ingestPath(const OUString& rSystemPath, const OUString& rVaultRoot)
{
    VaultIngestResult r;
    if (rSystemPath.isEmpty())
    {
        r.messageZh = u"路径为空"_ustr;
        return r;
    }
    if (!VaultStore::ensureLayout(rVaultRoot))
    {
        r.messageZh = u"无法创建资料盘目录"_ustr;
        return r;
    }
    const VaultPaths vp = VaultStore::pathsFor(rVaultRoot);
    const auto extracted = kqoffice::ai::chat::DocumentAIMaterialReader::extractPath(rSystemPath,
                                                                                    48000);
    const OUString title = extracted.title.isEmpty() ? BaseName(rSystemPath) : extracted.title;
    const OUString body = extracted.text;
    const OUString hash = Sha256HexOf(body.isEmpty() ? rSystemPath : body);
    const OUString id = MakeId(rSystemPath + hash);
    r.id = id;
    r.title = title;
    r.sourcePath = rSystemPath;
    r.contentHash = hash;

    const bool bDir = extracted.kind == kqoffice::ai::chat::MaterialKind::Folder;
    const bool bTexty = IsProbablyTextExt(rSystemPath) || !body.isEmpty();
    const bool bLink = bDir || !bTexty
                       || VaultStore::officeLargeFileDefault() == u"link"_ustr;

    if (bDir)
    {
        r.kind = u"folder-link"_ustr;
        const OUString rel = u"raw/office-links/"_ustr + id + u".json"_ustr;
        const OUString abs = vp.root + u"/"_ustr + rel;
        OUStringBuffer jb;
        jb.append(u"{\"id\":\""_ustr);
        jb.append(id);
        jb.append(u"\",\"kind\":\"folder-link\",\"title\":\""_ustr);
        jb.append(JsonEscape(title));
        jb.append(u"\",\"sourcePath\":\""_ustr);
        jb.append(JsonEscape(rSystemPath));
        jb.append(u"\",\"contentHash\":\""_ustr);
        jb.append(hash);
        jb.append(u"\"}\n"_ustr);
        if (!WriteFileUtf8(abs, jb.makeStringAndClear()))
        {
            r.messageZh = u"写入 folder-link 失败"_ustr;
            return r;
        }
        // Snippet for FTS: inventory text
        const OUString snipRel = u"raw/imports/"_ustr + id + u".txt"_ustr;
        const OUString snipAbs = vp.root + u"/"_ustr + snipRel;
        const OUString snipBody
            = u"# "_ustr + title + u"\n\n"_ustr
              + (body.isEmpty() ? u"（文件夹清单为空）\n"_ustr : body);
        WriteFileUtf8(snipAbs, snipBody);
        r.rawRelativePath = rel;
        r.snippetPath = snipAbs;
        r.indexedChars = snipBody.getLength();
    }
    else if (bLink && !IsProbablyTextExt(rSystemPath))
    {
        r.kind = u"file-link"_ustr;
        const OUString rel = u"raw/office-links/"_ustr + id + u".json"_ustr;
        const OUString abs = vp.root + u"/"_ustr + rel;
        OUStringBuffer jb;
        jb.append(u"{\"id\":\""_ustr);
        jb.append(id);
        jb.append(u"\",\"kind\":\"file-link\",\"title\":\""_ustr);
        jb.append(JsonEscape(title));
        jb.append(u"\",\"sourcePath\":\""_ustr);
        jb.append(JsonEscape(rSystemPath));
        jb.append(u"\",\"contentHash\":\""_ustr);
        jb.append(hash);
        jb.append(u"\"}\n"_ustr);
        WriteFileUtf8(abs, jb.makeStringAndClear());
        const OUString snipRel = u"raw/imports/"_ustr + id + u".txt"_ustr;
        const OUString snipAbs = vp.root + u"/"_ustr + snipRel;
        OUString snipBody = u"# "_ustr + title + u"\n来源："_ustr + rSystemPath + u"\n\n"_ustr;
        if (!body.isEmpty())
            snipBody += body;
        else
            snipBody += u"（未能提取正文，仅索引标题与路径）\n"_ustr;
        WriteFileUtf8(snipAbs, snipBody);
        r.rawRelativePath = rel;
        r.snippetPath = snipAbs;
        r.indexedChars = snipBody.getLength();
    }
    else
    {
        r.kind = u"file-copy"_ustr;
        const OUString rel = u"raw/imports/"_ustr + id + u"-"_ustr + BaseName(rSystemPath);
        const OUString abs = vp.root + u"/"_ustr + rel;
        OUString content = body;
        if (content.isEmpty())
            content = u"# "_ustr + title + u"\n"_ustr + rSystemPath + u"\n"_ustr;
        if (!WriteFileUtf8(abs, content))
        {
            r.messageZh = u"写入 raw/imports 失败"_ustr;
            return r;
        }
        r.rawRelativePath = rel;
        r.snippetPath = abs;
        r.indexedChars = content.getLength();
    }

    OUStringBuffer log;
    log.append(u"{\"id\":\""_ustr);
    log.append(id);
    log.append(u"\",\"title\":\""_ustr);
    log.append(JsonEscape(title));
    log.append(u"\",\"kind\":\""_ustr);
    log.append(r.kind);
    log.append(u"\",\"path\":\""_ustr);
    log.append(JsonEscape(r.snippetPath));
    log.append(u"\"}"_ustr);
    AppendIngestLog(rVaultRoot, log.makeStringAndClear());

    r.ok = true;
    r.messageZh = u"已收入资料盘："_ustr + title;
    return r;
}

VaultIngestResult VaultIngest::ingestText(const OUString& rTitle, const OUString& rBody,
                                          const OUString& rVaultRoot)
{
    VaultIngestResult r;
    if (rBody.isEmpty())
    {
        r.messageZh = u"正文为空"_ustr;
        return r;
    }
    if (!VaultStore::ensureLayout(rVaultRoot))
    {
        r.messageZh = u"无法创建资料盘目录"_ustr;
        return r;
    }
    const VaultPaths vp = VaultStore::pathsFor(rVaultRoot);
    const OUString title = rTitle.isEmpty() ? u"粘贴文本"_ustr : rTitle;
    const OUString hash = Sha256HexOf(rBody);
    const OUString id = MakeId(title + hash);
    const OUString rel = u"raw/imports/"_ustr + id + u".md"_ustr;
    const OUString abs = vp.root + u"/"_ustr + rel;
    const OUString body = u"# "_ustr + title + u"\n\n"_ustr + rBody;
    if (!WriteFileUtf8(abs, body))
    {
        r.messageZh = u"写入失败"_ustr;
        return r;
    }
    r.ok = true;
    r.id = id;
    r.title = title;
    r.kind = u"paste-text"_ustr;
    r.rawRelativePath = rel;
    r.snippetPath = abs;
    r.contentHash = hash;
    r.indexedChars = body.getLength();
    r.messageZh = u"已收入资料盘："_ustr + title;
    AppendIngestLog(rVaultRoot,
                    u"{\"id\":\""_ustr + id + u"\",\"title\":\""_ustr + JsonEscape(title)
                        + u"\",\"kind\":\"paste-text\"}"_ustr);
    return r;
}

sal_Int32 VaultIngest::ingestNotebookMaterials(const OUString& rVaultRoot, sal_Int32 nMax)
{
    if (!VaultStore::ensureLayout(rVaultRoot))
        return 0;
    const auto mats = kqoffice::ai::notebook::NotebookMaterialStore::listMaterials();
    sal_Int32 n = 0;
    const sal_Int32 lim = nMax > 0 ? nMax : 200;
    for (const auto& m : mats)
    {
        if (n >= lim)
            break;
        if (!m.sourcePath.isEmpty())
        {
            const auto ir = ingestPath(m.sourcePath, rVaultRoot);
            if (ir.ok)
                ++n;
        }
        else
        {
            const auto full = kqoffice::ai::notebook::NotebookMaterialStore::loadMaterial(m.id);
            if (!full.snippet.isEmpty())
            {
                const auto ir = ingestText(full.title.isEmpty() ? m.title : full.title, full.snippet,
                                           rVaultRoot);
                if (ir.ok)
                    ++n;
            }
        }
    }
    return n;
}

std::vector<OUString> VaultIngest::listRecentTitles(const OUString& rVaultRoot, sal_Int32 nMax)
{
    std::vector<OUString> out;
    const VaultPaths vp = VaultStore::pathsFor(rVaultRoot);
    const OUString logPath = vp.internalState + u"/ingest-log.jsonl"_ustr;
    const OString sys = OUStringToOString(logPath, RTL_TEXTENCODING_UTF8);
    std::ifstream in(sys.getStr());
    if (!in)
        return out;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty())
            lines.push_back(line);
    const sal_Int32 lim = nMax > 0 ? nMax : 40;
    for (sal_Int32 i = static_cast<sal_Int32>(lines.size()) - 1; i >= 0 && static_cast<sal_Int32>(out.size()) < lim;
         --i)
    {
        const std::string& L = lines[static_cast<size_t>(i)];
        const auto tpos = L.find("\"title\":\"");
        if (tpos == std::string::npos)
            continue;
        size_t a = tpos + 9;
        size_t b = L.find('"', a);
        if (b == std::string::npos || b <= a)
            continue;
        out.push_back(OStringToOUString(std::string_view(L.data() + a, b - a), RTL_TEXTENCODING_UTF8));
    }
    return out;
}

} // namespace kqoffice::ai::vault
