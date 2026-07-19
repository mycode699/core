/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <DocumentAIMaterialReader.hxx>
#include <DocumentAIInputPrefs.hxx>
#include <NotebookMaterialStore.hxx>

#include <osl/file.hxx>
#include <osl/process.h>
#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kqoffice::ai::chat
{
namespace
{
constexpr sal_Int32 kMaxReadBytes = 512 * 1024;
constexpr sal_Int32 kMaxOcrBytes = 256 * 1024;

OUString extOf(const OUString& path)
{
    sal_Int32 slash = path.lastIndexOf('/');
    const sal_Int32 bslash = path.lastIndexOf('\\');
    if (bslash > slash)
        slash = bslash;
    const OUString name = slash >= 0 ? path.copy(slash + 1) : path;
    const sal_Int32 dot = name.lastIndexOf('.');
    if (dot < 0 || dot + 1 >= name.getLength())
        return {};
    return name.copy(dot + 1).toAsciiLowerCase();
}

OUString fileNameOf(const OUString& path)
{
    sal_Int32 slash = path.lastIndexOf('/');
    const sal_Int32 bslash = path.lastIndexOf('\\');
    if (bslash > slash)
        slash = bslash;
    return slash >= 0 ? path.copy(slash + 1) : path;
}

bool isDirectory(const OUString& systemPath)
{
    if (systemPath.isEmpty())
        return false;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(url, item) != osl::FileBase::E_None)
        return false;
    osl::FileStatus st(osl_FileStatus_Mask_Type);
    if (item.getFileStatus(st) != osl::FileBase::E_None)
        return false;
    return st.isValid(osl_FileStatus_Mask_Type) && st.isDirectory();
}

bool pathExists(const OUString& systemPath)
{
    if (systemPath.isEmpty())
        return false;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    return osl::DirectoryItem::get(url, item) == osl::FileBase::E_None;
}

OUString readUtf8Limited(const OUString& systemPath, sal_Int32 maxBytes)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return {};
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return {};
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0)
    {
        f.close();
        return {};
    }
    const sal_uInt64 take = std::min<sal_uInt64>(sz, static_cast<sal_uInt64>(maxBytes));
    std::vector<char> buf(static_cast<size_t>(take));
    sal_uInt64 n = 0;
    f.read(buf.data(), take, n);
    f.close();
    if (n == 0)
        return {};
    // Reject obvious binary (lots of NULs in first 256).
    sal_Int32 nul = 0;
    const size_t probe = std::min<size_t>(n, 256);
    for (size_t i = 0; i < probe; ++i)
        if (buf[i] == 0)
            ++nul;
    if (nul > 4)
        return {};
    return OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
}

OUString stripXmlTags(const OUString& xml, sal_Int32 maxChars)
{
    OUStringBuffer out;
    bool inTag = false;
    sal_Int32 consecutiveSpace = 0;
    for (sal_Int32 i = 0; i < xml.getLength(); ++i)
    {
        const sal_Unicode c = xml[i];
        if (c == u'<')
        {
            inTag = true;
            continue;
        }
        if (c == u'>')
        {
            inTag = false;
            if (out.getLength() > 0 && consecutiveSpace == 0)
            {
                out.append(u' ');
                consecutiveSpace = 1;
            }
            continue;
        }
        if (inTag)
            continue;
        if (c == u' ' || c == u'\t' || c == u'\n' || c == u'\r')
        {
            if (consecutiveSpace == 0)
            {
                out.append(u' ');
                consecutiveSpace = 1;
            }
            continue;
        }
        consecutiveSpace = 0;
        out.append(c);
        if (out.getLength() >= maxChars)
            break;
    }
    return out.makeStringAndClear().trim();
}

