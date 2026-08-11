/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "NotebookProjectStore.hxx"

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
osl::Mutex& projMutex()
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
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
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
    if (sz == 0 || sz > 8 * 1024 * 1024)
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
        const sal_Unicode c = frag[i];
        if (c == u'\\')
        {
            if (i + 1 < frag.getLength())
            {
                ++i;
                if (frag[i] == u'n')
                    out.append(u'\n');
                else
                    out.append(frag[i]);
            }
            continue;
        }
        if (c == u'"')
            break;
        out.append(c);
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

OUString extractArrayBody(const OUString& json, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\":["_ustr;
    sal_Int32 p = json.indexOf(needle);
    if (p < 0)
        return OUString();
    p += needle.getLength();
    int depth = 1;
    const sal_Int32 start = p;
    for (; p < json.getLength(); ++p)
    {
        const sal_Unicode c = json[p];
        if (c == u'[')
            ++depth;
        else if (c == u']')
        {
            --depth;
            if (depth == 0)
                return json.copy(start, p - start);
        }
    }
    return OUString();
}

std::vector<OUString> parseStringArray(const OUString& body)
{
    std::vector<OUString> out;
    sal_Int32 i = 0;
    while (i < body.getLength())
    {
        while (i < body.getLength() && (body[i] == u' ' || body[i] == u',' || body[i] == u'\n'))
            ++i;
        if (i >= body.getLength() || body[i] != u'"')
            break;
        ++i;
        OUStringBuffer s;
        for (; i < body.getLength(); ++i)
        {
            if (body[i] == u'\\')
            {
                if (i + 1 < body.getLength())
                {
                    ++i;
                    s.append(body[i] == u'n' ? u'\n' : body[i]);
                }
                continue;
            }
            if (body[i] == u'"')
            {
                ++i;
                break;
            }
            s.append(body[i]);
        }
        out.push_back(s.makeStringAndClear());
    }
    return out;
}

std::vector<OUString> splitObjects(const OUString& body)
{
    std::vector<OUString> objs;
    int depth = 0;
    sal_Int32 start = -1;
    for (sal_Int32 i = 0; i < body.getLength(); ++i)
    {
        const sal_Unicode c = body[i];
        if (c == u'{')
        {
            if (depth == 0)
                start = i;
            ++depth;
        }
        else if (c == u'}')
        {
            --depth;
            if (depth == 0 && start >= 0)
            {
                objs.push_back(body.copy(start, i - start + 1));
                start = -1;
            }
        }
    }
    return objs;
}

OUString projectPath(const OUString& rId)
{
    return NotebookProjectStore::projectsDir() + u"/"_ustr + rId + u".json"_ustr;
}

OUString activePath()
{
    return NotebookProjectStore::rootDir() + u"/active-project.txt"_ustr;
}
} // namespace

OUString NotebookProjectStore::rootDir()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        home = "/tmp";
    return OUString::fromUtf8(home) + u"/.config/kqoffice/notebook"_ustr;
}

OUString NotebookProjectStore::projectsDir() { return rootDir() + u"/projects"_ustr; }

OUString NotebookProjectStore::indexPath() { return projectsDir() + u"/index.json"_ustr; }

OUString NotebookProjectStore::newId()
{
    static sal_Int32 n = 0;
    return u"nb"_ustr + nowIso().replaceAll(u":"_ustr, u""_ustr).replaceAll(u"-"_ustr, u""_ustr)
           + u"_"_ustr + OUString::number(++n);
}

std::vector<NotebookProjectIndexEntry> NotebookProjectStore::listProjects()
{
    osl::MutexGuard g(projMutex());
    ensureDirSys(projectsDir());
    const OUString raw = readFile(indexPath());
    std::vector<NotebookProjectIndexEntry> out;
    if (raw.isEmpty())
        return out;
    const OUString body = extractArrayBody(raw, u"projects"_ustr);
    for (const auto& o : splitObjects(body))
    {
        NotebookProjectIndexEntry e;
        e.id = field(o, u"id"_ustr);
        e.title = field(o, u"title"_ustr);
        e.updatedIso = field(o, u"updated"_ustr);
        e.sourceCount = fieldInt(o, u"sources"_ustr);
        if (!e.id.isEmpty())
            out.push_back(e);
    }
    return out;
}

