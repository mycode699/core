/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "LocalNotebookStore.hxx"
#include "NotebookMaterialStore.hxx"

#include <osl/file.hxx>
#include <osl/mutex.hxx>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace kqoffice::ai::notebook
{
namespace
{
osl::Mutex& nbMutex()
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

OUString readFile(const OUString& rSysPath)
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
    if (sz > 4 * 1024 * 1024)
    {
        f.close();
        return OUString();
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    return OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
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

bool fieldBool(const OUString& frag, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\":"_ustr;
    sal_Int32 p = frag.indexOf(needle);
    if (p < 0)
        return false;
    return frag.copy(p).indexOf(u"true"_ustr) >= 0
           && frag.copy(p).indexOf(u"true"_ustr) < 12;
}

std::vector<NotebookIndexEntry> parseIndex(const OUString& json)
{
    std::vector<NotebookIndexEntry> out;
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
        NotebookIndexEntry e;
        e.id = field(frag, u"id"_ustr);
        e.title = field(frag, u"title"_ustr);
        e.updatedIso = field(frag, u"updated"_ustr);
        e.pinned = fieldBool(frag, u"pinned"_ustr);
        if (!e.id.isEmpty())
            out.push_back(e);
        search = end;
    }
    return out;
}

OUString serializeIndex(const std::vector<NotebookIndexEntry>& items)
{
    OUStringBuffer b;
    b.append(u"{\n  \"schema_version\": \"v1-notebook\",\n  \"items\": [\n"_ustr);
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i)
            b.append(u",\n"_ustr);
        const auto& e = items[i];
        b.append(u"    {\"id\":\""_ustr);
        appendEsc(b, e.id);
        b.append(u"\",\"title\":\""_ustr);
        appendEsc(b, e.title);
        b.append(u"\",\"updated\":\""_ustr);
        appendEsc(b, e.updatedIso);
        b.append(u"\",\"pinned\":"_ustr);
        b.append(e.pinned ? u"true"_ustr : u"false"_ustr);
        b.append(u"}"_ustr);
    }
    b.append(u"\n  ]\n}\n"_ustr);
    return b.makeStringAndClear();
}
} // namespace

OUString LocalNotebookStore::rootDir()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return u"/tmp/kqoffice-notebook"_ustr;
    return OUString::fromUtf8(home) + u"/.config/kqoffice/notebook"_ustr;
}

OUString LocalNotebookStore::notesDir() { return rootDir() + u"/notes"_ustr; }

OUString LocalNotebookStore::indexPath() { return rootDir() + u"/index.json"_ustr; }

OUString LocalNotebookStore::newId()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "note-%u-%u", static_cast<unsigned>(tv.Seconds),
                  static_cast<unsigned>(tv.Nanosec % 1000000));
    return OUString::fromUtf8(buf);
}

std::vector<NotebookIndexEntry> LocalNotebookStore::listNotes()
{
    osl::MutexGuard g(nbMutex());
    ensureDirSys(rootDir());
    ensureDirSys(notesDir());
    return parseIndex(readFile(indexPath()));
}

NotebookNote LocalNotebookStore::loadNote(const OUString& rId)
{
    osl::MutexGuard g(nbMutex());
    NotebookNote n;
    n.id = rId;
    if (rId.isEmpty())
        return n;
    n.body = readFile(notesDir() + u"/"_ustr + rId + u".md"_ustr);
    // title from first line or index
    for (const auto& e : parseIndex(readFile(indexPath())))
    {
        if (e.id == rId)
        {
            n.title = e.title;
            n.updatedIso = e.updatedIso;
            n.pinned = e.pinned;
            break;
        }
    }
    if (n.title.isEmpty() && !n.body.isEmpty())
    {
        sal_Int32 nl = n.body.indexOf(u'\n');
        n.title = (nl > 0 ? n.body.copy(0, nl) : n.body).trim();
        if (n.title.startsWith(u"# "_ustr))
            n.title = n.title.copy(2).trim();
    }
    return n;
}