#if !defined(_WIN32)
OUString runShellCapture(const OUString& cmd)
{
    // Capture stdout via temporary file to avoid complex pipe APIs.
    const char* tmpEnv = std::getenv("TMPDIR");
    OUString tmpBase = tmpEnv && *tmpEnv ? OUString::fromUtf8(tmpEnv) : u"/tmp"_ustr;
    const OUString outPath
        = tmpBase + u"/kqoffice-ocr-"_ustr + OUString::number(static_cast<sal_Int64>(osl_getGlobalTimer()))
          + u".txt"_ustr;
    const OUString full
        = u"("_ustr + cmd + u") > \""_ustr + outPath + u"\" 2>/dev/null"_ustr;
    const OString utf8 = OUStringToOString(full, RTL_TEXTENCODING_UTF8);
    const int rc = std::system(utf8.getStr());
    (void)rc;
    OUString body = readUtf8Limited(outPath, kMaxOcrBytes);
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(outPath, url) == osl::FileBase::E_None)
        osl::File::remove(url);
    return body.trim();
}

bool commandExists(const OUString& name)
{
    const OUString cmd = u"command -v "_ustr + name + u" >/dev/null 2>&1"_ustr;
    const OString utf8 = OUStringToOString(cmd, RTL_TEXTENCODING_UTF8);
    return std::system(utf8.getStr()) == 0;
}
#endif

OUString resolveOcrCommand(const OUString& imagePath)
{
    const char* env = std::getenv("KQOFFICE_AI_OCR_CMD");
    OUString tmpl;
    if (env && *env)
        tmpl = OUString::fromUtf8(env);
    else
    {
        const auto prefs = DocumentAIInputPrefs::load();
        if (!prefs.ocrCmd.isEmpty())
            tmpl = prefs.ocrCmd;
    }
#if !defined(_WIN32)
    if (tmpl.isEmpty() && commandExists(u"tesseract"_ustr))
    {
        // Prefer Chinese+English when available; tesseract falls back if lang missing.
        tmpl = u"tesseract \"$IMAGE\" stdout -l chi_sim+eng --psm 6"_ustr;
    }
#endif
    if (tmpl.isEmpty())
        return {};
    OUString cmd = tmpl;
    cmd = cmd.replaceAll(u"$IMAGE"_ustr, imagePath);
    cmd = cmd.replaceAll(u"$FILE"_ustr, imagePath);
    cmd = cmd.replaceAll(u"$AUDIO"_ustr, imagePath); // tolerate voice-style placeholders
    return cmd;
}

OUString extractOfficeZipText(const OUString& systemPath, sal_Int32 maxChars)
{
#if !defined(_WIN32)
    // ODT/ODS/ODP are ZIP; DOCX/XLSX/PPTX use word/document.xml etc.
    const OUString ext = extOf(systemPath);
    std::vector<OUString> members;
    if (ext == u"odt"_ustr || ext == u"fodt"_ustr)
        members = { u"content.xml"_ustr };
    else if (ext == u"ods"_ustr || ext == u"fods"_ustr)
        members = { u"content.xml"_ustr };
    else if (ext == u"odp"_ustr || ext == u"fodp"_ustr)
        members = { u"content.xml"_ustr };
    else if (ext == u"docx"_ustr)
        members = { u"word/document.xml"_ustr };
    else if (ext == u"xlsx"_ustr)
        members = { u"xl/sharedStrings.xml"_ustr, u"xl/worksheets/sheet1.xml"_ustr };
    else if (ext == u"pptx"_ustr)
        members = { u"ppt/slides/slide1.xml"_ustr, u"ppt/slides/slide2.xml"_ustr,
                    u"ppt/slides/slide3.xml"_ustr };
    else
        return {};

    if (!commandExists(u"unzip"_ustr))
        return {};

    OUStringBuffer all;
    for (const auto& mem : members)
    {
        const OUString cmd = u"unzip -p -qq \""_ustr + systemPath + u"\" \""_ustr + mem + u"\""_ustr;
        const OUString xml = runShellCapture(cmd);
        if (xml.isEmpty())
            continue;
        const OUString plain = stripXmlTags(xml, maxChars);
        if (plain.isEmpty())
            continue;
        if (all.getLength() > 0)
            all.append(u"\n"_ustr);
        all.append(plain);
        if (all.getLength() >= maxChars)
            break;
    }
    OUString out = all.makeStringAndClear();
    if (out.getLength() > maxChars)
        out = out.copy(0, maxChars) + u"\n…"_ustr;
    return out;
#else
    (void)systemPath;
    (void)maxChars;
    return {};
#endif
}