NotebookProject NotebookProjectStore::loadProject(const OUString& rId)
{
    osl::MutexGuard g(projMutex());
    NotebookProject p;
    if (rId.isEmpty())
        return p;
    const OUString raw = readFile(projectPath(rId));
    if (raw.isEmpty())
        return p;
    p.id = field(raw, u"id"_ustr);
    p.title = field(raw, u"title"_ustr);
    p.createdIso = field(raw, u"created"_ustr);
    p.updatedIso = field(raw, u"updated"_ustr);
    p.materialIds = parseStringArray(extractArrayBody(raw, u"materials"_ustr));
    for (const auto& o : splitObjects(extractArrayBody(raw, u"chat"_ustr)))
    {
        ChatTurn t;
        t.role = field(o, u"role"_ustr);
        t.text = field(o, u"text"_ustr);
        if (!t.text.isEmpty())
            p.chat.push_back(t);
    }
    for (const auto& o : splitObjects(extractArrayBody(raw, u"artifacts"_ustr)))
    {
        StudioArtifact a;
        a.id = field(o, u"id"_ustr);
        a.kind = field(o, u"kind"_ustr);
        a.title = field(o, u"title"_ustr);
        a.body = field(o, u"body"_ustr);
        a.createdIso = field(o, u"created"_ustr);
        if (!a.id.isEmpty())
            p.artifacts.push_back(a);
    }
    return p;
}

bool NotebookProjectStore::saveProject(const NotebookProject& rProject)
{
    osl::MutexGuard g(projMutex());
    if (rProject.id.isEmpty())
        return false;
    ensureDirSys(projectsDir());
    NotebookProject p = rProject;
    p.updatedIso = nowIso();
    if (p.createdIso.isEmpty())
        p.createdIso = p.updatedIso;

    OUStringBuffer b;
    b.append(u"{\n  \"id\":\""_ustr);
    appendEsc(b, p.id);
    b.append(u"\",\n  \"title\":\""_ustr);
    appendEsc(b, p.title);
    b.append(u"\",\n  \"created\":\""_ustr);
    appendEsc(b, p.createdIso);
    b.append(u"\",\n  \"updated\":\""_ustr);
    appendEsc(b, p.updatedIso);
    b.append(u"\",\n  \"materials\":["_ustr);
    for (size_t i = 0; i < p.materialIds.size(); ++i)
    {
        if (i)
            b.append(u',');
        b.append(u'"');
        appendEsc(b, p.materialIds[i]);
        b.append(u'"');
    }
    b.append(u"],\n  \"chat\":["_ustr);
    for (size_t i = 0; i < p.chat.size(); ++i)
    {
        if (i)
            b.append(u',');
        b.append(u"{\"role\":\""_ustr);
        appendEsc(b, p.chat[i].role);
        b.append(u"\",\"text\":\""_ustr);
        appendEsc(b, p.chat[i].text);
        b.append(u"\"}"_ustr);
    }
    b.append(u"],\n  \"artifacts\":["_ustr);
    for (size_t i = 0; i < p.artifacts.size(); ++i)
    {
        if (i)
            b.append(u',');
        b.append(u"{\"id\":\""_ustr);
        appendEsc(b, p.artifacts[i].id);
        b.append(u"\",\"kind\":\""_ustr);
        appendEsc(b, p.artifacts[i].kind);
        b.append(u"\",\"title\":\""_ustr);
        appendEsc(b, p.artifacts[i].title);
        b.append(u"\",\"body\":\""_ustr);
        appendEsc(b, p.artifacts[i].body);
        b.append(u"\",\"created\":\""_ustr);
        appendEsc(b, p.artifacts[i].createdIso);
        b.append(u"\"}"_ustr);
    }
    b.append(u"]\n}\n"_ustr);
    if (!writeFile(projectPath(p.id), b.makeStringAndClear()))
        return false;

    // rewrite index (do not call listProjects — already hold mutex)
    const OUString idxRaw = readFile(indexPath());
    std::vector<NotebookProjectIndexEntry> idx;
    if (!idxRaw.isEmpty())
    {
        for (const auto& o : splitObjects(extractArrayBody(idxRaw, u"projects"_ustr)))
        {
            NotebookProjectIndexEntry e;
            e.id = field(o, u"id"_ustr);
            e.title = field(o, u"title"_ustr);
            e.updatedIso = field(o, u"updated"_ustr);
            e.sourceCount = fieldInt(o, u"sources"_ustr);
            if (!e.id.isEmpty() && e.id != p.id)
                idx.push_back(e);
        }
    }
    NotebookProjectIndexEntry self;
    self.id = p.id;
    self.title = p.title;
    self.updatedIso = p.updatedIso;
    self.sourceCount = static_cast<sal_Int32>(p.materialIds.size());
    idx.insert(idx.begin(), self);

    OUStringBuffer ib;
    ib.append(u"{\"projects\":["_ustr);
    for (size_t i = 0; i < idx.size(); ++i)
    {
        if (i)
            ib.append(u',');
        ib.append(u"{\"id\":\""_ustr);
        appendEsc(ib, idx[i].id);
        ib.append(u"\",\"title\":\""_ustr);
        appendEsc(ib, idx[i].title);
        ib.append(u"\",\"updated\":\""_ustr);
        appendEsc(ib, idx[i].updatedIso);
        ib.append(u"\",\"sources\":"_ustr);
        ib.append(OUString::number(idx[i].sourceCount));
        ib.append(u"}"_ustr);
    }
    ib.append(u"]}\n"_ustr);
    return writeFile(indexPath(), ib.makeStringAndClear());
}

