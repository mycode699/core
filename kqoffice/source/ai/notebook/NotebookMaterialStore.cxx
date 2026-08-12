/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "NotebookMaterialStore.hxx"

#include <osl/file.hxx>
#include <osl/mutex.hxx>
#include <osl/process.h>
#include <osl/thread.hxx>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace kqoffice::ai::notebook
{
namespace
{
constexpr sal_Int32 kMaxSnippetChars = 200000; // ~200KB text cap per material
constexpr sal_Int32 kMaxReadBytes = 512 * 1024; // read at most 512KB for extract

osl::Mutex& matMutex()
{
    static osl::Mutex a;
    return a;
}

bool ensureDirSys(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

OUString nowIso()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    const std::time_t sec = static_cast<std::time_t>(tv.Seconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &sec);
#else
    localtime_r(&sec, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return OUString::fromUtf8(buf);
}

bool writeFile(const OUString& rSysPath, const OUString& rContent)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString utf8 = OUStringToOString(rContent, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    const bool ok = f.write(utf8.getStr(), utf8.getLength(), n) == osl::FileBase::E_None
                    && n == static_cast<sal_uInt64>(utf8.getLength());
    f.close();
    return ok;
}

OUString readFileLimited(const OUString& rSysPath, sal_Int32 nMaxBytes)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return OUString();
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return OUString();
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0)
    {
        f.close();
        return OUString();
    }
    const sal_uInt64 take = std::min<sal_uInt64>(sz, static_cast<sal_uInt64>(nMaxBytes));
    std::vector<char> buf(static_cast<size_t>(take));
    sal_uInt64 n = 0;
    f.read(buf.data(), take, n);
    f.close();
    // reject if high ratio of NULs (binary)
    size_t nuls = 0;
    for (size_t i = 0; i < static_cast<size_t>(n); ++i)
        if (buf[i] == 0)
            ++nuls;
    if (n > 32 && nuls * 20 > static_cast<size_t>(n))
        return OUString();
    return OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
}

sal_Int64 fileSizeSys(const OUString& rSysPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return 0;
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(url, item) != osl::FileBase::E_None)
        return 0;
    osl::FileStatus st(osl_FileStatus_Mask_FileSize);
    if (item.getFileStatus(st) != osl::FileBase::E_None)
        return 0;
    return static_cast<sal_Int64>(st.getFileSize());
}

OUString fileNameOf(const OUString& rPath)
{
    sal_Int32 slash = rPath.lastIndexOf(u'/');
#if defined(_WIN32)
    const sal_Int32 bslash = rPath.lastIndexOf(u'\\');
    if (bslash > slash)
        slash = bslash;
#endif
    if (slash >= 0 && slash + 1 < rPath.getLength())
        return rPath.copy(slash + 1);
    return rPath;
}

OUString extOf(const OUString& rPath)
{
    const OUString name = fileNameOf(rPath);
    const sal_Int32 dot = name.lastIndexOf(u'.');
    if (dot < 0 || dot + 1 >= name.getLength())
        return OUString();
    return name.copy(dot + 1).toAsciiLowerCase();
}

OUString baseNameNoExt(const OUString& rPath)
{
    const OUString name = fileNameOf(rPath);
    const sal_Int32 dot = name.lastIndexOf(u'.');
    if (dot > 0)
        return name.copy(0, dot);
    return name;
}

OUString parentDirOf(const OUString& rPath)
{
    sal_Int32 slash = rPath.lastIndexOf(u'/');
#if defined(_WIN32)
    const sal_Int32 bslash = rPath.lastIndexOf(u'\\');
    if (bslash > slash)
        slash = bslash;
#endif
    if (slash > 0)
        return rPath.copy(0, slash);
    return OUString();
}

bool fileExistsSys(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    return osl::DirectoryItem::get(url, item) == osl::FileBase::E_None;
}

/// Strip SRT/VTT timing / index lines → dialogue.
OUString stripSubtitleMarkup(const OUString& rRaw)
{
    OUStringBuffer out;
    sal_Int32 lineStart = 0;
    const sal_Int32 n = rRaw.getLength();
    for (sal_Int32 i = 0; i <= n; ++i)
    {
        if (i != n && rRaw[i] != u'\n')
            continue;
        OUString line = rRaw.copy(lineStart, i - lineStart);
        lineStart = i + 1;
        // strip CR
        if (line.endsWith(u"\r"_ustr))
            line = line.copy(0, line.getLength() - 1);
        const OUString t = line.trim();
        if (t.isEmpty())
            continue;
        // WEBVTT header / NOTE / STYLE
        if (t.startsWith(u"WEBVTT"_ustr) || t.startsWith(u"NOTE"_ustr) || t.startsWith(u"STYLE"_ustr)
            || t.startsWith(u"REGION"_ustr) || t.startsWith(u"X-TIMESTAMP"_ustr))
            continue;
        // pure index number
        bool pureNum = true;
        for (sal_Int32 k = 0; k < t.getLength(); ++k)
        {
            if (t[k] < u'0' || t[k] > u'9')
            {
                pureNum = false;
                break;
            }
        }
        if (pureNum)
            continue;
        // timestamp line 00:00:01,000 --> 00:00:04,000
        if (t.indexOf(u"-->"_ustr) >= 0)
            continue;
        // VTT cue settings with timestamps
        if (t.indexOf(u':') >= 0 && t.indexOf(u'.') >= 0 && t.getLength() < 40
            && t.indexOf(u' ') < 0)
            continue;
        // strip simple tags <c> </c> {\an8}
        OUString cleaned;
        {
            OUStringBuffer cb;
            bool inAngle = false;
            bool inBrace = false;
            for (sal_Int32 k = 0; k < t.getLength(); ++k)
            {
                const sal_Unicode c = t[k];
                if (c == u'<')
                {
                    inAngle = true;
                    continue;
                }
                if (c == u'>')
                {
                    inAngle = false;
                    continue;
                }
                if (c == u'{')
                {
                    inBrace = true;
                    continue;
                }
                if (c == u'}')
                {
                    inBrace = false;
                    continue;
                }
                if (!inAngle && !inBrace)
                    cb.append(c);
            }
            cleaned = cb.makeStringAndClear().trim();
        }
        if (cleaned.isEmpty())
            continue;
        out.append(cleaned);
        out.append(u'\n');
    }
    return out.makeStringAndClear().trim();
}

OUString loadSidecarTranscript(const OUString& rMediaPath)
{
    const OUString dir = parentDirOf(rMediaPath);
    const OUString base = baseNameNoExt(rMediaPath);
    if (dir.isEmpty() || base.isEmpty())
        return OUString();
    const OUString sep = rMediaPath.indexOf(u'\\') >= 0 ? u"\\"_ustr : u"/"_ustr;
    static const char* const kSuff[]
        = { ".srt", ".vtt", ".zh.srt", ".zh-CN.srt", ".en.srt", ".txt", ".transcript.txt" };
    for (const char* suf : kSuff)
    {
        const OUString cand = dir + sep + base + OUString::fromUtf8(suf);
        if (!fileExistsSys(cand))
            continue;
        const OUString raw = readFileLimited(cand, kMaxReadBytes);
        if (raw.isEmpty())
            continue;
        OUString plain = stripSubtitleMarkup(raw);
        if (plain.isEmpty())
            plain = raw;
        return u"\n\n--- 同名字幕/转录（"_ustr + fileNameOf(cand) + u"）---\n"_ustr + plain;
    }
    return OUString();
}

void appendEsc(OUStringBuffer& b, const OUString& s)
{
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u'\\' || c == u'"')
            b.append(u'\\');
        if (c == u'\n')
        {
            b.append(u"\\n"_ustr);
            continue;
        }
        if (c == u'\r')
            continue;
        b.append(c);
    }
}

OUString field(const OUString& frag, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\":\""_ustr;
    sal_Int32 p = frag.indexOf(needle);
    if (p < 0)
        return OUString();
    p += needle.getLength();
    OUStringBuffer out;
    for (sal_Int32 i = p; i < frag.getLength(); ++i)
    {
        if (frag[i] == u'\\' && i + 1 < frag.getLength())
        {
            ++i;
            if (frag[i] == u'n')
                out.append(u'\n');
            else
                out.append(frag[i]);
            continue;
        }
        if (frag[i] == u'"')
            break;
        out.append(frag[i]);
    }
    return out.makeStringAndClear();
}