OUString normalizePathToken(OUString path)
{
    path = path.trim();
    // Strip trailing punctuation commonly stuck from Chinese prompts.
    while (!path.isEmpty())
    {
        const sal_Unicode c = path[path.getLength() - 1];
        if (c == u'。' || c == u'，' || c == u',' || c == u';' || c == u'）' || c == u']'
            || c == u'」' || c == u'"' || c == u'\'')
            path = path.copy(0, path.getLength() - 1).trim();
        else
            break;
    }
    if (path.startsWith(u"\""_ustr) && path.endsWith(u"\""_ustr) && path.getLength() >= 2)
        path = path.copy(1, path.getLength() - 2);
    if (path.startsWith(u"file:"_ustr))
    {
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(path, sys) == osl::FileBase::E_None
            && !sys.isEmpty())
            path = sys;
    }
    return path;
}

void collectMentions(const OUString& prompt,
                     std::vector<std::pair<OUString, OUString>>& out /* label, path */)
{
    const std::vector<OUString> prefixes
        = { u"@文件:"_ustr, u"@截图:"_ustr, u"@文件夹:"_ustr, u"@folder:"_ustr, u"@file:"_ustr };
    sal_Int32 pos = 0;
    while (pos < prompt.getLength())
    {
        sal_Int32 found = -1;
        OUString which;
        for (const auto& p : prefixes)
        {
            const sal_Int32 i = prompt.indexOf(p, pos);
            if (i >= 0 && (found < 0 || i < found))
            {
                found = i;
                which = p;
            }
        }
        if (found < 0)
            break;
        sal_Int32 start = found + which.getLength();
        sal_Int32 end = start;
        // Path runs to end of line (supports spaces in paths).
        while (end < prompt.getLength() && prompt[end] != u'\n' && prompt[end] != u'\r')
            ++end;
        OUString path = normalizePathToken(prompt.copy(start, end - start));
        if (!path.isEmpty())
            out.emplace_back(which, path);
        pos = end;
    }
}
} // namespace

MaterialKind DocumentAIMaterialReader::detectKind(const OUString& rSystemPath)
{
    if (rSystemPath.isEmpty())
        return MaterialKind::Other;
    if (isDirectory(rSystemPath))
        return MaterialKind::Folder;
    const OUString e = extOf(rSystemPath);
    if (e == u"txt"_ustr || e == u"log"_ustr || e == u"json"_ustr || e == u"xml"_ustr
        || e == u"yaml"_ustr || e == u"yml"_ustr || e == u"ini"_ustr || e == u"cfg"_ustr
        || e == u"conf"_ustr || e == u"html"_ustr || e == u"htm"_ustr || e == u"css"_ustr
        || e == u"js"_ustr || e == u"ts"_ustr || e == u"py"_ustr || e == u"sh"_ustr
        || e == u"c"_ustr || e == u"cpp"_ustr || e == u"h"_ustr || e == u"java"_ustr
        || e == u"go"_ustr || e == u"rs"_ustr || e == u"rb"_ustr)
        return MaterialKind::Text;
    if (e == u"md"_ustr || e == u"markdown"_ustr)
        return MaterialKind::Markdown;
    if (e == u"csv"_ustr || e == u"tsv"_ustr)
        return MaterialKind::Csv;
    if (e == u"pdf"_ustr)
        return MaterialKind::Pdf;
    if (e == u"odt"_ustr || e == u"ods"_ustr || e == u"odp"_ustr || e == u"docx"_ustr
        || e == u"xlsx"_ustr || e == u"pptx"_ustr || e == u"doc"_ustr || e == u"xls"_ustr
        || e == u"ppt"_ustr || e == u"rtf"_ustr || e == u"fodt"_ustr || e == u"fods"_ustr
        || e == u"fodp"_ustr)
        return MaterialKind::Office;
    if (e == u"png"_ustr || e == u"jpg"_ustr || e == u"jpeg"_ustr || e == u"gif"_ustr
        || e == u"webp"_ustr || e == u"bmp"_ustr || e == u"tif"_ustr || e == u"tiff"_ustr
        || e == u"svg"_ustr)
        return MaterialKind::Image;
    return MaterialKind::Other;
}