bool NotebookProjectStore::removeProject(const OUString& rId)
{
    osl::MutexGuard g(projMutex());
    if (rId.isEmpty())
        return false;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(projectPath(rId), url) == osl::FileBase::E_None)
        osl::File::remove(url);
    // rewrite index without rId
    const OUString idxRaw = readFile(indexPath());
    std::vector<NotebookProjectIndexEntry> idx;
    for (const auto& o : splitObjects(extractArrayBody(idxRaw, u"projects"_ustr)))
    {
        NotebookProjectIndexEntry e;
        e.id = field(o, u"id"_ustr);
        if (e.id.isEmpty() || e.id == rId)
            continue;
        e.title = field(o, u"title"_ustr);
        e.updatedIso = field(o, u"updated"_ustr);
        e.sourceCount = fieldInt(o, u"sources"_ustr);
        idx.push_back(e);
    }
    OUStringBuffer ib;
    ib.append(u"{\"projects\":["_ustr);
    for (size_t i = 0; i < idx.size(); ++i)
    {
        if (i)
            ib.append(u',');
        ib.append(u"{\"id\":\""_ustr);
        appendEsc(ib, idx[i].id);
        ib.append(u"\",\"title\":\""_ustr);
        appendEsc(ib, idx[i].title);
        ib.append(u"\",\"updated\":\""_ustr);
        appendEsc(ib, idx[i].updatedIso);
        ib.append(u"\",\"sources\":"_ustr);
        ib.append(OUString::number(idx[i].sourceCount));
        ib.append(u"}"_ustr);
    }
    ib.append(u"]}\n"_ustr);
    writeFile(indexPath(), ib.makeStringAndClear());
    // activeProjectId/setActiveProjectId also take mutex — inline under same lock
    const OUString active = readFile(activePath()).trim();
    if (active == rId)
    {
        ensureDirSys(rootDir());
        writeFile(activePath(), u"\n"_ustr);
    }
    return true;
}

NotebookProject NotebookProjectStore::createProject(const OUString& rTitle)
{
    NotebookProject p;
    p.id = newId();
    p.title = rTitle.isEmpty() ? u"未命名笔记本"_ustr : rTitle;
    p.createdIso = nowIso();
    p.updatedIso = p.createdIso;
    saveProject(p);
    setActiveProjectId(p.id);
    return p;
}

OUString NotebookProjectStore::activeProjectId()
{
    osl::MutexGuard g(projMutex());
    return readFile(activePath()).trim();
}

void NotebookProjectStore::setActiveProjectId(const OUString& rId)
{
    osl::MutexGuard g(projMutex());
    ensureDirSys(rootDir());
    writeFile(activePath(), rId + u"\n"_ustr);
}

} // namespace kqoffice::ai::notebook

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