sal_Int32 fieldInt(const OUString& frag, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\":"_ustr;
    sal_Int32 p = frag.indexOf(needle);
    if (p < 0)
        return 0;
    p += needle.getLength();
    while (p < frag.getLength() && (frag[p] == u' ' || frag[p] == u'\t'))
        ++p;
    sal_Int32 v = 0;
    bool neg = false;
    if (p < frag.getLength() && frag[p] == u'-')
    {
        neg = true;
        ++p;
    }
    for (; p < frag.getLength(); ++p)
    {
        const sal_Unicode c = frag[p];
        if (c < u'0' || c > u'9')
            break;
        v = v * 10 + (c - u'0');
    }
    return neg ? -v : v;
}

sal_Int64 fieldInt64(const OUString& frag, const OUString& key)
{
    return static_cast<sal_Int64>(fieldInt(frag, key));
}

std::vector<NotebookMaterialIndexEntry> parseIndex(const OUString& json)
{
    std::vector<NotebookMaterialIndexEntry> out;
    sal_Int32 search = 0;
    while (true)
    {
        sal_Int32 idPos = json.indexOf(u"\"id\""_ustr, search);
        if (idPos < 0)
            break;
        sal_Int32 start = idPos;
        while (start > 0 && json[start] != u'{')
            --start;
        sal_Int32 depth = 0, end = start;
        for (; end < json.getLength(); ++end)
        {
            if (json[end] == u'{')
                ++depth;
            else if (json[end] == u'}')
            {
                --depth;
                if (depth == 0)
                {
                    ++end;
                    break;
                }
            }
        }
        const OUString frag = json.copy(start, end - start);
        NotebookMaterialIndexEntry e;
        e.id = field(frag, u"id"_ustr);
        e.title = field(frag, u"title"_ustr);
        e.kind = field(frag, u"kind"_ustr);
        e.sourcePath = field(frag, u"source"_ustr);
        e.mimeOrExt = field(frag, u"ext"_ustr);
        e.createdIso = field(frag, u"created"_ustr);
        e.lastUsedIso = field(frag, u"last_used"_ustr);
        e.noteId = field(frag, u"note_id"_ustr);
        e.charCount = fieldInt(frag, u"chars"_ustr);
        e.byteSize = fieldInt64(frag, u"bytes"_ustr);
        e.pinned = fieldInt(frag, u"pinned"_ustr) != 0
                   || field(frag, u"pinned"_ustr) == u"true"_ustr
                   || field(frag, u"pinned"_ustr) == u"1"_ustr;
        e.tags = field(frag, u"tags"_ustr);
        if (!e.id.isEmpty())
            out.push_back(e);
        search = end;
    }
    return out;
}

OUString serializeIndex(const std::vector<NotebookMaterialIndexEntry>& items)
{
    OUStringBuffer b;
    b.append(u"{\n  \"schema_version\": \"v1-materials\",\n  \"items\": [\n"_ustr);
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i)
            b.append(u",\n"_ustr);
        const auto& e = items[i];
        b.append(u"    {\"id\":\""_ustr);
        appendEsc(b, e.id);
        b.append(u"\",\"title\":\""_ustr);
        appendEsc(b, e.title);
        b.append(u"\",\"kind\":\""_ustr);
        appendEsc(b, e.kind);
        b.append(u"\",\"source\":\""_ustr);
        appendEsc(b, e.sourcePath);
        b.append(u"\",\"ext\":\""_ustr);
        appendEsc(b, e.mimeOrExt);
        b.append(u"\",\"created\":\""_ustr);
        appendEsc(b, e.createdIso);
        b.append(u"\",\"last_used\":\""_ustr);
        appendEsc(b, e.lastUsedIso);
        b.append(u"\",\"note_id\":\""_ustr);
        appendEsc(b, e.noteId);
        b.append(u"\",\"chars\":"_ustr);
        b.append(e.charCount);
        b.append(u",\"bytes\":"_ustr);
        b.append(static_cast<sal_Int32>(std::min<sal_Int64>(e.byteSize, 2000000000)));
        b.append(u",\"pinned\":"_ustr);
        b.append(e.pinned ? sal_Int32(1) : sal_Int32(0));
        b.append(u",\"tags\":\""_ustr);
        appendEsc(b, e.tags);
        b.append(u"\"}"_ustr);
    }
    b.append(u"\n  ]\n}\n"_ustr);
    return b.makeStringAndClear();
}

OUString snippetPath(const OUString& rId)
{
    return NotebookMaterialStore::materialsDir() + u"/"_ustr + rId + u".txt"_ustr;
}

OUString capSnippet(const OUString& r)
{
    if (r.getLength() <= kMaxSnippetChars)
        return r;
    return r.copy(0, kMaxSnippetChars) + u"\n…(已截断)"_ustr;
}

/// Decode PDF literal string body (after opening '('), handling nested parens & escapes.
OUString decodePdfLiteral(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const char c = raw[i];
        if (c == '\\' && i + 1 < raw.size())
        {
            const char n = raw[++i];
            if (n == 'n')
                out.push_back('\n');
            else if (n == 'r')
                out.push_back('\r');
            else if (n == 't')
                out.push_back('\t');
            else if (n == 'b' || n == 'f')
                continue;
            else if (n >= '0' && n <= '7')
            {
                int v = n - '0';
                int digits = 1;
                while (digits < 3 && i + 1 < raw.size() && raw[i + 1] >= '0' && raw[i + 1] <= '7')
                {
                    v = (v << 3) + (raw[++i] - '0');
                    ++digits;
                }
                out.push_back(static_cast<char>(v & 0xFF));
            }
            else
                out.push_back(n);
        }
        else
            out.push_back(c);
    }
    // Detect UTF-16BE BOM
    if (out.size() >= 2 && static_cast<unsigned char>(out[0]) == 0xFE
        && static_cast<unsigned char>(out[1]) == 0xFF)
    {
        OUStringBuffer ub;
        for (size_t i = 2; i + 1 < out.size(); i += 2)
        {
            const sal_Unicode u = (static_cast<sal_Unicode>(static_cast<unsigned char>(out[i])) << 8)
                                  | static_cast<unsigned char>(out[i + 1]);
            if (u == 0)
                continue;
            ub.append(u);
        }
        return ub.makeStringAndClear();
    }
    return OUString::fromUtf8(std::string_view(out.data(), out.size()));
}

OUString decodePdfHex(const std::string& hex)
{
    std::string bytes;
    bytes.reserve(hex.size() / 2);
    int val = 0;
    bool half = false;
    for (char c : hex)
    {
        int d = -1;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else
            continue;
        if (!half)
        {
            val = d;
            half = true;
        }
        else
        {
            bytes.push_back(static_cast<char>((val << 4) | d));
            half = false;
        }
    }
    if (half)
        bytes.push_back(static_cast<char>(val << 4));
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE
        && static_cast<unsigned char>(bytes[1]) == 0xFF)
    {
        OUStringBuffer ub;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2)
        {
            const sal_Unicode u
                = (static_cast<sal_Unicode>(static_cast<unsigned char>(bytes[i])) << 8)
                  | static_cast<unsigned char>(bytes[i + 1]);
            if (u)
                ub.append(u);
        }
        return ub.makeStringAndClear();
    }
    return OUString::fromUtf8(std::string_view(bytes.data(), bytes.size()));
}

bool looksLikeReadable(const OUString& s)
{
    if (s.getLength() < 2)
        return false;
    sal_Int32 letters = 0;
    sal_Int32 controls = 0;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if ((c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= 0x4E00 && c <= 0x9FFF)
            || (c >= 0x3400 && c <= 0x4DBF) || c == u' ' || c == u'\n' || c == u'\t' || c == u'.'
            || c == u',' || c == u':' || c == u'-' || (c >= u'0' && c <= u'9'))
            ++letters;
        if (c < 0x09 || (c > 0x0D && c < 0x20))
            ++controls;
    }
    return letters >= 2 && controls * 4 < s.getLength();
}