OUString DocumentAIMaterialReader::kindLabelZh(MaterialKind eKind)
{
    switch (eKind)
    {
        case MaterialKind::Text:
            return u"纯文本"_ustr;
        case MaterialKind::Markdown:
            return u"Markdown"_ustr;
        case MaterialKind::Csv:
            return u"表格文本"_ustr;
        case MaterialKind::Pdf:
            return u"PDF"_ustr;
        case MaterialKind::Office:
            return u"办公文档"_ustr;
        case MaterialKind::Image:
            return u"图片"_ustr;
        case MaterialKind::Folder:
            return u"文件夹"_ustr;
        case MaterialKind::Other:
        default:
            return u"其他"_ustr;
    }
}

MaterialExtractResult DocumentAIMaterialReader::extractPath(const OUString& rSystemPath,
                                                            sal_Int32 nMaxChars)
{
    MaterialExtractResult r;
    r.path = normalizePathToken(rSystemPath);
    r.title = fileNameOf(r.path);
    if (nMaxChars < 200)
        nMaxChars = 200;
    if (r.path.isEmpty())
    {
        r.messageZh = u"路径为空"_ustr;
        return r;
    }
    if (!pathExists(r.path))
    {
        r.messageZh = u"路径不存在："_ustr + r.path;
        return r;
    }
    if (isDirectory(r.path))
        return extractFolder(r.path, 24, 3, nMaxChars);

    r.kind = detectKind(r.path);
    r.kindLabelZh = kindLabelZh(r.kind);

    switch (r.kind)
    {
        case MaterialKind::Text:
        case MaterialKind::Markdown:
        case MaterialKind::Csv:
        {
            OUString body = readUtf8Limited(r.path, kMaxReadBytes);
            if (body.isEmpty())
            {
                r.messageZh = u"无法按 UTF-8 读取文本文件"_ustr;
                r.method = u"path-only"_ustr;
                r.text = u"[本地文件 · 未能提取正文]\n路径: "_ustr + r.path;
                r.success = true; // still provide path context
                return r;
            }
            if (body.getLength() > nMaxChars)
                body = body.copy(0, nMaxChars) + u"\n…（已截断）"_ustr;
            r.text = body;
            r.method = u"utf8"_ustr;
            r.success = true;
            r.messageZh = u"已读取文本 · "_ustr + r.kindLabelZh;
            r.extractedCount = 1;
            return r;
        }
        case MaterialKind::Pdf:
        {
            const OUString harvested
                = notebook::NotebookMaterialStore::extractPdfTextLightweight(r.path, nMaxChars);
            if (harvested.getLength() >= 20)
            {
                r.text = harvested;
                r.method = u"pdf-lightweight"_ustr;
                r.success = true;
                r.messageZh = u"已轻量提取 PDF 文本"_ustr;
                r.extractedCount = 1;
            }
            else
            {
                r.text = u"[PDF · 文本层较少]\n路径: "_ustr + r.path
                         + u"\n说明：已尝试本地字符串提取；扫描版 PDF 需配置 OCR（tesseract 或 "
                           u"KQOFFICE_AI_OCR_CMD）"_ustr;
                if (!harvested.isEmpty())
                {
                    r.text += u"\n\n"_ustr + harvested;
                    r.method = u"pdf-lightweight"_ustr;
                }
                else
                    r.method = u"path-only"_ustr;
                r.success = true;
                r.messageZh = u"PDF 文本较少，已附路径"_ustr;
            }
            return r;
        }
        case MaterialKind::Office:
        {
            OUString body = extractOfficeZipText(r.path, nMaxChars);
            if (body.getLength() >= 20)
            {
                r.text = body;
                r.method = u"office-zip"_ustr;
                r.success = true;
                r.messageZh = u"已从办公文档提取文本"_ustr;
                r.extractedCount = 1;
            }
            else
            {
                r.text = u"[办公文档]\n路径: "_ustr + r.path
                         + u"\n说明：未能从 ZIP/XML 提取正文（可打开文档后用「问本文档」或授权目录）"_ustr;
                r.method = u"path-only"_ustr;
                r.success = true;
                r.messageZh = u"办公文档未提取到正文，已附路径"_ustr;
            }
            return r;
        }
        case MaterialKind::Image:
        {
            const auto prefs = DocumentAIInputPrefs::load();
            if (!prefs.ocrEnabled)
            {
                r.text = u"[图片 · OCR 已关闭]\n路径: "_ustr + r.path
                         + u"\n说明：在选项或 ai-input-prefs.json 中设置 ocrEnabled=true，"
                           u"或配置 KQOFFICE_AI_OCR_CMD（$IMAGE）"_ustr;
                r.method = u"path-only"_ustr;
                r.success = true;
                r.messageZh = u"图片已引用（OCR 关闭）"_ustr;
                return r;
            }
#if !defined(_WIN32)
            const OUString cmd = resolveOcrCommand(r.path);
            if (!cmd.isEmpty())
            {
                const OUString ocr = runShellCapture(cmd);
                if (ocr.getLength() >= 2)
                {
                    OUString body = ocr;
                    if (body.getLength() > nMaxChars)
                        body = body.copy(0, nMaxChars) + u"\n…（OCR 已截断）"_ustr;
                    r.text = u"[图片 OCR · 本地]\n路径: "_ustr + r.path + u"\n\n"_ustr + body;
                    r.method = u"ocr"_ustr;
                    r.success = true;
                    r.messageZh = u"已本地 OCR 识别图片文字"_ustr;
                    r.extractedCount = 1;
                    return r;
                }
            }
#endif
            r.text = u"[图片 · 无 OCR 结果]\n路径: "_ustr + r.path
                     + u"\n说明：安装 tesseract（chi_sim+eng）或设置 KQOFFICE_AI_OCR_CMD，"
                       u"例如：tesseract \"$IMAGE\" stdout -l chi_sim+eng"_ustr;
            r.method = u"path-only"_ustr;
            r.success = true;
            r.messageZh = u"图片已引用（OCR 未产出文本）"_ustr;
            return r;
        }
        case MaterialKind::Other:
        default:
        {
            OUString body = readUtf8Limited(r.path, kMaxReadBytes);
            if (!body.isEmpty())
            {
                if (body.getLength() > nMaxChars)
                    body = body.copy(0, nMaxChars) + u"\n…"_ustr;
                r.text = body;
                r.method = u"utf8"_ustr;
                r.success = true;
                r.messageZh = u"已按文本尝试读取"_ustr;
                r.extractedCount = 1;
            }
            else
            {
                r.text = u"[本地文件]\n路径: "_ustr + r.path + u"\n（无法自动提取正文）"_ustr;
                r.method = u"path-only"_ustr;
                r.success = true;
                r.messageZh = u"已附路径，正文未提取"_ustr;
            }
            return r;
        }
    }
}