bool LocalNotebookStore::saveNote(const NotebookNote& rNote)
{
    if (rNote.id.isEmpty())
        return false;
    osl::MutexGuard g(nbMutex());
    ensureDirSys(rootDir());
    ensureDirSys(notesDir());
    if (!writeFile(notesDir() + u"/"_ustr + rNote.id + u".md"_ustr, rNote.body))
        return false;

    auto items = parseIndex(readFile(indexPath()));
    bool found = false;
    const OUString now = nowIso();
    for (auto& e : items)
    {
        if (e.id == rNote.id)
        {
            e.title = rNote.title.isEmpty() ? u"未命名笔记"_ustr : rNote.title;
            e.updatedIso = now;
            e.pinned = rNote.pinned;
            found = true;
            break;
        }
    }
    if (!found)
    {
        NotebookIndexEntry e;
        e.id = rNote.id;
        e.title = rNote.title.isEmpty() ? u"未命名笔记"_ustr : rNote.title;
        e.updatedIso = now;
        e.pinned = rNote.pinned;
        items.insert(items.begin(), e);
    }
    return writeFile(indexPath(), serializeIndex(items));
}

bool LocalNotebookStore::removeNote(const OUString& rId)
{
    if (rId.isEmpty())
        return false;
    osl::MutexGuard g(nbMutex());
    OUString url;
    const OUString path = notesDir() + u"/"_ustr + rId + u".md"_ustr;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) == osl::FileBase::E_None)
        osl::File::remove(url);
    auto items = parseIndex(readFile(indexPath()));
    std::vector<NotebookIndexEntry> next;
    for (const auto& e : items)
        if (e.id != rId)
            next.push_back(e);
    return writeFile(indexPath(), serializeIndex(next));
}

NotebookNote LocalNotebookStore::createNote(const OUString& rTitle)
{
    NotebookNote n;
    n.id = newId();
    n.title = rTitle.isEmpty() ? u"未命名笔记"_ustr : rTitle;
    n.createdIso = nowIso();
    n.updatedIso = n.createdIso;
    n.body = u"# "_ustr + n.title + u"\n\n"_ustr;
    saveNote(n);
    return n;
}

OUString LocalNotebookStore::formatAiContextBrief(sal_Int32 nRecentNotes)
{
    if (nRecentNotes < 1)
        nRecentNotes = 1;
    if (nRecentNotes > 20)
        nRecentNotes = 20;
    OUStringBuffer b;
    b.append(u"【可圈本地记事本 · 上下文】\n"_ustr);
    b.append(NotebookMaterialStore::formatLibraryStats());
    b.append(u"\n"_ustr);
    const auto tags = NotebookMaterialStore::formatTagCloudText(8);
    if (!tags.isEmpty())
    {
        b.append(u"标签云："_ustr);
        b.append(tags);
        b.append(u"\n"_ustr);
    }
    b.append(u"近期笔记：\n"_ustr);
    const auto notes = listNotes();
    sal_Int32 n = 0;
    for (const auto& e : notes)
    {
        if (n++ >= nRecentNotes)
            break;
        b.append(u"- "_ustr);
        if (e.pinned)
            b.append(u"📌 "_ustr);
        b.append(e.title.isEmpty() ? u"（无标题）"_ustr : e.title);
        if (!e.updatedIso.isEmpty())
        {
            b.append(u" · "_ustr);
            b.append(e.updatedIso.getLength() >= 10 ? e.updatedIso.copy(0, 10) : e.updatedIso);
        }
        b.append(u"\n"_ustr);
    }
    if (n == 0)
        b.append(u"- （尚无笔记）\n"_ustr);
    b.append(u"\n请结合以上本地笔记/材料库信息，用中文简洁回答用户问题。\n"_ustr);
    return b.makeStringAndClear();
}

} // namespace kqoffice::ai::notebook

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