OUString harvestPdfStrings(const std::vector<char>& buf, sal_Int32 nMaxChars)
{
    OUStringBuffer out;
    const size_t n = buf.size();
    size_t i = 0;
    while (i < n && out.getLength() < nMaxChars)
    {
        const char c = buf[i];
        if (c == '(')
        {
            // parse balanced literal
            ++i;
            std::string lit;
            int depth = 1;
            bool esc = false;
            while (i < n && depth > 0)
            {
                const char ch = buf[i++];
                if (esc)
                {
                    lit.push_back('\\');
                    lit.push_back(ch);
                    esc = false;
                    continue;
                }
                if (ch == '\\')
                {
                    esc = true;
                    continue;
                }
                if (ch == '(')
                {
                    ++depth;
                    lit.push_back(ch);
                    continue;
                }
                if (ch == ')')
                {
                    --depth;
                    if (depth == 0)
                        break;
                    lit.push_back(ch);
                    continue;
                }
                lit.push_back(ch);
            }
            const OUString piece = decodePdfLiteral(lit);
            if (looksLikeReadable(piece))
            {
                if (out.getLength())
                    out.append(u' ');
                out.append(piece);
            }
            continue;
        }
        if (c == '<' && i + 1 < n && buf[i + 1] != '<')
        {
            ++i;
            std::string hex;
            while (i < n && buf[i] != '>')
            {
                hex.push_back(buf[i++]);
                if (hex.size() > 8000)
                    break;
            }
            if (i < n && buf[i] == '>')
                ++i;
            if (hex.size() >= 4)
            {
                const OUString piece = decodePdfHex(hex);
                if (looksLikeReadable(piece))
                {
                    if (out.getLength())
                        out.append(u' ');
                    out.append(piece);
                }
            }
            continue;
        }
        ++i;
    }
    // collapse whitespace
    OUString s = out.makeStringAndClear();
    OUStringBuffer cleaned;
    bool sp = false;
    for (sal_Int32 k = 0; k < s.getLength(); ++k)
    {
        const sal_Unicode ch = s[k];
        if (ch == u' ' || ch == u'\t' || ch == u'\r' || ch == u'\n')
        {
            if (!sp && cleaned.getLength())
            {
                cleaned.append(u' ');
                sp = true;
            }
            continue;
        }
        sp = false;
        cleaned.append(ch);
    }
    return cleaned.makeStringAndClear();
}

bool saveIndexEntry(const NotebookMaterial& m)
{
    auto items = parseIndex(readFileLimited(NotebookMaterialStore::indexPath(), 8 * 1024 * 1024));
    bool found = false;
    for (auto& e : items)
    {
        if (e.id == m.id)
        {
            e.title = m.title;
            e.kind = m.kind;
            e.sourcePath = m.sourcePath;
            e.mimeOrExt = m.mimeOrExt;
            e.createdIso = m.createdIso.isEmpty() ? e.createdIso : m.createdIso;
            e.lastUsedIso = m.lastUsedIso.isEmpty() ? e.lastUsedIso : m.lastUsedIso;
            e.noteId = m.noteId;
            e.charCount = m.charCount;
            e.byteSize = m.byteSize;
            e.pinned = m.pinned;
            e.tags = m.tags;
            found = true;
            break;
        }
    }
    if (!found)
    {
        NotebookMaterialIndexEntry e;
        e.id = m.id;
        e.title = m.title;
        e.kind = m.kind;
        e.sourcePath = m.sourcePath;
        e.mimeOrExt = m.mimeOrExt;
        e.createdIso = m.createdIso;
        e.lastUsedIso = m.lastUsedIso;
        e.noteId = m.noteId;
        e.charCount = m.charCount;
        e.byteSize = m.byteSize;
        e.pinned = m.pinned;
        e.tags = m.tags;
        items.insert(items.begin(), e);
    }
    return writeFile(NotebookMaterialStore::indexPath(), serializeIndex(items));
}

} // namespace

OUString NotebookMaterialStore::rootDir()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return u"/tmp/kqoffice-notebook"_ustr;
    return OUString::fromUtf8(home) + u"/.config/kqoffice/notebook"_ustr;
}

OUString NotebookMaterialStore::materialsDir() { return rootDir() + u"/materials"_ustr; }

OUString NotebookMaterialStore::indexPath() { return materialsDir() + u"/index.json"_ustr; }

OUString NotebookMaterialStore::newId()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "mat-%u-%u", static_cast<unsigned>(tv.Seconds),
                  static_cast<unsigned>(tv.Nanosec % 1000000));
    return OUString::fromUtf8(buf);
}

OUString NotebookMaterialStore::detectKind(const OUString& rPathOrExt)
{
    OUString ext = rPathOrExt;
    if (ext.indexOf(u'/') >= 0 || ext.indexOf(u'\\') >= 0)
        ext = extOf(rPathOrExt);
    else
        ext = ext.toAsciiLowerCase();

    if (ext == u"md"_ustr || ext == u"markdown"_ustr)
        return u"markdown"_ustr;
    if (ext == u"txt"_ustr || ext == u"log"_ustr || ext == u"text"_ustr)
        return u"text"_ustr;
    if (ext == u"csv"_ustr || ext == u"tsv"_ustr)
        return u"csv"_ustr;
    if (ext == u"json"_ustr || ext == u"xml"_ustr || ext == u"html"_ustr || ext == u"htm"_ustr
        || ext == u"css"_ustr || ext == u"js"_ustr || ext == u"ts"_ustr || ext == u"py"_ustr
        || ext == u"cxx"_ustr || ext == u"hxx"_ustr || ext == u"java"_ustr)
        return u"text"_ustr;
    if (ext == u"png"_ustr || ext == u"jpg"_ustr || ext == u"jpeg"_ustr || ext == u"gif"_ustr
        || ext == u"webp"_ustr || ext == u"bmp"_ustr || ext == u"heic"_ustr)
        return u"image"_ustr;
    if (ext == u"pdf"_ustr)
        return u"pdf"_ustr;
    if (ext == u"odt"_ustr || ext == u"ods"_ustr || ext == u"odp"_ustr || ext == u"doc"_ustr
        || ext == u"docx"_ustr || ext == u"xls"_ustr || ext == u"xlsx"_ustr || ext == u"ppt"_ustr
        || ext == u"pptx"_ustr || ext == u"rtf"_ustr)
        return u"office"_ustr;
    if (ext == u"mp4"_ustr || ext == u"mov"_ustr || ext == u"mkv"_ustr || ext == u"webm"_ustr
        || ext == u"avi"_ustr || ext == u"m4v"_ustr || ext == u"wmv"_ustr || ext == u"mpeg"_ustr
        || ext == u"mpg"_ustr)
        return u"video"_ustr;
    if (ext == u"mp3"_ustr || ext == u"wav"_ustr || ext == u"m4a"_ustr || ext == u"aac"_ustr
        || ext == u"flac"_ustr || ext == u"ogg"_ustr || ext == u"opus"_ustr || ext == u"aiff"_ustr
        || ext == u"aif"_ustr || ext == u"wma"_ustr)
        return u"audio"_ustr;
    if (ext == u"srt"_ustr || ext == u"vtt"_ustr || ext == u"ass"_ustr || ext == u"ssa"_ustr)
        return u"subtitle"_ustr;
    if (ext == u"http"_ustr || ext == u"https"_ustr || rPathOrExt.startsWith(u"http://"_ustr)
        || rPathOrExt.startsWith(u"https://"_ustr))
        return u"url"_ustr;
    return u"other"_ustr;
}

std::vector<NotebookMaterialIndexEntry>
NotebookMaterialStore::listMaterials(const OUString& rNoteIdFilter)
{
    osl::MutexGuard g(matMutex());
    ensureDirSys(rootDir());
    ensureDirSys(materialsDir());
    auto all = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    std::vector<NotebookMaterialIndexEntry> out;
    if (rNoteIdFilter.isEmpty())
        out = std::move(all);
    else
    {
        for (const auto& e : all)
            if (e.noteId == rNoteIdFilter || e.noteId.isEmpty())
                out.push_back(e);
    }
    // Pinned first, then most recently used, then newest created
    std::sort(out.begin(), out.end(),
              [](const NotebookMaterialIndexEntry& a, const NotebookMaterialIndexEntry& b) {
                  if (a.pinned != b.pinned)
                      return a.pinned && !b.pinned;
                  const OUString ka = a.lastUsedIso.isEmpty() ? a.createdIso : a.lastUsedIso;
                  const OUString kb = b.lastUsedIso.isEmpty() ? b.createdIso : b.lastUsedIso;
                  if (ka != kb)
                      return ka > kb;
                  return a.id > b.id;
              });
    return out;
}