MaterialExtractResult DocumentAIMaterialReader::extractFolder(const OUString& rDirPath,
                                                              sal_Int32 nMaxFiles,
                                                              sal_Int32 nMaxDepth,
                                                              sal_Int32 nMaxChars)
{
    MaterialExtractResult r;
    r.path = normalizePathToken(rDirPath);
    r.title = fileNameOf(r.path);
    r.kind = MaterialKind::Folder;
    r.kindLabelZh = kindLabelZh(MaterialKind::Folder);
    if (nMaxFiles < 1)
        nMaxFiles = 1;
    if (nMaxDepth < 1)
        nMaxDepth = 1;
    if (nMaxChars < 500)
        nMaxChars = 500;

    if (r.path.isEmpty() || !isDirectory(r.path))
    {
        r.messageZh = u"不是有效文件夹："_ustr + r.path;
        return r;
    }

    struct Entry
    {
        OUString path;
        OUString name;
        MaterialKind kind = MaterialKind::Other;
    };
    std::vector<Entry> files;

    std::function<void(const OUString&, sal_Int32)> walk;
    walk = [&](const OUString& dir, sal_Int32 depth) {
        if (static_cast<sal_Int32>(files.size()) >= nMaxFiles * 3)
            return;
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(dir, url) != osl::FileBase::E_None)
            return;
        osl::Directory d(url);
        if (d.open() != osl::FileBase::E_None)
            return;
        osl::DirectoryItem item;
        while (d.getNextItem(item) == osl::FileBase::E_None)
        {
            osl::FileStatus st(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileName
                               | osl_FileStatus_Mask_FileURL);
            if (item.getFileStatus(st) != osl::FileBase::E_None)
                continue;
            if (!st.isValid(osl_FileStatus_Mask_FileName))
                continue;
            const OUString name = st.getFileName();
            if (name.isEmpty() || name[0] == u'.')
                continue;
            OUString childSys;
            if (st.isValid(osl_FileStatus_Mask_FileURL))
                osl::FileBase::getSystemPathFromFileURL(st.getFileURL(), childSys);
            if (childSys.isEmpty())
                childSys = dir + u"/"_ustr + name;
            if (st.isValid(osl_FileStatus_Mask_Type) && st.isDirectory())
            {
                if (depth < nMaxDepth)
                    walk(childSys, depth + 1);
                continue;
            }
            if (st.isValid(osl_FileStatus_Mask_Type) && st.isRegular())
            {
                Entry e;
                e.path = childSys;
                e.name = name;
                e.kind = detectKind(childSys);
                files.push_back(std::move(e));
            }
        }
        d.close();
    };
    walk(r.path, 0);

    r.fileCount = static_cast<sal_Int32>(files.size());
    OUStringBuffer inv;
    inv.append(u"[文件夹清单] "_ustr);
    inv.append(r.path);
    inv.append(u"\n共扫描文件 "_ustr);
    inv.append(static_cast<sal_Int32>(files.size()));
    inv.append(u" 个（深度≤"_ustr);
    inv.append(nMaxDepth);
    inv.append(u"）\n"_ustr);

    sal_Int32 listed = 0;
    for (const auto& f : files)
    {
        if (listed >= 40)
        {
            inv.append(u"…\n"_ustr);
            break;
        }
        inv.append(u"- "_ustr);
        inv.append(kindLabelZh(f.kind));
        inv.append(u" · "_ustr);
        inv.append(f.name);
        inv.append(u"\n"_ustr);
        ++listed;
    }

    // Prefer extractable kinds for body samples.
    auto score = [](MaterialKind k) -> int {
        switch (k)
        {
            case MaterialKind::Text:
            case MaterialKind::Markdown:
            case MaterialKind::Csv:
                return 0;
            case MaterialKind::Pdf:
                return 1;
            case MaterialKind::Office:
                return 2;
            case MaterialKind::Image:
                return 3;
            default:
                return 9;
        }
    };
    std::stable_sort(files.begin(), files.end(),
                     [&](const Entry& a, const Entry& b) { return score(a.kind) < score(b.kind); });

    OUStringBuffer bodies;
    sal_Int32 extracted = 0;
    sal_Int32 budget = nMaxChars - inv.getLength() - 64;
    for (const auto& f : files)
    {
        if (extracted >= nMaxFiles || budget < 200)
            break;
        if (f.kind == MaterialKind::Other)
            continue;
        const MaterialExtractResult one = extractPath(f.path, std::min<sal_Int32>(budget / 2, 4000));
        if (!one.success || one.text.isEmpty())
            continue;
        if (one.method == u"path-only"_ustr && one.kind != MaterialKind::Image)
            continue;
        bodies.append(u"\n--- 文件: "_ustr);
        bodies.append(f.name);
        bodies.append(u" ("_ustr);
        bodies.append(one.kindLabelZh);
        bodies.append(u" · "_ustr);
        bodies.append(one.method);
        bodies.append(u") ---\n"_ustr);
        OUString snip = one.text;
        if (snip.getLength() > budget)
            snip = snip.copy(0, budget) + u"\n…"_ustr;
        bodies.append(snip);
        bodies.append(u"\n"_ustr);
        budget -= snip.getLength() + 80;
        ++extracted;
    }

    r.text = inv.makeStringAndClear() + bodies.makeStringAndClear();
    r.extractedCount = extracted;
    r.method = u"inventory"_ustr;
    r.success = true;
    r.messageZh = u"文件夹清单 "_ustr + OUString::number(r.fileCount) + u" 个 · 已抽取 "_ustr
                  + OUString::number(extracted) + u" 份正文"_ustr;
    return r;
}