std::vector<NotebookMaterialIndexEntry>
NotebookMaterialStore::searchMaterials(const OUString& rQuery, const OUString& rNoteIdFilter,
                                       sal_Int32 nMax)
{
    if (nMax < 1)
        nMax = 1;
    if (nMax > 200)
        nMax = 200;
    const OUString qRaw = rQuery.trim().toAsciiLowerCase();
    if (qRaw.isEmpty())
        return listMaterials(rNoteIdFilter);

    // Split multi-token query (space/comma)
    std::vector<OUString> tokens;
    {
        sal_Int32 start = 0;
        const sal_Int32 len = qRaw.getLength();
        while (start < len)
        {
            while (start < len
                   && (qRaw[start] == u' ' || qRaw[start] == u'\t' || qRaw[start] == u','
                       || qRaw[start] == u'，'))
                ++start;
            if (start >= len)
                break;
            sal_Int32 end = start;
            while (end < len && qRaw[end] != u' ' && qRaw[end] != u'\t' && qRaw[end] != u','
                   && qRaw[end] != u'，')
                ++end;
            if (end > start)
                tokens.push_back(qRaw.copy(start, end - start));
            start = end;
        }
    }
    if (tokens.empty())
        return listMaterials(rNoteIdFilter);

    // Split into tag tokens vs word tokens; | / or enables tag OR mode
    bool tagOrMode = false;
    std::vector<OUString> tagToks;
    std::vector<OUString> wordToks;
    for (const auto& tok : tokens)
    {
        if (tok == u"|"_ustr || tok == u"or"_ustr || tok == u"OR"_ustr || tok == u"||"_ustr)
        {
            tagOrMode = true;
            continue;
        }
        if (tok.startsWith(u"#"_ustr))
        {
            OUString t = tok.copy(1);
            // also support #a|#b glued
            const sal_Int32 pipe = t.indexOf(u'|');
            if (pipe >= 0)
            {
                tagOrMode = true;
                OUString left = t.copy(0, pipe).trim();
                OUString right = t.copy(pipe + 1).trim();
                if (right.startsWith(u"#"_ustr))
                    right = right.copy(1);
                if (!left.isEmpty())
                    tagToks.push_back(left);
                if (!right.isEmpty())
                    tagToks.push_back(right);
            }
            else if (!t.isEmpty())
                tagToks.push_back(t);
        }
        else if (!tok.isEmpty())
            wordToks.push_back(tok);
    }
    if (tagToks.empty() && wordToks.empty())
        return listMaterials(rNoteIdFilter);

    auto hasExactTag = [](const OUString& rTags, const OUString& rWant) -> bool {
        for (const auto& t : splitTags(rTags))
            if (t.equalsIgnoreAsciiCase(rWant))
                return true;
        return false;
    };

    const auto base = listMaterials(rNoteIdFilter);
    struct Scored
    {
        NotebookMaterialIndexEntry e;
        sal_Int32 score = 0;
    };
    std::vector<Scored> scored;
    scored.reserve(base.size());

    for (const auto& e : base)
    {
        const OUString title = e.title.toAsciiLowerCase();
        const OUString path = e.sourcePath.toAsciiLowerCase();
        const OUString kind = e.kind.toAsciiLowerCase();
        const OUString tagsLc = e.tags.toAsciiLowerCase();
        const NotebookMaterial full = loadMaterial(e.id);
        const OUString body = full.snippet.toAsciiLowerCase();

        sal_Int32 score = 0;
        bool ok = true;

        // Tag filter
        if (!tagToks.empty())
        {
            if (tagOrMode)
            {
                bool any = false;
                for (const auto& t : tagToks)
                {
                    if (hasExactTag(e.tags, t) || tagsLc.indexOf(t.toAsciiLowerCase()) >= 0)
                    {
                        any = true;
                        score += 120;
                        break;
                    }
                }
                if (!any)
                    ok = false;
            }
            else
            {
                for (const auto& t : tagToks)
                {
                    if (hasExactTag(e.tags, t) || tagsLc.indexOf(t.toAsciiLowerCase()) >= 0)
                        score += 120;
                    else
                    {
                        ok = false;
                        break;
                    }
                }
            }
        }
        if (!ok)
            continue;

        // Word filter (AND)
        for (const auto& tok : wordToks)
        {
            const OUString t = tok.toAsciiLowerCase();
            if (t.isEmpty())
                continue;
            sal_Int32 part = 0;
            if (title.indexOf(t) >= 0)
                part += 100;
            if (path.indexOf(t) >= 0)
                part += 40;
            if (kind.indexOf(t) >= 0)
                part += 20;
            if (body.indexOf(t) >= 0)
                part += 10;
            if (title.startsWith(t))
                part += 30;
            // words may also hit tags
            if (tagsLc.indexOf(t) >= 0)
                part += 80;
            if (part == 0)
            {
                ok = false;
                break;
            }
            score += part;
        }
        if (!ok)
            continue;
        // slight boost for longer extracted bodies (more useful context)
        if (e.charCount > 500)
            score += 5;
        if (e.pinned)
            score += 8;
        scored.push_back(Scored{ e, score });
    }

    // Rank by relevance, then pinned, then most-recently used
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.e.pinned != b.e.pinned)
            return a.e.pinned && !b.e.pinned;
        const OUString ka = a.e.lastUsedIso.isEmpty() ? a.e.createdIso : a.e.lastUsedIso;
        const OUString kb = b.e.lastUsedIso.isEmpty() ? b.e.createdIso : b.e.lastUsedIso;
        if (ka != kb)
            return ka > kb;
        return a.e.id > b.e.id;
    });

    std::vector<NotebookMaterialIndexEntry> out;
    out.reserve(static_cast<size_t>(std::min(nMax, static_cast<sal_Int32>(scored.size()))));
    for (size_t i = 0; i < scored.size() && static_cast<sal_Int32>(i) < nMax; ++i)
        out.push_back(scored[i].e);
    return out;
}

NotebookMaterial NotebookMaterialStore::loadMaterial(const OUString& rId)
{
    osl::MutexGuard g(matMutex());
    NotebookMaterial m;
    m.id = rId;
    if (rId.isEmpty())
        return m;
    for (const auto& e : parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024)))
    {
        if (e.id == rId)
        {
            m.title = e.title;
            m.kind = e.kind;
            m.sourcePath = e.sourcePath;
            m.mimeOrExt = e.mimeOrExt;
            m.createdIso = e.createdIso;
            m.lastUsedIso = e.lastUsedIso;
            m.noteId = e.noteId;
            m.charCount = e.charCount;
            m.byteSize = e.byteSize;
            m.pinned = e.pinned;
            m.tags = e.tags;
            break;
        }
    }
    m.snippet = readFileLimited(snippetPath(rId), kMaxSnippetChars * 4);
    if (m.charCount == 0)
        m.charCount = m.snippet.getLength();
    return m;
}

bool NotebookMaterialStore::removeMaterial(const OUString& rId)
{
    if (rId.isEmpty())
        return false;
    osl::MutexGuard g(matMutex());
    OUString url;
    const OUString path = snippetPath(rId);
    if (osl::FileBase::getFileURLFromSystemPath(path, url) == osl::FileBase::E_None)
        osl::File::remove(url);
    auto items = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    std::vector<NotebookMaterialIndexEntry> next;
    for (const auto& e : items)
        if (e.id != rId)
            next.push_back(e);
    return writeFile(indexPath(), serializeIndex(next));
}

NotebookMaterial NotebookMaterialStore::importFile(const OUString& rSystemPath,
                                                   const OUString& rNoteId)
{
    NotebookMaterial m;
    if (rSystemPath.isEmpty())
        return m;

    osl::MutexGuard g(matMutex());
    ensureDirSys(rootDir());
    ensureDirSys(materialsDir());

    m.id = newId();
    m.title = fileNameOf(rSystemPath);
    m.sourcePath = rSystemPath;
    m.mimeOrExt = extOf(rSystemPath);
    m.kind = detectKind(rSystemPath);
    m.createdIso = nowIso();
    m.noteId = rNoteId;
    m.byteSize = fileSizeSys(rSystemPath);

    OUString extracted;
    if (m.kind == u"text"_ustr || m.kind == u"markdown"_ustr || m.kind == u"csv"_ustr)
    {
        extracted = readFileLimited(rSystemPath, kMaxReadBytes);
    }
    else if (m.kind == u"image"_ustr)
    {
        extracted = u"[图片材料] "_ustr + m.title + u"\n路径: "_ustr + rSystemPath
                    + u"\n（本地引用；可在笔记中用 @截图 描述）"_ustr;
    }
    else if (m.kind == u"pdf"_ustr)
    {
        // Baseline harvest (no PDFium). UI layer may replaceSnippet with deeper extract.
        const OUString harvested = extractPdfTextLightweight(rSystemPath, kMaxSnippetChars);
        if (harvested.getLength() >= 40)
        {
            extracted = u"[PDF 材料 · 本地提取] "_ustr + m.title + u"\n路径: "_ustr + rSystemPath
                        + u"\n\n"_ustr + harvested;
        }
        else
        {
            extracted = u"[PDF 材料] "_ustr + m.title + u"\n路径: "_ustr + rSystemPath
                        + u"\n大小: "_ustr + OUString::number(m.byteSize)
                        + u" 字节\n（轻量提取字数较少；打开记事本导入时将尝试 PDFium 深度提取）"_ustr;
            if (!harvested.isEmpty())
            {
                extracted += u"\n\n"_ustr;
                extracted += harvested;
            }
        }
    }
    else if (m.kind == u"office"_ustr)
    {
        extracted = u"[办公文稿材料] "_ustr + m.title + u"\n路径: "_ustr + rSystemPath
                    + u"\n（导入时将尝试 ZIP/XML 文本提取；失败则保留路径引用）"_ustr;
    }
    else if (m.kind == u"subtitle"_ustr)
    {
        const OUString raw = readFileLimited(rSystemPath, kMaxReadBytes);
        const OUString plain = stripSubtitleMarkup(raw);
        extracted = u"[字幕材料] "_ustr + m.title + u"\n路径: "_ustr + rSystemPath + u"\n\n"_ustr
                    + (plain.isEmpty() ? raw : plain);
    }
    else if (m.kind == u"video"_ustr || m.kind == u"audio"_ustr)
    {
        // Local media: keep path inventory + auto-attach same-basename .srt/.vtt/.txt
        extracted = (m.kind == u"video"_ustr ? u"[本地视频材料] "_ustr : u"[本地音频材料] "_ustr)
                    + m.title + u"\n路径: "_ustr + rSystemPath + u"\n大小: "_ustr
                    + OUString::number(m.byteSize)
                    + u" 字节\n说明: 可圈笔记不上传云端。问答依据「同名字幕/转录」或你粘贴的旁白文本。"
                      u"\n建议: 将同名 .srt/.vtt 与媒体放在同一文件夹；或点「字幕/转录」粘贴文稿。\n"_ustr;
        const OUString side = loadSidecarTranscript(rSystemPath);
        if (!side.isEmpty())
            extracted += side;
        else
            extracted += u"\n（未找到同名字幕。可用「字幕/转录」粘贴，或选中后点「本机转写」"
                         u"（需本机 ffmpeg + whisper，不上传云端）。）"_ustr;
    }
    else
    {
        // try text extract anyway
        extracted = readFileLimited(rSystemPath, kMaxReadBytes);
        if (extracted.isEmpty())
            extracted = u"[材料] "_ustr + m.title + u"\n路径: "_ustr + rSystemPath;
    }

    m.snippet = capSnippet(extracted);
    m.charCount = m.snippet.getLength();
    if (!writeFile(snippetPath(m.id), m.snippet))
        return NotebookMaterial();
    if (!saveIndexEntry(m))
        return NotebookMaterial();
    return m;
}

NotebookMaterial NotebookMaterialStore::importText(const OUString& rTitle, const OUString& rBody,
                                                   const OUString& rNoteId)
{
    NotebookMaterial m;
    if (rBody.isEmpty())
        return m;
    osl::MutexGuard g(matMutex());
    ensureDirSys(rootDir());
    ensureDirSys(materialsDir());
    m.id = newId();
    m.title = rTitle.isEmpty() ? u"粘贴文本"_ustr : rTitle;
    m.kind = u"text"_ustr;
    m.mimeOrExt = u"txt"_ustr;
    m.createdIso = nowIso();
    m.noteId = rNoteId;
    m.snippet = capSnippet(rBody);
    m.charCount = m.snippet.getLength();
    m.byteSize = m.charCount;
    if (!writeFile(snippetPath(m.id), m.snippet))
        return NotebookMaterial();
    if (!saveIndexEntry(m))
        return NotebookMaterial();
    return m;
}

OUString NotebookMaterialStore::normalizeSubtitleOrTranscript(const OUString& rRaw)
{
    if (rRaw.isEmpty())
        return OUString();
    // If looks like SRT/VTT (has -->), strip markup; else keep plain
    if (rRaw.indexOf(u"-->"_ustr) >= 0 || rRaw.startsWith(u"WEBVTT"_ustr))
    {
        const OUString plain = stripSubtitleMarkup(rRaw);
        return plain.isEmpty() ? rRaw : plain;
    }
    return rRaw;
}

NotebookMaterial NotebookMaterialStore::importTranscript(const OUString& rTitle,
                                                         const OUString& rBody,
                                                         const OUString& rKindHint)
{
    NotebookMaterial m;
    if (rBody.isEmpty())
        return m;
    const OUString plain = normalizeSubtitleOrTranscript(rBody);
    if (plain.isEmpty())
        return m;
    osl::MutexGuard g(matMutex());
    ensureDirSys(rootDir());
    ensureDirSys(materialsDir());
    m.id = newId();
    m.title = rTitle.isEmpty() ? u"字幕/转录"_ustr : rTitle;
    if (rKindHint == u"subtitle"_ustr)
        m.kind = u"subtitle"_ustr;
    else if (rKindHint == u"text"_ustr)
        m.kind = u"text"_ustr;
    else
        m.kind = u"subtitle"_ustr; // transcript treated as subtitle-class for grounding
    m.mimeOrExt = u"transcript"_ustr;
    m.createdIso = nowIso();
    m.snippet = capSnippet(u"[字幕/转录] "_ustr + m.title + u"\n\n"_ustr + plain);
    m.charCount = m.snippet.getLength();
    m.byteSize = m.charCount;
    if (!writeFile(snippetPath(m.id), m.snippet))
        return NotebookMaterial();
    if (!saveIndexEntry(m))
        return NotebookMaterial();
    return m;
}

namespace
{
OUString firstExistingPath(const std::vector<OUString>& cands)
{
    for (const auto& p : cands)
    {
        if (p.isEmpty())
            continue;
        if (fileExistsSys(p))
            return p;
    }
    return OUString();
}

OUString whichOnPath(const char* name)
{
    // Lightweight: check common install locations + PATH via `command -v` is heavy;
    // probe fixed prefixes first.
    const OUString n = OUString::fromUtf8(name);
    std::vector<OUString> cands;
    cands.push_back(u"/opt/homebrew/bin/"_ustr + n);
    cands.push_back(u"/usr/local/bin/"_ustr + n);
    cands.push_back(u"/usr/bin/"_ustr + n);
    const char* home = std::getenv("HOME");
    if (home && *home)
    {
        const OUString h = OUString::fromUtf8(home);
        cands.push_back(h + u"/.local/bin/"_ustr + n);
        cands.push_back(h + u"/Library/Python/3.12/bin/"_ustr + n);
        cands.push_back(h + u"/Library/Python/3.11/bin/"_ustr + n);
        cands.push_back(h + u"/Library/Python/3.10/bin/"_ustr + n);
    }
    // PATH scan
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv)
    {
        const OUString path = OUString::fromUtf8(pathEnv);
        sal_Int32 start = 0;
        while (start <= path.getLength())
        {
            sal_Int32 colon = path.indexOf(u':', start);
            if (colon < 0)
                colon = path.getLength();
            if (colon > start)
            {
                OUString dir = path.copy(start, colon - start);
                if (!dir.isEmpty())
                    cands.push_back(dir + u"/"_ustr + n);
            }
            start = colon + 1;
            if (colon >= path.getLength())
                break;
        }
    }
    return firstExistingPath(cands);
}

OUString shellSingleQuote(const OUString& s)
{
    OUStringBuffer b;
    b.append(u'\'');
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        if (s[i] == u'\'')
            b.append(u"'\\''"_ustr);
        else
            b.append(s[i]);
    }
    b.append(u'\'');
    return b.makeStringAndClear();
}
} // namespace

bool NotebookMaterialStore::isLocalMediaMaterial(const NotebookMaterial& rMat)
{
    return (rMat.kind == u"video"_ustr || rMat.kind == u"audio"_ustr)
           && !rMat.sourcePath.isEmpty();
}