bool DocumentAIMaterialReader::isMaterialMentionPrefix(const OUString& rToken)
{
    return rToken.startsWith(u"@文件:"_ustr) || rToken.startsWith(u"@截图:"_ustr)
           || rToken.startsWith(u"@文件夹:"_ustr) || rToken.startsWith(u"@folder:"_ustr)
           || rToken.startsWith(u"@file:"_ustr);
}

std::vector<OUString> DocumentAIMaterialReader::collectMentionPaths(const OUString& rPrompt)
{
    std::vector<std::pair<OUString, OUString>> pairs;
    collectMentions(rPrompt, pairs);
    std::vector<OUString> out;
    out.reserve(pairs.size());
    for (const auto& p : pairs)
        out.push_back(p.second);
    return out;
}

OUString DocumentAIMaterialReader::expandMentionsInPrompt(const OUString& rPrompt,
                                                          OUString& rSummaryZh,
                                                          sal_Int32 nMaxTotalChars)
{
    rSummaryZh.clear();
    std::vector<std::pair<OUString, OUString>> pairs;
    collectMentions(rPrompt, pairs);
    if (pairs.empty())
        return rPrompt;

    if (nMaxTotalChars < 2000)
        nMaxTotalChars = 2000;

    OUStringBuffer ctx;
    ctx.append(u"【本地材料上下文 · 已读取/抽取，未上传云端】\n"_ustr);
    sal_Int32 used = ctx.getLength();
    sal_Int32 ok = 0;
    sal_Int32 fail = 0;
    OUStringBuffer statusBits;

    // Dedupe paths.
    std::vector<OUString> seen;
    for (const auto& pr : pairs)
    {
        const OUString& path = pr.second;
        bool dup = false;
        for (const auto& s : seen)
            if (s == path)
            {
                dup = true;
                break;
            }
        if (dup)
            continue;
        seen.push_back(path);

        if (used >= nMaxTotalChars - 400)
            break;

        const sal_Int32 budget = std::min<sal_Int32>(8000, nMaxTotalChars - used - 200);
        MaterialExtractResult ex;
        if (pr.first.indexOf(u"文件夹"_ustr) >= 0 || pr.first.indexOf(u"folder"_ustr) >= 0
            || isDirectory(path))
            ex = extractFolder(path, 20, 3, budget);
        else
            ex = extractPath(path, budget);

        ctx.append(u"\n======== "_ustr);
        ctx.append(ex.kindLabelZh.isEmpty() ? u"材料"_ustr : ex.kindLabelZh);
        ctx.append(u" · "_ustr);
        ctx.append(ex.title.isEmpty() ? path : ex.title);
        ctx.append(u" ========\n"_ustr);
        ctx.append(u"路径: "_ustr);
        ctx.append(path);
        ctx.append(u"\n方式: "_ustr);
        ctx.append(ex.method.isEmpty() ? u"-"_ustr : ex.method);
        ctx.append(u"\n"_ustr);
        if (!ex.text.isEmpty())
            ctx.append(ex.text);
        else
            ctx.append(u"（无抽取正文）"_ustr);
        ctx.append(u"\n"_ustr);
        used = ctx.getLength();

        if (ex.success && ex.extractedCount > 0)
            ++ok;
        else if (ex.success)
            ++ok; // path-only still counts as handled
        else
            ++fail;
        if (!ex.messageZh.isEmpty())
        {
            if (statusBits.getLength() > 0)
                statusBits.append(u"；"_ustr);
            statusBits.append(ex.messageZh);
        }
    }

    ctx.append(u"\n【用户指令】\n"_ustr);
    ctx.append(rPrompt);

    rSummaryZh = u"本地材料 "_ustr + OUString::number(static_cast<sal_Int32>(seen.size()))
                 + u" 项 · 可读 "_ustr + OUString::number(ok);
    if (fail > 0)
        rSummaryZh += u" · 失败 "_ustr + OUString::number(fail);
    if (statusBits.getLength() > 0)
        rSummaryZh += u" · "_ustr + statusBits.makeStringAndClear();
    rSummaryZh += u" · 未上传"_ustr;

    return ctx.makeStringAndClear();
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