NotebookMaterialStore::LocalAsrDiagnostics NotebookMaterialStore::diagnoseLocalAsr()
{
    LocalAsrDiagnostics d;
    d.ffmpegPath = whichOnPath("ffmpeg");
    d.hasFfmpeg = !d.ffmpegPath.isEmpty();

    // Prefer openai-whisper CLI, then whisper.cpp family, then mlx
    struct Cand
    {
        const char* bin;
        const char* kind;
    };
    static const Cand kWhisper[] = {
        { "whisper", "openai-whisper" },
        { "whisper-cli", "whisper-cpp" },
        { "whisper-cpp", "whisper-cpp" },
        { "main", "whisper-cpp" }, // rare; only if named main in path
        { "mlx_whisper", "mlx" },
        { "faster-whisper", "faster-whisper" },
    };
    for (const auto& c : kWhisper)
    {
        // skip bare "main" unless under whisper-ish dir
        OUString p = whichOnPath(c.bin);
        if (p.isEmpty())
            continue;
        if (OUString::fromUtf8(c.bin) == u"main"_ustr
            && p.indexOf(u"whisper"_ustr) < 0 && p.indexOf(u"Whisper"_ustr) < 0)
            continue;
        d.whisperPath = p;
        d.whisperKind = OUString::fromUtf8(c.kind);
        d.hasWhisper = true;
        break;
    }

    if (d.hasFfmpeg && d.hasWhisper)
    {
        d.summary = u"本机 ASR 就绪 · ffmpeg + "_ustr + d.whisperKind + u" ("_ustr
                    + fileNameOf(d.whisperPath) + u")"_ustr;
        d.installHint.clear();
    }
    else if (d.hasFfmpeg && !d.hasWhisper)
    {
        d.summary = u"已检测到 ffmpeg，未检测到 whisper"_ustr;
        d.installHint = u"安装任选其一（本机，不上传）：\n"
                        u"  pip install -U openai-whisper\n"
                        u"  brew install whisper-cpp\n"
                        u"装好后重启可圈办公，再点「本机转写」。"_ustr;
    }
    else if (!d.hasFfmpeg && d.hasWhisper)
    {
        d.summary = u"已检测到 whisper，未检测到 ffmpeg"_ustr;
        d.installHint = u"请安装 ffmpeg：brew install ffmpeg"_ustr;
    }
    else
    {
        d.summary = u"未检测到本机 ASR 工具"_ustr;
        d.installHint = u"推荐：\n"
                        u"  brew install ffmpeg\n"
                        u"  pip install -U openai-whisper\n"
                        u"或 brew install whisper-cpp\n"
                        u"仅在本机运行，媒体不会上传。"_ustr;
    }
    return d;
}

OUString NotebookMaterialStore::transcribeLocalMedia(const OUString& rMediaSystemPath,
                                                     OUString& rStatusOut, sal_Int32 nTimeoutSec,
                                                     const std::function<void()>& rOnTick)
{
    rStatusOut.clear();
    if (rMediaSystemPath.isEmpty() || !fileExistsSys(rMediaSystemPath))
    {
        rStatusOut = u"媒体文件不存在"_ustr;
        return OUString();
    }
    const LocalAsrDiagnostics diag = diagnoseLocalAsr();
    if (!diag.hasFfmpeg || !diag.hasWhisper)
    {
        rStatusOut = diag.summary + u"\n"_ustr + diag.installHint;
        return OUString();
    }
    if (nTimeoutSec < 30)
        nTimeoutSec = 30;
    if (nTimeoutSec > 3600)
        nTimeoutSec = 3600;

    const OUString work = rootDir() + u"/asr-work"_ustr;
    ensureDirSys(work);
    // unique job dir
    const OUString job = work + u"/"_ustr + newId();
    ensureDirSys(job);
    const OUString wav = job + u"/audio.wav"_ustr;
    const OUString result = job + u"/result.txt"_ustr;
    const OUString status = job + u"/status.txt"_ustr;
    const OUString script = job + u"/run.sh"_ustr;
    const OUString log = job + u"/run.log"_ustr;

    OUStringBuffer sh;
    sh.append(u"#!/bin/bash\nset +e\n"
              u"JOB="_ustr);
    sh.append(shellSingleQuote(job));
    sh.append(u"\nMEDIA="_ustr);
    sh.append(shellSingleQuote(rMediaSystemPath));
    sh.append(u"\nFFMPEG="_ustr);
    sh.append(shellSingleQuote(diag.ffmpegPath));
    sh.append(u"\nWHISPER="_ustr);
    sh.append(shellSingleQuote(diag.whisperPath));
    sh.append(u"\nKIND="_ustr);
    sh.append(shellSingleQuote(diag.whisperKind));
    sh.append(u"\nWAV=\"$JOB/audio.wav\"\n"
              u"echo RUNNING > \"$JOB/status.txt\"\n"
              u"\"$FFMPEG\" -y -i \"$MEDIA\" -ar 16000 -ac 1 -c:a pcm_s16le \"$WAV\" "
              u">\"$JOB/ffmpeg.log\" 2>&1\n"
              u"if [ ! -s \"$WAV\" ]; then echo FFMPEG_FAIL > \"$JOB/status.txt\"; exit 1; fi\n"
              u"case \"$KIND\" in\n"
              u"  openai-whisper)\n"
              u"    \"$WHISPER\" \"$WAV\" --language Chinese --model base --task transcribe "
              u"--output_format txt --output_dir \"$JOB\" >\"$JOB/whisper.log\" 2>&1\n"
              u"    if [ -f \"$JOB/audio.txt\" ]; then cp \"$JOB/audio.txt\" \"$JOB/result.txt\"; fi\n"
              u"    ;;\n"
              u"  whisper-cpp)\n"
              u"    # try common flags; models left to user env WHISPER_CPP_MODEL\n"
              u"    MODEL=\"${WHISPER_CPP_MODEL:-}\"\n"
              u"    if [ -z \"$MODEL\" ]; then\n"
              u"      for m in /opt/homebrew/share/whisper-cpp/*.bin "
              u"/usr/local/share/whisper-cpp/*.bin \"$HOME/.cache/whisper-cpp\"/*.bin; do\n"
              u"        [ -f \"$m\" ] && MODEL=\"$m\" && break\n"
              u"      done\n"
              u"    fi\n"
              u"    if [ -n \"$MODEL\" ]; then\n"
              u"      \"$WHISPER\" -m \"$MODEL\" -f \"$WAV\" -l zh -otxt -of \"$JOB/out\" "
              u">\"$JOB/whisper.log\" 2>&1\n"
              u"      [ -f \"$JOB/out.txt\" ] && cp \"$JOB/out.txt\" \"$JOB/result.txt\"\n"
              u"    else\n"
              u"      \"$WHISPER\" -f \"$WAV\" -l zh -otxt -of \"$JOB/out\" "
              u">\"$JOB/whisper.log\" 2>&1\n"
              u"      [ -f \"$JOB/out.txt\" ] && cp \"$JOB/out.txt\" \"$JOB/result.txt\"\n"
              u"    fi\n"
              u"    ;;\n"
              u"  mlx)\n"
              u"    \"$WHISPER\" \"$WAV\" --language zh -o \"$JOB/result.txt\" "
              u">\"$JOB/whisper.log\" 2>&1 || "
              u"\"$WHISPER\" \"$WAV\" >\"$JOB/result.txt\" 2>\"$JOB/whisper.log\"\n"
              u"    ;;\n"
              u"  *)\n"
              u"    \"$WHISPER\" \"$WAV\" >\"$JOB/result.txt\" 2>\"$JOB/whisper.log\"\n"
              u"    ;;\n"
              u"esac\n"
              u"if [ -s \"$JOB/result.txt\" ]; then\n"
              u"  echo OK > \"$JOB/status.txt\"\n"
              u"else\n"
              u"  echo WHISPER_FAIL > \"$JOB/status.txt\"\n"
              u"  exit 2\n"
              u"fi\n"_ustr);

    if (!writeFile(script, sh.makeStringAndClear()))
    {
        rStatusOut = u"无法写入转写脚本"_ustr;
        return OUString();
    }

    // chmod +x via shell
    {
        OUString chmodScript = u"#!/bin/bash\nchmod +x "_ustr + shellSingleQuote(script) + u"\n"_ustr;
        const OUString chmodPath = job + u"/chmod.sh"_ustr;
        writeFile(chmodPath, chmodScript);
        rtl_uString* args[2] = {};
        OUString bash(u"/bin/bash"_ustr);
        args[0] = bash.pData;
        args[1] = chmodPath.pData;
        oslProcess hp = nullptr;
        osl_executeProcess(bash.pData, args + 1, 1, osl_Process_WAIT, nullptr, nullptr, nullptr, 0,
                           &hp);
        if (hp)
            osl_freeProcessHandle(hp);
    }

    rtl_uString* pArgs[2] = {};
    OUString bash(u"/bin/bash"_ustr);
    pArgs[0] = bash.pData;
    pArgs[1] = script.pData;
    oslProcess hProc = nullptr;
    if (osl_executeProcess(bash.pData, pArgs + 1, 1, osl_Process_NORMAL, nullptr, nullptr, nullptr,
                           0, &hProc)
        != osl_Process_E_None || !hProc)
    {
        rStatusOut = u"无法启动本机转写进程"_ustr;
        return OUString();
    }

    // Poll until done or timeout (caller may Reschedule between sleeps externally;
    // we still sleep in short slices).
    const sal_Int32 slices = nTimeoutSec * 2; // 500ms
    bool done = false;
    for (sal_Int32 i = 0; i < slices; ++i)
    {
        if (rOnTick)
            rOnTick();
        TimeValue tv{ 0, 500000000 }; // 0.5s
        osl_waitThread(&tv);
        if (rOnTick)
            rOnTick();
        oslProcessInfo info{};
        info.Size = sizeof(info);
        if (osl_getProcessInfo(hProc, osl_Process_EXITCODE, &info) == osl_Process_E_None)
        {
            done = true;
            break;
        }
        // also check status file
        const OUString st = readFileLimited(status, 64).trim();
        if (st == u"OK"_ustr || st == u"FFMPEG_FAIL"_ustr || st == u"WHISPER_FAIL"_ustr)
        {
            // process may still be exiting
            if (st != u"RUNNING"_ustr)
            {
                TimeValue tv2{ 0, 200000000 };
                osl_waitThread(&tv2);
                done = true;
                break;
            }
        }
    }
    if (!done)
    {
        osl_terminateProcess(hProc);
        osl_freeProcessHandle(hProc);
        rStatusOut = u"本机转写超时（"_ustr + OUString::number(nTimeoutSec)
                     + u"s）。可换更短片段，或设置较小 whisper 模型后重试。日志: "_ustr + log;
        return OUString();
    }
    osl_freeProcessHandle(hProc);

    const OUString st = readFileLimited(status, 64).trim();
    if (st == u"FFMPEG_FAIL"_ustr)
    {
        rStatusOut = u"ffmpeg 提取音频失败。日志: "_ustr + job + u"/ffmpeg.log"_ustr;
        return OUString();
    }
    if (st != u"OK"_ustr)
    {
        rStatusOut = u"whisper 转写失败（"_ustr + st + u"）。请确认模型已下载。"
                     u" openai-whisper 首次会下载 base 模型。"
                     u" whisper-cpp 可设置环境变量 WHISPER_CPP_MODEL=模型.bin 路径。"
                     u"\n日志: "_ustr
                     + job + u"/whisper.log"_ustr;
        return OUString();
    }
    OUString text = readFileLimited(result, kMaxSnippetChars).trim();
    if (text.isEmpty())
    {
        rStatusOut = u"转写结果为空"_ustr;
        return OUString();
    }
    // If whisper dumped SRT/VTT-like, keep dialogue only
    if (text.indexOf(u"-->"_ustr) >= 0 || text.startsWith(u"WEBVTT"_ustr))
    {
        const OUString plain = stripSubtitleMarkup(text);
        if (!plain.isEmpty())
            text = plain;
    }
    rStatusOut = u"本机转写完成（"_ustr + diag.whisperKind + u" · "_ustr
                 + OUString::number(text.getLength()) + u" 字）· 未上传"_ustr;
    (void)wav;
    return text;
}

bool NotebookMaterialStore::linkToNote(const OUString& rMaterialId, const OUString& rNoteId)
{
    if (rMaterialId.isEmpty())
        return false;
    osl::MutexGuard g(matMutex());
    auto items = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    bool found = false;
    for (auto& e : items)
    {
        if (e.id == rMaterialId)
        {
            e.noteId = rNoteId;
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    return writeFile(indexPath(), serializeIndex(items));
}

bool NotebookMaterialStore::replaceSnippet(const OUString& rId, const OUString& rSnippet)
{
    if (rId.isEmpty() || rSnippet.isEmpty())
        return false;
    osl::MutexGuard g(matMutex());
    ensureDirSys(materialsDir());
    const OUString capped = capSnippet(rSnippet);
    if (!writeFile(snippetPath(rId), capped))
        return false;
    auto items = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    bool found = false;
    for (auto& e : items)
    {
        if (e.id == rId)
        {
            e.charCount = capped.getLength();
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    return writeFile(indexPath(), serializeIndex(items));
}

void NotebookMaterialStore::touchMaterials(const std::vector<OUString>& rIds)
{
    if (rIds.empty())
        return;
    osl::MutexGuard g(matMutex());
    auto items = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    const OUString now = nowIso();
    bool any = false;
    for (auto& e : items)
    {
        for (const auto& id : rIds)
        {
            if (e.id == id)
            {
                e.lastUsedIso = now;
                any = true;
                break;
            }
        }
    }
    if (any)
        writeFile(indexPath(), serializeIndex(items));
}

bool NotebookMaterialStore::setPinned(const OUString& rId, bool bPinned)
{
    if (rId.isEmpty())
        return false;
    osl::MutexGuard g(matMutex());
    auto items = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    bool found = false;
    for (auto& e : items)
    {
        if (e.id == rId)
        {
            e.pinned = bPinned;
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    return writeFile(indexPath(), serializeIndex(items));
}

sal_Int32 NotebookMaterialStore::togglePinned(const std::vector<OUString>& rIds)
{
    if (rIds.empty())
        return 0;
    osl::MutexGuard g(matMutex());
    auto items = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    sal_Int32 n = 0;
    for (auto& e : items)
    {
        for (const auto& id : rIds)
        {
            if (e.id == id)
            {
                e.pinned = !e.pinned;
                ++n;
                break;
            }
        }
    }
    if (n > 0)
        writeFile(indexPath(), serializeIndex(items));
    return n;
}

std::vector<NotebookMaterialIndexEntry>
NotebookMaterialStore::listPinnedMaterials(sal_Int32 nMax)
{
    if (nMax < 1)
        nMax = 1;
    if (nMax > 100)
        nMax = 100;
    auto all = listMaterials();
    std::vector<NotebookMaterialIndexEntry> out;
    out.reserve(static_cast<size_t>(nMax));
    for (const auto& e : all)
    {
        if (!e.pinned)
            continue;
        out.push_back(e);
        if (static_cast<sal_Int32>(out.size()) >= nMax)
            break;
    }
    return out;
}

OUString NotebookMaterialStore::normalizeTags(const OUString& rRaw)
{
    std::vector<OUString> tags;
    const OUString raw = rRaw.trim();
    if (raw.isEmpty())
        return OUString();
    sal_Int32 start = 0;
    const sal_Int32 len = raw.getLength();
    while (start < len)
    {
        while (start < len
               && (raw[start] == u' ' || raw[start] == u'\t' || raw[start] == u','
                   || raw[start] == u';' || raw[start] == u'#'))
            ++start;
        if (start >= len)
            break;
        sal_Int32 end = start;
        while (end < len && raw[end] != u' ' && raw[end] != u'\t' && raw[end] != u','
               && raw[end] != u';')
            ++end;
        OUString t = raw.copy(start, end - start).trim();
        while (t.startsWith(u"#"_ustr))
            t = t.copy(1);
        if (!t.isEmpty())
        {
            bool dup = false;
            for (const auto& e : tags)
                if (e.equalsIgnoreAsciiCase(t))
                {
                    dup = true;
                    break;
                }
            if (!dup)
                tags.push_back(t);
        }
        start = end;
    }
    OUStringBuffer b;
    for (size_t i = 0; i < tags.size(); ++i)
    {
        if (i)
            b.append(u' ');
        b.append(tags[i]);
    }
    return b.makeStringAndClear();
}

OUString NotebookMaterialStore::formatTagsDisplay(const OUString& rTags)
{
    const OUString n = normalizeTags(rTags);
    if (n.isEmpty())
        return OUString();
    OUStringBuffer b;
    for (const auto& t : splitTags(n))
    {
        if (!b.isEmpty())
            b.append(u' ');
        b.append(u'#');
        b.append(t);
    }
    return b.makeStringAndClear();
}

std::vector<OUString> NotebookMaterialStore::splitTags(const OUString& rTags)
{
    std::vector<OUString> out;
    const OUString n = normalizeTags(rTags);
    if (n.isEmpty())
        return out;
    sal_Int32 start = 0;
    while (start < n.getLength())
    {
        sal_Int32 sp = n.indexOf(u' ', start);
        if (sp < 0)
            sp = n.getLength();
        const OUString t = n.copy(start, sp - start);
        if (!t.isEmpty())
            out.push_back(t);
        start = sp + 1;
    }
    return out;
}

std::vector<MaterialTagCount> NotebookMaterialStore::listTagCloud(sal_Int32 nMax)
{
    if (nMax < 1)
        nMax = 1;
    if (nMax > 200)
        nMax = 200;
    std::map<OUString, sal_Int32> counts; // key = lowercase for merge
    std::map<OUString, OUString> display; // lower → first-seen display
    for (const auto& e : listMaterials())
    {
        for (const auto& t : splitTags(e.tags))
        {
            const OUString key = t.toAsciiLowerCase();
            counts[key] += 1;
            if (display.find(key) == display.end())
                display[key] = t;
        }
    }
    std::vector<MaterialTagCount> out;
    out.reserve(counts.size());
    for (const auto& kv : counts)
    {
        MaterialTagCount c;
        c.tag = display[kv.first];
        c.count = kv.second;
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const MaterialTagCount& a, const MaterialTagCount& b) {
        if (a.count != b.count)
            return a.count > b.count;
        return a.tag < b.tag;
    });
    if (static_cast<sal_Int32>(out.size()) > nMax)
        out.resize(static_cast<size_t>(nMax));
    return out;
}

OUString NotebookMaterialStore::formatTagCloudText(sal_Int32 nMax)
{
    const auto cloud = listTagCloud(nMax);
    if (cloud.empty())
        return u"（尚无标签 · 选中材料后在搜索框输入标签点「打标签」）"_ustr;
    OUStringBuffer b;
    for (size_t i = 0; i < cloud.size(); ++i)
    {
        if (i)
            b.append(u"  "_ustr);
        b.append(u'#');
        b.append(cloud[i].tag);
        b.append(u'×');
        b.append(cloud[i].count);
    }
    return b.makeStringAndClear();
}

sal_Int32 NotebookMaterialStore::renameTag(const OUString& rOldTag, const OUString& rNewTag)
{
    const OUString oldN = normalizeTags(rOldTag);
    const OUString newN = normalizeTags(rNewTag);
    if (oldN.isEmpty())
        return 0;
    // only first token of each
    const auto oldParts = splitTags(oldN);
    const auto newParts = splitTags(newN);
    if (oldParts.empty())
        return 0;
    const OUString oldTag = oldParts.front();
    const OUString newTag = newParts.empty() ? OUString() : newParts.front();
    if (oldTag.equalsIgnoreAsciiCase(newTag))
        return 0;

    osl::MutexGuard g(matMutex());
    auto items = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    sal_Int32 touched = 0;
    for (auto& e : items)
    {
        auto tags = splitTags(e.tags);
        if (tags.empty())
            continue;
        bool changed = false;
        std::vector<OUString> next;
        next.reserve(tags.size());
        for (const auto& t : tags)
        {
            if (t.equalsIgnoreAsciiCase(oldTag))
            {
                changed = true;
                if (!newTag.isEmpty())
                {
                    bool dup = false;
                    for (const auto& x : next)
                        if (x.equalsIgnoreAsciiCase(newTag))
                        {
                            dup = true;
                            break;
                        }
                    if (!dup)
                        next.push_back(newTag);
                }
            }
            else
                next.push_back(t);
        }
        if (!changed)
            continue;
        OUStringBuffer b;
        for (size_t i = 0; i < next.size(); ++i)
        {
            if (i)
                b.append(u' ');
            b.append(next[i]);
        }
        e.tags = b.makeStringAndClear();
        ++touched;
    }
    if (touched > 0)
        writeFile(indexPath(), serializeIndex(items));
    return touched;
}

bool NotebookMaterialStore::setTags(const OUString& rId, const OUString& rTags)
{
    if (rId.isEmpty())
        return false;
    osl::MutexGuard g(matMutex());
    auto items = parseIndex(readFileLimited(indexPath(), 8 * 1024 * 1024));
    bool found = false;
    const OUString norm = normalizeTags(rTags);
    for (auto& e : items)
    {
        if (e.id == rId)
        {
            e.tags = norm;
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    return writeFile(indexPath(), serializeIndex(items));
}

bool NotebookMaterialStore::addTags(const OUString& rId, const OUString& rTags)
{
    if (rId.isEmpty())
        return false;
    const NotebookMaterial m = loadMaterial(rId);
    if (m.id.isEmpty() && m.title.isEmpty())
        return false;
    OUString merged = m.tags;
    if (!merged.isEmpty() && !rTags.isEmpty())
        merged += u" "_ustr;
    merged += rTags;
    return setTags(rId, merged);
}

MaterialLibraryStats NotebookMaterialStore::summarizeLibrary()
{
    MaterialLibraryStats s;
    const auto items = listMaterials();
    s.total = static_cast<sal_Int32>(items.size());
    std::map<OUString, bool> tagSeen;
    for (const auto& e : items)
    {
        if (e.pinned)
            ++s.pinned;
        if (!e.tags.isEmpty())
            ++s.tagged;
        if (e.charCount > 0)
            ++s.withSnippet;
        s.totalChars += e.charCount;
        for (const auto& t : splitTags(e.tags))
            tagSeen[t.toAsciiLowerCase()] = true;
    }
    s.uniqueTags = static_cast<sal_Int32>(tagSeen.size());
    return s;
}

OUString NotebookMaterialStore::formatLibraryStats()
{
    const MaterialLibraryStats s = summarizeLibrary();
    OUStringBuffer b;
    b.append(u"材料库 "_ustr);
    b.append(s.total);
    b.append(u" 条 · 置顶 "_ustr);
    b.append(s.pinned);
    b.append(u" · 有标签 "_ustr);
    b.append(s.tagged);
    b.append(u" · 标签种 "_ustr);
    b.append(s.uniqueTags);
    b.append(u" · 有正文 "_ustr);
    b.append(s.withSnippet);
    b.append(u" · 约 "_ustr);
    b.append(static_cast<sal_Int32>(std::min<sal_Int64>(s.totalChars, 2000000000)));
    b.append(u" 字"_ustr);
    return b.makeStringAndClear();
}

OUString NotebookMaterialStore::materialDayKey(const NotebookMaterialIndexEntry& rEntry)
{
    OUString iso = rEntry.lastUsedIso;
    if (iso.isEmpty())
        iso = rEntry.createdIso;
    if (iso.getLength() >= 10)
        return iso.copy(0, 10); // YYYY-MM-DD
    return OUString();
}

void NotebookMaterialStore::requestFocusPinned()
{
    ensureDirSys(rootDir());
    const OUString path = rootDir() + u"/focus-pinned.flag"_ustr;
    writeFile(path, u"1\n"_ustr);
}

bool NotebookMaterialStore::consumeFocusPinned()
{
    const OUString path = rootDir() + u"/focus-pinned.flag"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    f.close();
    osl::File::remove(url);
    return true;
}

OUString NotebookMaterialStore::extractPdfTextLightweight(const OUString& rSystemPath,
                                                          sal_Int32 nMaxChars)
{
    if (rSystemPath.isEmpty())
        return OUString();
    if (nMaxChars < 100)
        nMaxChars = 100;
    if (nMaxChars > kMaxSnippetChars)
        nMaxChars = kMaxSnippetChars;

    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSystemPath, url) != osl::FileBase::E_None)
        return OUString();
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return OUString();
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz < 8)
    {
        f.close();
        return OUString();
    }
    // Cap raw scan size (PDFs can be huge; strings usually early/streamed)
    const sal_uInt64 take = std::min<sal_uInt64>(sz, 4 * 1024 * 1024);
    std::vector<char> buf(static_cast<size_t>(take));
    sal_uInt64 n = 0;
    f.read(buf.data(), take, n);
    f.close();
    buf.resize(static_cast<size_t>(n));
    if (n < 5 || !(buf[0] == '%' && buf[1] == 'P' && buf[2] == 'D' && buf[3] == 'F'))
    {
        // still try harvest
    }
    return harvestPdfStrings(buf, nMaxChars);
}

OUString NotebookMaterialStore::formatContextBlock(const std::vector<OUString>& rMaterialIds,
                                                   sal_Int32 nMaxChars)
{
    if (nMaxChars < 500)
        nMaxChars = 500;
    OUStringBuffer b;
    b.append(u"【本地材料库上下文】\n"_ustr);
    sal_Int32 used = b.getLength();
    sal_Int32 n = 0;
    for (const auto& id : rMaterialIds)
    {
        if (id.isEmpty())
            continue;
        const NotebookMaterial m = loadMaterial(id);
        if (m.id.isEmpty())
            continue;
        OUStringBuffer block;
        block.append(u"\n--- 材料: "_ustr);
        block.append(m.title);
        block.append(u" ("_ustr);
        block.append(m.kind);
        block.append(u") ---\n"_ustr);
        if (!m.sourcePath.isEmpty())
        {
            block.append(u"来源: "_ustr);
            block.append(m.sourcePath);
            block.append(u"\n"_ustr);
        }
        sal_Int32 remain = nMaxChars - used - block.getLength() - 32;
        if (remain < 80)
            break;
        OUString snip = m.snippet;
        if (snip.getLength() > remain)
            snip = snip.copy(0, remain) + u"\n…"_ustr;
        block.append(snip);
        block.append(u"\n"_ustr);
        b.append(block.makeStringAndClear());
        used = b.getLength();
        if (++n >= 8)
            break;
        if (used >= nMaxChars)
            break;
    }
    if (n == 0)
        b.append(u"（未选择材料）\n"_ustr);
    return b.makeStringAndClear();
}

} // namespace kqoffice::ai::notebook

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
