/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "WorkTelemetryStore.hxx"

#include <comphelper/hash.hxx>
#include <osl/file.hxx>
#include <osl/mutex.hxx>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
// std::system used by createDesktopShortcuts chmod helper
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace kqoffice::ai::workbench
{
namespace
{
osl::Mutex& telemetryMutex()
{
    static osl::Mutex a;
    return a;
}

OUString envOrEmpty(const char* n)
{
    const char* v = std::getenv(n);
    return (v && *v) ? OUString::fromUtf8(v) : OUString();
}

bool ensureDirSys(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

OUString nowIsoLocal()
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

OUString dayKeyFromNow()
{
    const OUString iso = nowIsoLocal();
    return iso.getLength() >= 10 ? iso.copy(0, 10) : iso;
}

void appendJsonEsc(OUStringBuffer& b, const OUString& s)
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

OUString eventToJsonLine(const WorkTelemetryEvent& e)
{
    OUStringBuffer b;
    b.append(u"{"_ustr);
    b.append(u"\"kind\":\""_ustr);
    appendJsonEsc(b, e.kind);
    b.append(u"\",\"ts\":\""_ustr);
    appendJsonEsc(b, e.tsIso);
    b.append(u"\""_ustr);
    if (!e.model.isEmpty())
    {
        b.append(u",\"model\":\""_ustr);
        appendJsonEsc(b, e.model);
        b.append(u"\""_ustr);
    }
    if (!e.capability.isEmpty())
    {
        b.append(u",\"capability\":\""_ustr);
        appendJsonEsc(b, e.capability);
        b.append(u"\""_ustr);
    }
    if (!e.docHash.isEmpty())
    {
        b.append(u",\"doc_hash\":\""_ustr);
        appendJsonEsc(b, e.docHash);
        b.append(u"\""_ustr);
    }
    if (!e.docTitle.isEmpty())
    {
        b.append(u",\"doc_title\":\""_ustr);
        appendJsonEsc(b, e.docTitle);
        b.append(u"\""_ustr);
    }
    if (!e.status.isEmpty())
    {
        b.append(u",\"status\":\""_ustr);
        appendJsonEsc(b, e.status);
        b.append(u"\""_ustr);
    }
    b.append(u",\"tokens_est\":"_ustr);
    b.append(e.tokensEst);
    b.append(u",\"duration_ms\":"_ustr);
    b.append(e.durationMs);
    b.append(u",\"count\":"_ustr);
    b.append(e.count);
    b.append(u"}\n"_ustr);
    return b.makeStringAndClear();
}

OUString jsonField(const OUString& line, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\":\""_ustr;
    sal_Int32 p = line.indexOf(needle);
    if (p < 0)
        return OUString();
    p += needle.getLength();
    OUStringBuffer out;
    for (sal_Int32 i = p; i < line.getLength(); ++i)
    {
        if (line[i] == u'\\' && i + 1 < line.getLength())
        {
            ++i;
            if (line[i] == u'n')
                out.append(u'\n');
            else
                out.append(line[i]);
            continue;
        }
        if (line[i] == u'"')
            break;
        out.append(line[i]);
    }
    return out.makeStringAndClear();
}

sal_Int32 jsonIntField(const OUString& line, const OUString& key, sal_Int32 def = 0)
{
    const OUString needle = u"\""_ustr + key + u"\":"_ustr;
    sal_Int32 p = line.indexOf(needle);
    if (p < 0)
        return def;
    p += needle.getLength();
    while (p < line.getLength() && line[p] == u' ')
        ++p;
    sal_Int32 j = p;
    if (j < line.getLength() && line[j] == u'-')
        ++j;
    while (j < line.getLength() && line[j] >= u'0' && line[j] <= u'9')
        ++j;
    if (j == p)
        return def;
    return line.copy(p, j - p).toInt32();
}

bool appendFile(const OUString& rSysPath, const OString& rUtf8)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    osl::FileBase::RC e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    f.setPos(osl_Pos_Absolut, sal_uInt64(sz));
    sal_uInt64 n = 0;
    const bool ok = f.write(rUtf8.getStr(), rUtf8.getLength(), n) == osl::FileBase::E_None
                    && n == static_cast<sal_uInt64>(rUtf8.getLength());
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
    if (n == 0)
        return OUString();
    return OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
}

void accumulateLine(WorkPeriodSummary& s, const OUString& line,
                    std::map<OUString, ModelUsageRow>& models,
                    std::map<OUString, DocUsageRow>& docs)
{
    const OUString kind = jsonField(line, u"kind"_ustr);
    if (kind.isEmpty())
        return;
    const sal_Int32 cnt = std::max<sal_Int32>(1, jsonIntField(line, u"count"_ustr, 1));
    const sal_Int32 tokens = jsonIntField(line, u"tokens_est"_ustr, 0);
    if (kind == u"session_tick"_ustr)
        s.activeMinutes += cnt;
    else if (kind == u"doc_open"_ustr)
        s.docOpens += cnt;
    else if (kind == u"doc_save"_ustr)
        s.docSaves += cnt;
    else if (kind == u"doc_new"_ustr)
        s.docNews += cnt;
    else if (kind == u"ai_call"_ustr)
    {
        s.aiCalls += cnt;
        s.tokensEst += tokens;
        const OUString model = jsonField(line, u"model"_ustr);
        const OUString mkey = model.isEmpty() ? u"(unknown)"_ustr : model;
        auto& row = models[mkey];
        row.model = mkey;
        row.calls += cnt;
        row.tokensEst += tokens;
    }
    else if (kind == u"ai_apply"_ustr)
        s.applies += cnt;
    else if (kind == u"screenshot"_ustr)
        s.screenshots += cnt;
    else if (kind == u"voice"_ustr)
        s.voiceEvents += cnt;
    else if (kind == u"test_pass"_ustr)
        s.testsPassed += cnt;
    else if (kind == u"notebook_note"_ustr)
        s.notebookNotes += cnt;

    const OUString dh = jsonField(line, u"doc_hash"_ustr);
    if (!dh.isEmpty())
    {
        auto& d = docs[dh];
        d.docHash = dh;
        const OUString title = jsonField(line, u"doc_title"_ustr);
        if (!title.isEmpty())
            d.docTitle = title;
        if (kind == u"doc_open"_ustr)
            d.opens += cnt;
        if (kind == u"doc_save"_ustr)
            d.saves += cnt;
        if (kind == u"ai_call"_ustr || kind == u"ai_apply"_ustr)
            d.aiTouches += cnt;
    }
}

WorkPeriodSummary summarizeFiles(const std::vector<OUString>& dayFiles, const OUString& label)
{
    WorkPeriodSummary s;
    s.periodLabel = label;
    std::map<OUString, ModelUsageRow> models;
    std::map<OUString, DocUsageRow> docs;
    for (const auto& dayPath : dayFiles)
    {
        const OUString body = readFile(dayPath);
        if (body.isEmpty())
            continue;
        sal_Int32 pos = 0;
        while (pos >= 0 && pos < body.getLength())
        {
            sal_Int32 nl = body.indexOf(u'\n', pos);
            if (nl < 0)
                nl = body.getLength();
            const OUString line = body.copy(pos, nl - pos).trim();
            if (!line.isEmpty())
                accumulateLine(s, line, models, docs);
            if (nl >= body.getLength())
                break;
            pos = nl + 1;
        }
    }
    s.uniqueDocs = static_cast<sal_Int32>(docs.size());
    for (auto& kv : models)
        s.models.push_back(kv.second);
    std::sort(s.models.begin(), s.models.end(),
              [](const ModelUsageRow& a, const ModelUsageRow& b) { return a.calls > b.calls; });
    for (auto& kv : docs)
        s.docs.push_back(kv.second);
    std::sort(s.docs.begin(), s.docs.end(), [](const DocUsageRow& a, const DocUsageRow& b) {
        return (a.opens + a.saves + a.aiTouches) > (b.opens + b.saves + b.aiTouches);
    });
    return s;
}

std::vector<OUString> listDayFilesWithPrefix(const OUString& prefix /* YYYY or YYYY-MM or YYYY-MM-DD */)
{
    std::vector<OUString> out;
    const OUString dir = WorkTelemetryStore::telemetryDir();
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) != osl::FileBase::E_None)
        return out;
    osl::Directory d(url);
    if (d.open() != osl::FileBase::E_None)
        return out;
    osl::DirectoryItem item;
    while (d.getNextItem(item) == osl::FileBase::E_None)
    {
        osl::FileStatus st(osl_FileStatus_Mask_FileName | osl_FileStatus_Mask_Type);
        if (item.getFileStatus(st) != osl::FileBase::E_None)
            continue;
        if (st.getFileType() == osl::FileStatus::Directory)
            continue;
        const OUString name = st.getFileName();
        if (!name.endsWith(u".jsonl"_ustr))
            continue;
        const OUString stem = name.copy(0, name.getLength() - 6);
        if (stem.startsWith(prefix))
            out.push_back(dir + u"/"_ustr + name);
    }
    d.close();
    return out;
}
} // namespace

OUString WorkTelemetryStore::rootDir()
{
    OUString o = envOrEmpty("KQOFFICE_WORKBENCH_DIR");
    if (!o.isEmpty())
        return o;
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return u"/tmp/kqoffice-workbench"_ustr;
    return OUString::fromUtf8(home) + u"/.config/kqoffice/workbench"_ustr;
}

OUString WorkTelemetryStore::telemetryDir() { return rootDir() + u"/telemetry"_ustr; }

OUString WorkTelemetryStore::todayKey() { return dayKeyFromNow(); }

OUString WorkTelemetryStore::dayKeyOffset(sal_Int32 nDaysAgo)
{
    if (nDaysAgo < 0)
        nDaysAgo = 0;
    if (nDaysAgo > 3700)
        nDaysAgo = 3700;
    TimeValue tv{};
    osl_getSystemTime(&tv);
    const std::time_t t
        = static_cast<std::time_t>(tv.Seconds) - static_cast<std::time_t>(nDaysAgo) * 86400;
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return OUString::fromUtf8(buf);
}

bool WorkTelemetryStore::isDayWithinLastN(const OUString& rDayKey, sal_Int32 nDays)
{
    if (rDayKey.getLength() < 10 || nDays < 1)
        return false;
    const OUString day = rDayKey.copy(0, 10);
    const OUString oldest = dayKeyOffset(nDays - 1);
    const OUString today = todayKey();
    return day >= oldest && day <= today;
}

OUString WorkTelemetryStore::formatTodayGlance()
{
    const WorkPeriodSummary t = summarizeToday();
    const WorkPeriodSummary w = summarizeLastDays(7);
    const sal_Int32 goal = getWeekGoalMinutes();
    OUStringBuffer b;
    b.append(u"【今日速览】 "_ustr);
    b.append(t.periodLabel);
    b.append(u"\n"_ustr);
    b.append(u"┌ 活跃 "_ustr);
    b.append(t.activeMinutes);
    b.append(u" 分 ┐  ┌ 文稿 "_ustr);
    b.append(t.uniqueDocs);
    b.append(u" ┐  ┌ AI "_ustr);
    b.append(t.aiCalls);
    b.append(u" 次 ┐  ┌ tok≈"_ustr);
    b.append(t.tokensEst);
    b.append(u" ┐\n"_ustr);
    b.append(u"近7日 "_ustr);
    b.append(w.activeMinutes);
    b.append(u" 分 · AI "_ustr);
    b.append(w.aiCalls);
    b.append(u" · 文稿 "_ustr);
    b.append(w.uniqueDocs);
    if (goal > 0)
    {
        b.append(u" · 目标 "_ustr);
        b.append(w.activeMinutes);
        b.append(u"/");
        b.append(goal);
        b.append(u" 分"_ustr);
        if (w.activeMinutes >= goal)
            b.append(u" ✓"_ustr);
    }
    if (!isWeekArchived())
        b.append(u" · ⚠ 本周未归档"_ustr);
    else
        b.append(u" · ✓ 已归档"_ustr);
    b.append(u"\n"_ustr);
    return b.makeStringAndClear();
}

OUString WorkTelemetryStore::formatAiContextBrief()
{
    OUStringBuffer b;
    b.append(u"【可圈工作中台 · 本地上下文】\n"_ustr);
    b.append(formatTodayGlance());
    b.append(u"\n"_ustr);
    b.append(formatWeekGoalProgress());
    b.append(u"\n"_ustr);
    b.append(formatWeekOverWeekCompare());
    b.append(u"\n请基于以上本地工作数据，用中文给出简洁建议（今日优先事项 / 是否该归档周报）。\n"_ustr);
    return b.makeStringAndClear();
}

sal_Int32 WorkTelemetryStore::estimateTokens(sal_Int32 nChars)
{
    if (nChars <= 0)
        return 0;
    return (nChars + 3) / 4;
}

OUString WorkTelemetryStore::makeDocHash(const OUString& rIdentity)
{
    if (rIdentity.isEmpty())
        return OUString();
    const OString id = OUStringToOString(rIdentity, RTL_TEXTENCODING_UTF8);
    const auto hash = comphelper::Hash::calculateHash(id.getStr(), id.getLength(),
                                                      comphelper::HashType::SHA256);
    const OUString full = OUString::createFromAscii(comphelper::hashToString(hash));
    return full.getLength() > 16 ? full.copy(0, 16) : full;
}

bool WorkTelemetryStore::record(const WorkTelemetryEvent& rEvent)
{
    osl::MutexGuard g(telemetryMutex());
    WorkTelemetryEvent e = rEvent;
    if (e.kind.isEmpty())
        return false;
    if (e.tsIso.isEmpty())
        e.tsIso = nowIsoLocal();
    if (e.count <= 0)
        e.count = 1;
    const OUString day = e.tsIso.getLength() >= 10 ? e.tsIso.copy(0, 10) : todayKey();
    const OUString dir = telemetryDir();
    if (!ensureDirSys(dir))
        return false;
    const OUString path = dir + u"/"_ustr + day + u".jsonl"_ustr;
    const OUString line = eventToJsonLine(e);
    const OString utf8 = OUStringToOString(line, RTL_TEXTENCODING_UTF8);
    return appendFile(path, utf8);
}

bool WorkTelemetryStore::recordAiCall(const OUString& rModel, const OUString& rCapability,
                                      sal_Int32 nRequestChars, sal_Int32 nResponseChars,
                                      sal_Int32 nDurationMs, const OUString& rStatus,
                                      const OUString& rDocTitle)
{
    WorkTelemetryEvent e;
    e.kind = u"ai_call"_ustr;
    e.model = rModel;
    e.capability = rCapability;
    e.tokensEst = estimateTokens(nRequestChars) + estimateTokens(nResponseChars);
    e.durationMs = nDurationMs;
    e.status = rStatus;
    e.docTitle = rDocTitle;
    if (!rDocTitle.isEmpty())
        e.docHash = makeDocHash(rDocTitle);
    return record(e);
}

bool WorkTelemetryStore::recordSessionTick(sal_Int32 nMinutes)
{
    WorkTelemetryEvent e;
    e.kind = u"session_tick"_ustr;
    e.count = nMinutes > 0 ? nMinutes : 1;
    return record(e);
}

bool WorkTelemetryStore::recordDoc(const OUString& rKind, const OUString& rTitle,
                                   const OUString& rUrlOrId)
{
    WorkTelemetryEvent e;
    if (rKind == u"save"_ustr)
        e.kind = u"doc_save"_ustr;
    else if (rKind == u"new"_ustr)
        e.kind = u"doc_new"_ustr;
    else
        e.kind = u"doc_open"_ustr;
    e.docTitle = rTitle;
    e.docHash = makeDocHash(rUrlOrId.isEmpty() ? rTitle : rUrlOrId);
    return record(e);
}

bool WorkTelemetryStore::recordSimple(const OUString& rKind, sal_Int32 nCount)
{
    WorkTelemetryEvent e;
    e.kind = rKind;
    e.count = nCount > 0 ? nCount : 1;
    return record(e);
}

WorkPeriodSummary WorkTelemetryStore::summarizeDay(const OUString& rDayKey)
{
    const OUString path = telemetryDir() + u"/"_ustr + rDayKey + u".jsonl"_ustr;
    return summarizeFiles({ path }, rDayKey);
}

WorkPeriodSummary WorkTelemetryStore::summarizeMonth(const OUString& rMonthKey)
{
    return summarizeFiles(listDayFilesWithPrefix(rMonthKey), rMonthKey);
}

WorkPeriodSummary WorkTelemetryStore::summarizeYear(const OUString& rYearKey)
{
    return summarizeFiles(listDayFilesWithPrefix(rYearKey), rYearKey);
}

WorkPeriodSummary WorkTelemetryStore::summarizeToday() { return summarizeDay(todayKey()); }

WorkPeriodSummary WorkTelemetryStore::summarizeLastDays(sal_Int32 nDays)
{
    if (nDays < 1)
        nDays = 1;
    if (nDays > 366)
        nDays = 366;

    // Build day keys going backwards from today using time_t
    TimeValue tv{};
    osl_getSystemTime(&tv);
    std::time_t sec = static_cast<std::time_t>(tv.Seconds);
    std::vector<OUString> files;
    files.reserve(static_cast<size_t>(nDays));
    OUStringBuffer label;
    label.append(u"last-"_ustr);
    label.append(nDays);
    label.append(u"d"_ustr);

    for (sal_Int32 i = 0; i < nDays; ++i)
    {
        const std::time_t t = sec - static_cast<std::time_t>(i) * 86400;
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                      tm.tm_mday);
        const OUString day = OUString::fromUtf8(buf);
        files.push_back(telemetryDir() + u"/"_ustr + day + u".jsonl"_ustr);
    }
    return summarizeFiles(files, label.makeStringAndClear());
}

WorkPeriodSummary WorkTelemetryStore::summarizeDaysOffset(sal_Int32 nStartOffsetDays, sal_Int32 nDays)
{
    if (nStartOffsetDays < 0)
        nStartOffsetDays = 0;
    if (nStartOffsetDays > 730)
        nStartOffsetDays = 730;
    if (nDays < 1)
        nDays = 1;
    if (nDays > 366)
        nDays = 366;

    TimeValue tv{};
    osl_getSystemTime(&tv);
    std::time_t sec = static_cast<std::time_t>(tv.Seconds);
    std::vector<OUString> files;
    files.reserve(static_cast<size_t>(nDays));
    OUStringBuffer label;
    label.append(u"offset-"_ustr);
    label.append(nStartOffsetDays);
    label.append(u"+"_ustr);
    label.append(nDays);
    label.append(u"d"_ustr);

    for (sal_Int32 i = 0; i < nDays; ++i)
    {
        const std::time_t t
            = sec - static_cast<std::time_t>(nStartOffsetDays + i) * 86400;
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                      tm.tm_mday);
        const OUString day = OUString::fromUtf8(buf);
        files.push_back(telemetryDir() + u"/"_ustr + day + u".jsonl"_ustr);
    }
    return summarizeFiles(files, label.makeStringAndClear());
}

namespace
{
OUString deltaLabel(sal_Int32 cur, sal_Int32 prev)
{
    if (prev <= 0)
    {
        if (cur <= 0)
            return u"持平"_ustr;
        return u"新"_ustr;
    }
    const sal_Int32 d = cur - prev;
    const sal_Int32 pct = (d * 100) / prev;
    OUStringBuffer b;
    if (d > 0)
        b.append(u'+');
    b.append(pct);
    b.append(u'%');
    return b.makeStringAndClear();
}
} // namespace

OUString WorkTelemetryStore::formatWeekOverWeekCompare()
{
    const WorkPeriodSummary cur = summarizeDaysOffset(0, 7);
    const WorkPeriodSummary prev = summarizeDaysOffset(7, 7);
    OUStringBuffer b;
    b.append(u"—— 本周 vs 上周（滚动 7 日）——\n"_ustr);
    b.append(u"活跃 "_ustr);
    b.append(cur.activeMinutes);
    b.append(u" 分 ("_ustr);
    b.append(deltaLabel(cur.activeMinutes, prev.activeMinutes));
    b.append(u") · 上周 "_ustr);
    b.append(prev.activeMinutes);
    b.append(u" 分\n"_ustr);
    b.append(u"AI "_ustr);
    b.append(cur.aiCalls);
    b.append(u" 次 ("_ustr);
    b.append(deltaLabel(cur.aiCalls, prev.aiCalls));
    b.append(u") · 上周 "_ustr);
    b.append(prev.aiCalls);
    b.append(u" 次\n"_ustr);
    b.append(u"token≈"_ustr);
    b.append(cur.tokensEst);
    b.append(u" ("_ustr);
    b.append(deltaLabel(cur.tokensEst, prev.tokensEst));
    b.append(u") · 上周 ≈"_ustr);
    b.append(prev.tokensEst);
    b.append(u"\n"_ustr);
    b.append(u"文稿 "_ustr);
    b.append(cur.uniqueDocs);
    b.append(u" ("_ustr);
    b.append(deltaLabel(cur.uniqueDocs, prev.uniqueDocs));
    b.append(u") · 上周 "_ustr);
    b.append(prev.uniqueDocs);
    b.append(u"\n"_ustr);
    return b.makeStringAndClear();
}

namespace
{
OUString miniBar(sal_Int32 value, sal_Int32 maxVal, sal_Int32 width)
{
    if (width < 1)
        width = 1;
    if (maxVal < 1)
        maxVal = 1;
    sal_Int32 filled = (value <= 0) ? 0 : std::max<sal_Int32>(1, (value * width) / maxVal);
    if (filled > width)
        filled = width;
    OUStringBuffer b;
    for (sal_Int32 i = 0; i < filled; ++i)
        b.append(u'█');
    for (sal_Int32 i = filled; i < width; ++i)
        b.append(u'░');
    return b.makeStringAndClear();
}
} // namespace

OUString WorkTelemetryStore::formatWeekOverWeekSparkline()
{
    const WorkPeriodSummary cur = summarizeDaysOffset(0, 7);
    const WorkPeriodSummary prev = summarizeDaysOffset(7, 7);
    const sal_Int32 maxMin = std::max(std::max(cur.activeMinutes, prev.activeMinutes), sal_Int32(1));
    const sal_Int32 maxAi = std::max(std::max(cur.aiCalls, prev.aiCalls), sal_Int32(1));
    OUStringBuffer b;
    b.append(u"⏱ "_ustr);
    b.append(miniBar(cur.activeMinutes, maxMin, 8));
    b.append(u" "_ustr);
    b.append(cur.activeMinutes);
    b.append(u" vs "_ustr);
    b.append(miniBar(prev.activeMinutes, maxMin, 8));
    b.append(u" "_ustr);
    b.append(prev.activeMinutes);
    b.append(u" · AI "_ustr);
    b.append(miniBar(cur.aiCalls, maxAi, 6));
    b.append(u" "_ustr);
    b.append(cur.aiCalls);
    b.append(u" vs "_ustr);
    b.append(miniBar(prev.aiCalls, maxAi, 6));
    b.append(u" "_ustr);
    b.append(prev.aiCalls);
    return b.makeStringAndClear();
}

OUString WorkTelemetryStore::formatWeekOverWeekSvg()
{
    const WorkPeriodSummary cur = summarizeDaysOffset(0, 7);
    const WorkPeriodSummary prev = summarizeDaysOffset(7, 7);
    const sal_Int32 width = 480;
    const sal_Int32 height = 220;
    const sal_Int32 padL = 72;
    const sal_Int32 padT = 40;
    const sal_Int32 barMax = width - padL - 84;

    OUStringBuffer b;
    b.append(u"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"_ustr);
    b.append(u"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""_ustr);
    b.append(width);
    b.append(u"\" height=\""_ustr);
    b.append(height);
    b.append(u"\" viewBox=\"0 0 "_ustr);
    b.append(width);
    b.append(u" "_ustr);
    b.append(height);
    b.append(u"\">\n"_ustr);
    b.append(u"  <rect width=\"100%\" height=\"100%\" fill=\"#f7f8fa\"/>\n"_ustr);
    b.append(u"  <text x=\"16\" y=\"24\" font-family=\"system-ui,sans-serif\" font-size=\"14\" "
             u"font-weight=\"600\" fill=\"#1a1a1a\">本周 vs 上周</text>\n"_ustr);
    b.append(u"  <text x=\"130\" y=\"24\" font-family=\"system-ui,sans-serif\" font-size=\"10\" "
             u"fill=\"#3b6fd9\">本周</text>\n"_ustr);
    b.append(u"  <text x=\"170\" y=\"24\" font-family=\"system-ui,sans-serif\" font-size=\"10\" "
             u"fill=\"#94a3b8\">上周</text>\n"_ustr);

    auto esc = [](const OUString& s) {
        OUStringBuffer o;
        for (sal_Int32 i = 0; i < s.getLength(); ++i)
        {
            const sal_Unicode c = s[i];
            if (c == u'&')
                o.append(u"&amp;"_ustr);
            else if (c == u'<')
                o.append(u"&lt;"_ustr);
            else
                o.append(c);
        }
        return o.makeStringAndClear();
    };

    const OUString labels[]
        = { u"活跃分"_ustr, u"AI"_ustr, u"token"_ustr, u"文稿"_ustr };
    const sal_Int32 curs[]
        = { cur.activeMinutes, cur.aiCalls, cur.tokensEst, cur.uniqueDocs };
    const sal_Int32 prevs[]
        = { prev.activeMinutes, prev.aiCalls, prev.tokensEst, prev.uniqueDocs };
    for (sal_Int32 i = 0; i < 4; ++i)
    {
        const sal_Int32 y = padT + i * 42;
        const sal_Int32 scale = std::max(std::max(curs[i], prevs[i]), sal_Int32(1));
        const sal_Int32 bwC
            = curs[i] <= 0 ? 0 : std::max<sal_Int32>(2, (curs[i] * barMax) / scale);
        const sal_Int32 bwP
            = prevs[i] <= 0 ? 0 : std::max<sal_Int32>(2, (prevs[i] * barMax) / scale);
        b.append(u"  <text x=\"12\" y=\""_ustr);
        b.append(y + 14);
        b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"11\" fill=\"#333\">"_ustr);
        b.append(esc(labels[i]));
        b.append(u"</text>\n"_ustr);
        b.append(u"  <rect x=\""_ustr);
        b.append(padL);
        b.append(u"\" y=\""_ustr);
        b.append(y);
        b.append(u"\" width=\""_ustr);
        b.append(bwC);
        b.append(u"\" height=\"12\" rx=\"2\" fill=\"#3b6fd9\" opacity=\"0.9\"/>\n"_ustr);
        b.append(u"  <rect x=\""_ustr);
        b.append(padL);
        b.append(u"\" y=\""_ustr);
        b.append(y + 16);
        b.append(u"\" width=\""_ustr);
        b.append(bwP);
        b.append(u"\" height=\"10\" rx=\"2\" fill=\"#94a3b8\" opacity=\"0.85\"/>\n"_ustr);
        b.append(u"  <text x=\""_ustr);
        b.append(padL + 8 + std::max(bwC, bwP));
        b.append(u"\" y=\""_ustr);
        b.append(y + 14);
        b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"10\" fill=\"#555\">"_ustr);
        b.append(curs[i]);
        b.append(u" / "_ustr);
        b.append(prevs[i]);
        b.append(u"</text>\n"_ustr);
    }
    b.append(u"  <text x=\"12\" y=\""_ustr);
    b.append(height - 10);
    b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"9\" fill=\"#999\">可圈 · "
             u"local-first · 环比迷你图</text>\n"_ustr);
    b.append(u"</svg>\n"_ustr);
    return b.makeStringAndClear();
}

OUString WorkTelemetryStore::exportWeekOverWeekSvg()
{
    ensureDirSys(rootDir());
    const OUString dir = rootDir() + u"/reports"_ustr;
    ensureDirSys(dir);
    const OUString path = dir + u"/wow-"_ustr + todayKey() + u".svg"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return OUString();
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return OUString();
    f.setSize(0);
    const OString utf8
        = OUStringToOString(formatWeekOverWeekSvg(), RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
    return path;
}

OUString WorkTelemetryStore::formatDaySparkline(sal_Int32 nDays)
{
    if (nDays < 1)
        nDays = 1;
    if (nDays > 31)
        nDays = 31;

    TimeValue tv{};
    osl_getSystemTime(&tv);
    std::time_t sec = static_cast<std::time_t>(tv.Seconds);

    std::vector<WorkPeriodSummary> days;
    days.reserve(static_cast<size_t>(nDays));
    sal_Int32 maxMin = 1;
    for (sal_Int32 i = nDays - 1; i >= 0; --i)
    {
        const std::time_t t = sec - static_cast<std::time_t>(i) * 86400;
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                      tm.tm_mday);
        WorkPeriodSummary s = summarizeDay(OUString::fromUtf8(buf));
        if (s.activeMinutes > maxMin)
            maxMin = s.activeMinutes;
        days.push_back(std::move(s));
    }

    OUStringBuffer b;
    b.append(u"—— 近 "_ustr);
    b.append(nDays);
    b.append(u" 日活跃 ——\n"_ustr);
    for (const auto& s : days)
    {
        // label MM-DD
        OUString label = s.periodLabel;
        if (label.getLength() >= 10)
            label = label.copy(5, 5); // MM-DD
        b.append(label);
        b.append(u" "_ustr);
        const sal_Int32 barLen
            = s.activeMinutes <= 0 ? 0
                                   : std::max<sal_Int32>(1, (s.activeMinutes * 16) / maxMin);
        for (sal_Int32 k = 0; k < barLen; ++k)
            b.append(u'█');
        if (barLen == 0)
            b.append(u'·');
        b.append(u" "_ustr);
        b.append(s.activeMinutes);
        b.append(u"分 AI"_ustr);
        b.append(s.aiCalls);
        b.append(u"\n"_ustr);
    }
    return b.makeStringAndClear();
}

namespace
{
OUString xmlEsc(const OUString& s)
{
    OUStringBuffer b;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u'&')
            b.append(u"&amp;"_ustr);
        else if (c == u'<')
            b.append(u"&lt;"_ustr);
        else if (c == u'>')
            b.append(u"&gt;"_ustr);
        else if (c == u'"')
            b.append(u"&quot;"_ustr);
        else
            b.append(c);
    }
    return b.makeStringAndClear();
}
} // namespace

OUString WorkTelemetryStore::formatChartSvg(const WorkPeriodSummary& rSummary, sal_Int32 nSparkDays)
{
    if (nSparkDays < 1)
        nSparkDays = 1;
    if (nSparkDays > 31)
        nSparkDays = 31;

    // Collect day series (oldest → newest)
    TimeValue tv{};
    osl_getSystemTime(&tv);
    std::time_t sec = static_cast<std::time_t>(tv.Seconds);
    std::vector<WorkPeriodSummary> days;
    days.reserve(static_cast<size_t>(nSparkDays));
    sal_Int32 maxMin = 1;
    sal_Int32 maxAi = 1;
    for (sal_Int32 i = nSparkDays - 1; i >= 0; --i)
    {
        const std::time_t t = sec - static_cast<std::time_t>(i) * 86400;
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                      tm.tm_mday);
        WorkPeriodSummary s = summarizeDay(OUString::fromUtf8(buf));
        if (s.activeMinutes > maxMin)
            maxMin = s.activeMinutes;
        if (s.aiCalls > maxAi)
            maxAi = s.aiCalls;
        days.push_back(std::move(s));
    }

    const sal_Int32 width = 720;
    const sal_Int32 padL = 48;
    const sal_Int32 padR = 24;
    const sal_Int32 padT = 48;
    const sal_Int32 chartH = 160;
    const sal_Int32 chartW = width - padL - padR;
    // model rows (max 8) + WoW block (4 metric rows) → total canvas
    const sal_Int32 modelRowsEst
        = std::min(static_cast<sal_Int32>(rSummary.models.size()), sal_Int32(8));
    const sal_Int32 modelBlockH = (modelRowsEst > 0 ? (18 + modelRowsEst * 22) : 40);
    const sal_Int32 wowBlockH = 16 + 4 * 28 + 20;
    const sal_Int32 modelTopEst = padT + chartH + 40;
    const sal_Int32 height = modelTopEst + modelBlockH + 24 + wowBlockH + 24;

    OUStringBuffer b;
    b.append(u"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"_ustr);
    b.append(u"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""_ustr);
    b.append(width);
    b.append(u"\" height=\""_ustr);
    b.append(height);
    b.append(u"\" viewBox=\"0 0 "_ustr);
    b.append(width);
    b.append(u" "_ustr);
    b.append(height);
    b.append(u"\">\n"_ustr);
    b.append(u"  <rect width=\"100%\" height=\"100%\" fill=\"#f7f8fa\"/>\n"_ustr);
    b.append(u"  <text x=\""_ustr);
    b.append(padL);
    b.append(u"\" y=\"28\" font-family=\"system-ui,sans-serif\" font-size=\"16\" "
             u"font-weight=\"600\" fill=\"#1a1a1a\">可圈 工作中台 · "_ustr);
    b.append(xmlEsc(rSummary.periodLabel));
    b.append(u"</text>\n"_ustr);
    b.append(u"  <text x=\""_ustr);
    b.append(padL);
    b.append(u"\" y=\"46\" font-family=\"system-ui,sans-serif\" font-size=\"11\" "
             u"fill=\"#666\">本地生成 · 不上云 · 近 "_ustr);
    b.append(nSparkDays);
    b.append(u" 日活跃分钟（蓝）/ AI 次数（青）</text>\n"_ustr);

    // Day bars
    const sal_Int32 n = static_cast<sal_Int32>(days.size());
    const double slot = n > 0 ? static_cast<double>(chartW) / n : chartW;
    const double barW = std::max(4.0, slot * 0.55);
    for (sal_Int32 i = 0; i < n; ++i)
    {
        const auto& s = days[static_cast<size_t>(i)];
        const double x = padL + i * slot + (slot - barW) / 2.0;
        const double hMin
            = s.activeMinutes <= 0 ? 0.0
                                   : std::max(2.0, (static_cast<double>(s.activeMinutes) / maxMin)
                                                       * (chartH - 20));
        const double yMin = padT + chartH - hMin;
        b.append(u"  <rect x=\""_ustr);
        // print doubles as int for cleaner svg
        b.append(static_cast<sal_Int32>(x));
        b.append(u"\" y=\""_ustr);
        b.append(static_cast<sal_Int32>(yMin));
        b.append(u"\" width=\""_ustr);
        b.append(static_cast<sal_Int32>(barW));
        b.append(u"\" height=\""_ustr);
        b.append(static_cast<sal_Int32>(hMin));
        b.append(u"\" rx=\"3\" fill=\"#3b6fd9\" opacity=\"0.9\"/>\n"_ustr);

        // AI overlay bar (narrower)
        const double barW2 = std::max(2.0, barW * 0.35);
        const double hAi
            = s.aiCalls <= 0
                  ? 0.0
                  : std::max(2.0, (static_cast<double>(s.aiCalls) / maxAi) * (chartH - 20));
        const double yAi = padT + chartH - hAi;
        b.append(u"  <rect x=\""_ustr);
        b.append(static_cast<sal_Int32>(x + barW - barW2));
        b.append(u"\" y=\""_ustr);
        b.append(static_cast<sal_Int32>(yAi));
        b.append(u"\" width=\""_ustr);
        b.append(static_cast<sal_Int32>(barW2));
        b.append(u"\" height=\""_ustr);
        b.append(static_cast<sal_Int32>(hAi));
        b.append(u"\" rx=\"2\" fill=\"#2a9d8f\" opacity=\"0.85\"/>\n"_ustr);

        OUString label = s.periodLabel;
        if (label.getLength() >= 10)
            label = label.copy(8, 2); // DD
        b.append(u"  <text x=\""_ustr);
        b.append(static_cast<sal_Int32>(x + barW / 2));
        b.append(u"\" y=\""_ustr);
        b.append(padT + chartH + 16);
        b.append(u"\" text-anchor=\"middle\" font-family=\"system-ui,sans-serif\" "
                 u"font-size=\"10\" fill=\"#555\">"_ustr);
        b.append(xmlEsc(label));
        b.append(u"</text>\n"_ustr);
    }

    // Model usage section
    const sal_Int32 modelTop = padT + chartH + 40;
    b.append(u"  <text x=\""_ustr);
    b.append(padL);
    b.append(u"\" y=\""_ustr);
    b.append(modelTop);
    b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"13\" font-weight=\"600\" "
             u"fill=\"#1a1a1a\">模型调用（周期汇总）</text>\n"_ustr);

    sal_Int32 maxCalls = 1;
    for (const auto& m : rSummary.models)
        if (m.calls > maxCalls)
            maxCalls = m.calls;

    sal_Int32 row = 0;
    for (size_t i = 0; i < rSummary.models.size() && row < 8; ++i, ++row)
    {
        const auto& m = rSummary.models[i];
        const sal_Int32 y = modelTop + 18 + row * 22;
        const sal_Int32 barMax = chartW - 160;
        const sal_Int32 bw
            = m.calls <= 0 ? 0 : std::max<sal_Int32>(2, (m.calls * barMax) / maxCalls);
        b.append(u"  <text x=\""_ustr);
        b.append(padL);
        b.append(u"\" y=\""_ustr);
        b.append(y + 11);
        b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"11\" fill=\"#333\">"_ustr);
        OUString name = m.model;
        if (name.getLength() > 18)
            name = name.copy(0, 18) + u"…"_ustr;
        b.append(xmlEsc(name));
        b.append(u"</text>\n"_ustr);
        b.append(u"  <rect x=\""_ustr);
        b.append(padL + 130);
        b.append(u"\" y=\""_ustr);
        b.append(y);
        b.append(u"\" width=\""_ustr);
        b.append(bw);
        b.append(u"\" height=\"14\" rx=\"3\" fill=\"#6c5ce7\" opacity=\"0.85\"/>\n"_ustr);
        b.append(u"  <text x=\""_ustr);
        b.append(padL + 136 + bw);
        b.append(u"\" y=\""_ustr);
        b.append(y + 11);
        b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"10\" fill=\"#666\">"_ustr);
        b.append(m.calls);
        b.append(u" · tok≈"_ustr);
        b.append(m.tokensEst);
        b.append(u"</text>\n"_ustr);
    }
    if (rSummary.models.empty())
    {
        b.append(u"  <text x=\""_ustr);
        b.append(padL);
        b.append(u"\" y=\""_ustr);
        b.append(modelTop + 28);
        b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"11\" "
                 u"fill=\"#888\">（尚无模型调用）</text>\n"_ustr);
    }

    // Week-over-week side-by-side bars (this week vs previous rolling 7d)
    const WorkPeriodSummary wowCur = summarizeDaysOffset(0, 7);
    const WorkPeriodSummary wowPrev = summarizeDaysOffset(7, 7);
    const sal_Int32 modelRowsDrawn
        = std::min(static_cast<sal_Int32>(rSummary.models.size()), sal_Int32(8));
    const sal_Int32 wowTop
        = modelTop + (modelRowsDrawn > 0 ? (18 + modelRowsDrawn * 22) : 40) + 24;
    b.append(u"  <text x=\""_ustr);
    b.append(padL);
    b.append(u"\" y=\""_ustr);
    b.append(wowTop);
    b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"13\" font-weight=\"600\" "
             u"fill=\"#1a1a1a\">本周 vs 上周（滚动 7 日）</text>\n"_ustr);
    b.append(u"  <text x=\""_ustr);
    b.append(padL + 200);
    b.append(u"\" y=\""_ustr);
    b.append(wowTop);
    b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"10\" fill=\"#3b6fd9\">本周</text>\n"_ustr);
    b.append(u"  <text x=\""_ustr);
    b.append(padL + 250);
    b.append(u"\" y=\""_ustr);
    b.append(wowTop);
    b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"10\" fill=\"#94a3b8\">上周</text>\n"_ustr);

    const OUString wowLabels[]
        = { u"活跃分钟"_ustr, u"AI 次数"_ustr, u"token≈"_ustr, u"独立文稿"_ustr };
    const sal_Int32 wowCurs[]
        = { wowCur.activeMinutes, wowCur.aiCalls, wowCur.tokensEst, wowCur.uniqueDocs };
    const sal_Int32 wowPrevs[]
        = { wowPrev.activeMinutes, wowPrev.aiCalls, wowPrev.tokensEst, wowPrev.uniqueDocs };
    const sal_Int32 wowBarMax = chartW - 180;
    for (sal_Int32 ri = 0; ri < 4; ++ri)
    {
        const sal_Int32 y = wowTop + 16 + ri * 28;
        const sal_Int32 curV = wowCurs[ri];
        const sal_Int32 prevV = wowPrevs[ri];
        const sal_Int32 scale = std::max(std::max(curV, prevV), sal_Int32(1));
        const sal_Int32 bwCur
            = curV <= 0 ? 0 : std::max<sal_Int32>(2, (curV * wowBarMax) / scale);
        const sal_Int32 bwPrev
            = prevV <= 0 ? 0 : std::max<sal_Int32>(2, (prevV * wowBarMax) / scale);
        b.append(u"  <text x=\""_ustr);
        b.append(padL);
        b.append(u"\" y=\""_ustr);
        b.append(y + 12);
        b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"11\" fill=\"#333\">"_ustr);
        b.append(xmlEsc(wowLabels[ri]));
        b.append(u"</text>\n"_ustr);
        b.append(u"  <rect x=\""_ustr);
        b.append(padL + 90);
        b.append(u"\" y=\""_ustr);
        b.append(y);
        b.append(u"\" width=\""_ustr);
        b.append(bwCur);
        b.append(u"\" height=\"10\" rx=\"2\" fill=\"#3b6fd9\" opacity=\"0.9\"/>\n"_ustr);
        b.append(u"  <rect x=\""_ustr);
        b.append(padL + 90);
        b.append(u"\" y=\""_ustr);
        b.append(y + 12);
        b.append(u"\" width=\""_ustr);
        b.append(bwPrev);
        b.append(u"\" height=\"8\" rx=\"2\" fill=\"#94a3b8\" opacity=\"0.85\"/>\n"_ustr);
        b.append(u"  <text x=\""_ustr);
        b.append(padL + 96 + std::max(bwCur, bwPrev));
        b.append(u"\" y=\""_ustr);
        b.append(y + 12);
        b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"10\" fill=\"#555\">"_ustr);
        b.append(curV);
        b.append(u" / "_ustr);
        b.append(prevV);
        b.append(u"</text>\n"_ustr);
    }

    b.append(u"  <text x=\""_ustr);
    b.append(padL);
    b.append(u"\" y=\""_ustr);
    b.append(height - 14);
    b.append(u"\" font-family=\"system-ui,sans-serif\" font-size=\"10\" fill=\"#999\">可圈office "
             u"Workbench · SVG · local-first</text>\n"_ustr);
    b.append(u"</svg>\n"_ustr);
    return b.makeStringAndClear();
}

OUString WorkTelemetryStore::formatReportMarkdown(const WorkPeriodSummary& r, const OUString& rTitleExtra)
{
    OUStringBuffer b;
    b.append(u"# 可圈 工作中台报告\n\n"_ustr);
    b.append(u"- 周期：`"_ustr);
    b.append(r.periodLabel);
    b.append(u"`\n"_ustr);
    if (!rTitleExtra.isEmpty())
    {
        b.append(u"- "_ustr);
        b.append(rTitleExtra);
        b.append(u"\n"_ustr);
    }
    b.append(u"- 生成：本地 · 不上云 · Token 为字符估算\n\n"_ustr);

    b.append(u"## 概览\n\n"_ustr);
    b.append(u"| 指标 | 数值 |\n|---|---|\n"_ustr);
    b.append(u"| 活跃分钟 | "_ustr);
    b.append(r.activeMinutes);
    b.append(u" |\n| 文稿打开 | "_ustr);
    b.append(r.docOpens);
    b.append(u" |\n| 文稿保存 | "_ustr);
    b.append(r.docSaves);
    b.append(u" |\n| 新建文稿 | "_ustr);
    b.append(r.docNews);
    b.append(u" |\n| 独立文稿 | "_ustr);
    b.append(r.uniqueDocs);
    b.append(u" |\n| AI 调用 | "_ustr);
    b.append(r.aiCalls);
    b.append(u" |\n| 预估 Token | "_ustr);
    b.append(r.tokensEst);
    b.append(u" |\n| 写回批准 | "_ustr);
    b.append(r.applies);
    b.append(u" |\n| 截图 | "_ustr);
    b.append(r.screenshots);
    b.append(u" |\n| 语音 | "_ustr);
    b.append(r.voiceEvents);
    b.append(u" |\n| 测试通过 | "_ustr);
    b.append(r.testsPassed);
    b.append(u" |\n| 记事本事件 | "_ustr);
    b.append(r.notebookNotes);
    b.append(u" |\n\n"_ustr);

    // Local week goal progress
    b.append(u"## 本周目标\n\n"_ustr);
    b.append(formatWeekGoalProgress());
    b.append(u"\n\n"_ustr);

    // Week-over-week (rolling 7d) — always local compare, independent of report period
    b.append(u"## 本周 vs 上周\n\n"_ustr);
    b.append(u"```\n"_ustr);
    b.append(formatWeekOverWeekSparkline());
    b.append(u"\n```\n\n"_ustr);
    b.append(formatWeekOverWeekCompare());
    b.append(u"\n"_ustr);

    // ASCII bar chart for models
    b.append(u"## 模型使用\n\n"_ustr);
    if (r.models.empty())
        b.append(u"_暂无模型调用_\n\n"_ustr);
    else
    {
        sal_Int32 maxCalls = 1;
        for (const auto& m : r.models)
            if (m.calls > maxCalls)
                maxCalls = m.calls;
        for (size_t i = 0; i < r.models.size() && i < 12; ++i)
        {
            const auto& m = r.models[i];
            const sal_Int32 barLen = std::max<sal_Int32>(1, (m.calls * 20) / maxCalls);
            b.append(u"- **"_ustr);
            b.append(m.model);
            b.append(u"** `"_ustr);
            for (sal_Int32 k = 0; k < barLen; ++k)
                b.append(u'█');
            b.append(u"` 调用="_ustr);
            b.append(m.calls);
            b.append(u" token≈"_ustr);
            b.append(m.tokensEst);
            b.append(u"\n"_ustr);
        }
        b.append(u"\n"_ustr);
    }

    b.append(u"## 文稿触达（Top）\n\n"_ustr);
    if (r.docs.empty())
        b.append(u"_暂无文稿事件_\n"_ustr);
    else
    {
        for (size_t i = 0; i < r.docs.size() && i < 15; ++i)
        {
            const auto& d = r.docs[i];
            b.append(u"- "_ustr);
            b.append(d.docTitle.isEmpty() ? d.docHash : d.docTitle);
            b.append(u" — 开"_ustr);
            b.append(d.opens);
            b.append(u"/存"_ustr);
            b.append(d.saves);
            b.append(u"/AI"_ustr);
            b.append(d.aiTouches);
            b.append(u"\n"_ustr);
        }
    }
    b.append(u"\n---\n*可圈office 工作中台 · local-first*\n"_ustr);
    return b.makeStringAndClear();
}

OUString WorkTelemetryStore::exportReport(const OUString& rPeriod)
{
    WorkPeriodSummary s;
    OUString tag = rPeriod;
    if (rPeriod == u"today"_ustr || rPeriod.isEmpty())
    {
        s = summarizeToday();
        tag = u"day-"_ustr + todayKey();
    }
    else if (rPeriod == u"week"_ustr)
    {
        s = summarizeLastDays(7);
        tag = u"week-"_ustr + todayKey();
    }
    else if (rPeriod == u"month"_ustr)
    {
        const OUString t = todayKey();
        s = summarizeMonth(t.getLength() >= 7 ? t.copy(0, 7) : t);
        tag = u"month-"_ustr + s.periodLabel;
    }
    else if (rPeriod == u"year"_ustr)
    {
        const OUString t = todayKey();
        s = summarizeYear(t.getLength() >= 4 ? t.copy(0, 4) : t);
        tag = u"year-"_ustr + s.periodLabel;
    }
    else if (rPeriod.getLength() == 4)
        s = summarizeYear(rPeriod);
    else if (rPeriod.getLength() == 7)
        s = summarizeMonth(rPeriod);
    else
        s = summarizeDay(rPeriod);

    const OUString dir = rootDir() + u"/reports"_ustr;
    ensureDirSys(dir);
    const OUString base = dir + u"/work-report-"_ustr + tag;
    const OUString path = base + u".md"_ustr;
    const OUString svgPath = base + u".svg"_ustr;

    // SVG chart first so markdown can reference sibling file name
    const OUString svg = formatChartSvg(s, rPeriod == u"month"_ustr ? 30 : 7);
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(svgPath, url) == osl::FileBase::E_None)
        {
            osl::File f(url);
            auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
            if (e != osl::FileBase::E_None)
                e = f.open(osl_File_OpenFlag_Write);
            if (e == osl::FileBase::E_None)
            {
                f.setSize(0);
                const OString utf8 = OUStringToOString(svg, RTL_TEXTENCODING_UTF8);
                sal_uInt64 n = 0;
                f.write(utf8.getStr(), utf8.getLength(), n);
                f.close();
            }
        }
    }

    OUString md = formatReportMarkdown(s);
    // Append chart reference (relative path works when viewing folder)
    {
        const sal_Int32 slash = svgPath.lastIndexOf(u'/');
        const OUString svgName
            = slash >= 0 ? svgPath.copy(slash + 1) : svgPath;
        md += u"\n## 图表\n\n![工作中台图表]("_ustr + svgName
              + u")\n\n_文件：`"_ustr + svgPath + u"`_\n"_ustr;
        md += u"\n_也已生成 HTML（内嵌图）：`"_ustr + base + u".html`_\n"_ustr;
    }

    auto writeTextFile = [](const OUString& rSys, const OUString& rContent) -> bool {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
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
        const bool ok = f.write(utf8.getStr(), utf8.getLength(), n) == osl::FileBase::E_None;
        f.close();
        return ok;
    };

    if (!writeTextFile(path, md))
        return OUString();

    // HTML with embedded SVG for one-click browser view
    const OUString htmlPath = base + u".html"_ustr;
    writeTextFile(htmlPath, formatReportHtml(s, svg));

    // Week export counts as light archive (md/html/svg set)
    if (rPeriod == u"week"_ustr)
    {
        markWeekArchived(htmlPath);
        recordSimple(u"report_export"_ustr, 1);
    }

    return path;
}

OUString WorkTelemetryStore::htmlPathForReport(const OUString& rMarkdownPath)
{
    if (rMarkdownPath.endsWith(u".md"_ustr))
        return rMarkdownPath.copy(0, rMarkdownPath.getLength() - 3) + u".html"_ustr;
    if (rMarkdownPath.endsWith(u".svg"_ustr))
        return rMarkdownPath.copy(0, rMarkdownPath.getLength() - 4) + u".html"_ustr;
    return rMarkdownPath + u".html"_ustr;
}

OUString WorkTelemetryStore::exportShareBundle(const OUString& rPeriod)
{
    const OUString mdPath = exportReport(rPeriod);
    if (mdPath.isEmpty())
        return OUString();

    // Derive base name without extension
    OUString base = mdPath;
    if (base.endsWith(u".md"_ustr))
        base = base.copy(0, base.getLength() - 3);
    const sal_Int32 slash = base.lastIndexOf(u'/');
    const OUString name = slash >= 0 ? base.copy(slash + 1) : base;

    const OUString shareRoot = rootDir() + u"/share"_ustr;
    ensureDirSys(shareRoot);
    const OUString bundle = shareRoot + u"/"_ustr + name;
    ensureDirSys(bundle);

    auto copyOne = [](const OUString& fromSys, const OUString& toSys) -> bool {
        OUString fromUrl, toUrl;
        if (osl::FileBase::getFileURLFromSystemPath(fromSys, fromUrl) != osl::FileBase::E_None)
            return false;
        if (osl::FileBase::getFileURLFromSystemPath(toSys, toUrl) != osl::FileBase::E_None)
            return false;
        // overwrite if exists
        osl::File::remove(toUrl);
        return osl::File::copy(fromUrl, toUrl) == osl::FileBase::E_None;
    };

    const OUString html = htmlPathForReport(mdPath);
    OUString svg = mdPath;
    if (svg.endsWith(u".md"_ustr))
        svg = svg.copy(0, svg.getLength() - 3) + u".svg"_ustr;

    bool any = false;
    if (copyOne(mdPath, bundle + u"/"_ustr + name + u".md"_ustr))
        any = true;
    if (copyOne(html, bundle + u"/"_ustr + name + u".html"_ustr))
        any = true;
    if (copyOne(svg, bundle + u"/"_ustr + name + u".svg"_ustr))
        any = true;

    // README for recipients
    {
        OUStringBuffer rb;
        rb.append(u"# 可圈 工作中台分享包\n\n"_ustr);
        rb.append(u"- 打开 `*_work-report-*.html` 可在浏览器查看图表与表格\n"_ustr);
        rb.append(u"- `.md` 为 Markdown 正文；`.svg` 为矢量图\n"_ustr);
        rb.append(u"- 全部本地生成，未上传云端\n"_ustr);
        rb.append(u"- 周期标签：`"_ustr);
        rb.append(name);
        rb.append(u"`\n"_ustr);
        const OUString readme = bundle + u"/README.txt"_ustr;
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(readme, url) == osl::FileBase::E_None)
        {
            osl::File f(url);
            auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
            if (e != osl::FileBase::E_None)
                e = f.open(osl_File_OpenFlag_Write);
            if (e == osl::FileBase::E_None)
            {
                f.setSize(0);
                const OString utf8 = OUStringToOString(rb.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
                sal_uInt64 n = 0;
                f.write(utf8.getStr(), utf8.getLength(), n);
                f.close();
            }
        }
    }

    return any ? bundle : OUString();
}

namespace
{
OUString shellQuoteSys(const OUString& r)
{
    OUStringBuffer b;
    b.append(u'\'');
    for (sal_Int32 i = 0; i < r.getLength(); ++i)
    {
        if (r[i] == u'\'')
            b.append(u"'\\''"_ustr);
        else
            b.append(r[i]);
    }
    b.append(u'\'');
    return b.makeStringAndClear();
}

bool pathExistsSysFile(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    return osl::DirectoryItem::get(url, item) == osl::FileBase::E_None;
}
} // namespace

OUString WorkTelemetryStore::exportShareZip(const OUString& rPeriod)
{
    const OUString bundle = exportShareBundle(rPeriod);
    if (bundle.isEmpty())
        return OUString();

    const sal_Int32 slash = bundle.lastIndexOf(u'/');
    if (slash <= 0)
    {
        markWeekArchived(bundle);
        recordSimple(u"report_export"_ustr, 1);
        return bundle;
    }
    const OUString parent = bundle.copy(0, slash);
    const OUString name = bundle.copy(slash + 1);
    const OUString zipPath = parent + u"/"_ustr + name + u".zip"_ustr;

    // Remove stale zip
    {
        OUString zipUrl;
        if (osl::FileBase::getFileURLFromSystemPath(zipPath, zipUrl) == osl::FileBase::E_None)
            osl::File::remove(zipUrl);
    }

#if defined(_WIN32)
    // PowerShell Compress-Archive
    OUStringBuffer cmd;
    cmd.append(u"powershell -NoProfile -Command \"Compress-Archive -Path "_ustr);
    cmd.append(shellQuoteSys(bundle + u"/*"_ustr));
    cmd.append(u" -DestinationPath "_ustr);
    cmd.append(shellQuoteSys(zipPath));
    cmd.append(u" -Force\""_ustr);
    const OString cmd8 = OUStringToOString(cmd.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    std::system(cmd8.getStr());
#else
    // zip -r relative to parent so archive has folder name as root
    OUStringBuffer cmd;
    cmd.append(u"cd "_ustr);
    cmd.append(shellQuoteSys(parent));
    cmd.append(u" && zip -qr "_ustr);
    cmd.append(shellQuoteSys(name + u".zip"_ustr));
    cmd.append(u" "_ustr);
    cmd.append(shellQuoteSys(name));
    const OString cmd8 = OUStringToOString(cmd.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    std::system(cmd8.getStr());
#endif

    if (pathExistsSysFile(zipPath))
    {
        markWeekArchived(zipPath);
        recordSimple(u"report_export"_ustr, 1);
        return zipPath;
    }
    // zip tool missing — still return folder
    markWeekArchived(bundle);
    recordSimple(u"report_export"_ustr, 1);
    return bundle;
}

OUString WorkTelemetryStore::weekArchiveKey() { return u"week-"_ustr + todayKey(); }

bool WorkTelemetryStore::isWeekArchived()
{
    const OUString path = rootDir() + u"/last-week-archive.txt"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 512)
    {
        f.close();
        return false;
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    OUString content = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n))).trim();
    // first line is key
    const sal_Int32 nl = content.indexOf(u'\n');
    if (nl >= 0)
        content = content.copy(0, nl).trim();
    return content == weekArchiveKey();
}

void WorkTelemetryStore::markWeekArchived(const OUString& rArtifactPath)
{
    ensureDirSys(rootDir());
    const OUString path = rootDir() + u"/last-week-archive.txt"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return;
    OUStringBuffer b;
    b.append(weekArchiveKey());
    b.append(u'\n');
    if (!rArtifactPath.isEmpty())
    {
        b.append(rArtifactPath);
        b.append(u'\n');
    }
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return;
    f.setSize(0);
    const OString utf8 = OUStringToOString(b.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();

    // Append to local archive history (newest last in file; list reverses)
    if (!rArtifactPath.isEmpty())
    {
        const OUString hist = rootDir() + u"/archive-history.jsonl"_ustr;
        OUString hurl;
        if (osl::FileBase::getFileURLFromSystemPath(hist, hurl) == osl::FileBase::E_None)
        {
            osl::File hf(hurl);
            auto he = hf.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
            if (he != osl::FileBase::E_None)
                he = hf.open(osl_File_OpenFlag_Write);
            if (he == osl::FileBase::E_None)
            {
                sal_uInt64 sz = 0;
                hf.getSize(sz);
                (void)hf.setPos(osl_Pos_Absolut, sz);
                OUStringBuffer line;
                line.append(u"{\"ts\":\""_ustr);
                line.append(todayKey());
                line.append(u"\",\"key\":\""_ustr);
                line.append(weekArchiveKey());
                line.append(u"\",\"path\":\""_ustr);
                // minimal escape for path quotes/backslashes
                OUString p = rArtifactPath;
                p = p.replaceAll(u"\\"_ustr, u"\\\\"_ustr).replaceAll(u"\""_ustr, u"\\\""_ustr);
                line.append(p);
                line.append(u"\"}\n"_ustr);
                const OString u8 = OUStringToOString(line.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
                sal_uInt64 wn = 0;
                hf.write(u8.getStr(), u8.getLength(), wn);
                hf.close();
            }
        }
    }
}

std::vector<OUString> WorkTelemetryStore::listArchiveHistory(sal_Int32 nMax)
{
    std::vector<OUString> out;
    if (nMax < 1)
        nMax = 1;
    if (nMax > 50)
        nMax = 50;
    const OUString hist = rootDir() + u"/archive-history.jsonl"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(hist, url) != osl::FileBase::E_None)
        return out;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return out;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 512 * 1024)
    {
        f.close();
        return out;
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    const OUString content
        = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
    std::vector<OUString> all;
    sal_Int32 start = 0;
    while (start < content.getLength())
    {
        sal_Int32 end = content.indexOf(u'\n', start);
        if (end < 0)
            end = content.getLength();
        OUString line = content.copy(start, end - start).trim();
        start = end + 1;
        if (line.isEmpty())
            continue;
        // extract "path":"..."
        const OUString key = u"\"path\":\""_ustr;
        const sal_Int32 pi = line.indexOf(key);
        if (pi < 0)
            continue;
        sal_Int32 i = pi + key.getLength();
        OUStringBuffer pb;
        while (i < line.getLength())
        {
            const sal_Unicode c = line[i];
            if (c == u'\\' && i + 1 < line.getLength())
            {
                pb.append(line[i + 1]);
                i += 2;
                continue;
            }
            if (c == u'"')
                break;
            pb.append(c);
            ++i;
        }
        const OUString p = pb.makeStringAndClear();
        if (!p.isEmpty())
            all.push_back(p);
    }
    // newest last in file → reverse
    for (auto it = all.rbegin(); it != all.rend() && static_cast<sal_Int32>(out.size()) < nMax;
         ++it)
        out.push_back(*it);
    return out;
}

OUString WorkTelemetryStore::formatArchiveHistoryTip(sal_Int32 nMax)
{
    const auto items = listArchiveHistory(nMax);
    if (items.empty())
        return u"—— 归档历史 ——\n（尚无打包记录；点「完成本周」或「打包ZIP」）\n"_ustr;
    OUStringBuffer b;
    b.append(u"—— 归档历史（近 "_ustr);
    b.append(static_cast<sal_Int32>(items.size()));
    b.append(u" 次）——\n"_ustr);
    for (const auto& p : items)
    {
        b.append(u"· "_ustr);
        b.append(p);
        b.append(u"\n"_ustr);
    }
    return b.makeStringAndClear();
}

bool WorkTelemetryStore::removeArchiveHistoryEntry(const OUString& rPath)
{
    if (rPath.isEmpty())
        return false;
    const OUString hist = rootDir() + u"/archive-history.jsonl"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(hist, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 512 * 1024)
    {
        f.close();
        return false;
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    const OUString content
        = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
    OUStringBuffer out;
    bool removed = false;
    sal_Int32 start = 0;
    while (start < content.getLength())
    {
        sal_Int32 end = content.indexOf(u'\n', start);
        if (end < 0)
            end = content.getLength();
        const OUString line = content.copy(start, end - start);
        start = end + 1;
        if (line.isEmpty())
            continue;
        // match "path":"<rPath>" with optional escaping of quotes
        if (line.indexOf(rPath) >= 0)
        {
            removed = true;
            continue;
        }
        out.append(line);
        out.append(u'\n');
    }
    if (!removed)
        return false;
    osl::File wf(url);
    auto e = wf.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = wf.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    wf.setSize(0);
    const OString utf8 = OUStringToOString(out.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    sal_uInt64 wn = 0;
    wf.write(utf8.getStr(), utf8.getLength(), wn);
    wf.close();
    return true;
}

bool WorkTelemetryStore::clearArchiveHistory()
{
    const OUString hist = rootDir() + u"/archive-history.jsonl"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(hist, url) != osl::FileBase::E_None)
        return false;
    // Truncate or create empty
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
    {
        // try remove
        return osl::File::remove(url) == osl::FileBase::E_None;
    }
    f.setSize(0);
    f.close();
    return true;
}

OUString WorkTelemetryStore::exportArchiveHistoryCsv()
{
    ensureDirSys(rootDir());
    const OUString dir = rootDir() + u"/reports"_ustr;
    ensureDirSys(dir);
    const OUString path = dir + u"/archive-history-"_ustr + todayKey() + u".csv"_ustr;
    const auto items = listArchiveHistory(200);
    OUStringBuffer csv;
    csv.append(u"index,path\n"_ustr);
    sal_Int32 i = 1;
    for (const auto& p : items)
    {
        csv.append(i++);
        csv.append(u",\""_ustr);
        // escape quotes in path
        OUString esc = p;
        esc = esc.replaceAll(u"\""_ustr, u"\"\""_ustr);
        csv.append(esc);
        csv.append(u"\"\n"_ustr);
    }
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return OUString();
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return OUString();
    f.setSize(0);
    const OString utf8 = OUStringToOString(csv.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
    return path;
}

sal_Int32 WorkTelemetryStore::getWeekGoalMinutes()
{
    const OUString path = rootDir() + u"/week-goal.txt"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return 0;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return 0;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 32)
    {
        f.close();
        return 0;
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    const OUString s
        = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n))).trim();
    sal_Int32 v = s.toInt32();
    if (v < 0)
        v = 0;
    if (v > 10080) // 7*24*60
        v = 10080;
    return v;
}

bool WorkTelemetryStore::setWeekGoalMinutes(sal_Int32 nMinutes)
{
    if (nMinutes < 0)
        nMinutes = 0;
    if (nMinutes > 10080)
        nMinutes = 10080;
    ensureDirSys(rootDir());
    const OUString path = rootDir() + u"/week-goal.txt"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString utf8
        = OUStringToOString(OUString::number(nMinutes), RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
    return true;
}

OUString WorkTelemetryStore::formatWeekGoalProgress()
{
    const sal_Int32 goal = getWeekGoalMinutes();
    if (goal <= 0)
        return u"本周目标：未设置 · 在中台输入分钟数后点「设目标」"_ustr;
    const WorkPeriodSummary cur = summarizeDaysOffset(0, 7);
    const sal_Int32 done = cur.activeMinutes;
    sal_Int32 pct = (done * 100) / goal;
    if (pct > 999)
        pct = 999;
    const sal_Int32 barW = 16;
    sal_Int32 filled = (done * barW) / goal;
    if (done > 0 && filled < 1)
        filled = 1;
    if (filled > barW)
        filled = barW;
    OUStringBuffer b;
    b.append(u"本周目标 "_ustr);
    b.append(goal);
    b.append(u" 分 · 已用 "_ustr);
    b.append(done);
    b.append(u" · 进度 "_ustr);
    b.append(pct);
    b.append(u"% "_ustr);
    for (sal_Int32 i = 0; i < filled; ++i)
        b.append(u'█');
    for (sal_Int32 i = filled; i < barW; ++i)
        b.append(u'░');
    if (done >= goal)
        b.append(u" ✓"_ustr);
    return b.makeStringAndClear();
}

OUString WorkTelemetryStore::formatWeekEmailBody(const OUString& rArtifactPath)
{
    const WorkPeriodSummary s = summarizeLastDays(7);
    OUStringBuffer body;
    body.append(u"你好，\n\n请手动附加本地文件（邮件客户端无法自动附带）：\n"_ustr);
    if (!rArtifactPath.isEmpty())
    {
        body.append(rArtifactPath);
        body.append(u"\n"_ustr);
    }
    body.append(u"\n—— 近 7 日概览 ——\n"_ustr);
    body.append(u"活跃分钟："_ustr);
    body.append(s.activeMinutes);
    body.append(u"\nAI 调用："_ustr);
    body.append(s.aiCalls);
    body.append(u"\n预估 Token："_ustr);
    body.append(s.tokensEst);
    body.append(u"\n文稿打开/保存/新建："_ustr);
    body.append(s.docOpens);
    body.append(u"/");
    body.append(s.docSaves);
    body.append(u"/");
    body.append(s.docNews);
    body.append(u"\n独立文稿："_ustr);
    body.append(s.uniqueDocs);
    body.append(u"\n\n"_ustr);
    body.append(formatWeekGoalProgress());
    body.append(u"\n\n"_ustr);
    body.append(formatWeekOverWeekCompare());
    body.append(u"\n"_ustr);
    body.append(formatWeekOverWeekSparkline());
    body.append(u"\n\n（可圈office 本地生成 · 不上云）\n"_ustr);
    return body.makeStringAndClear();
}

OUString WorkTelemetryStore::weeklyArchiveTip()
{
    if (isWeekArchived())
        return OUString();
    return u"本周尚未打包归档 · 建议点「打包ZIP」或「邮件」完成本地周报"_ustr;
}

bool WorkTelemetryStore::maybeWeeklySystemReminder()
{
    // Throttle: one reminder per local day
    const OUString today = todayKey();
    const OUString flag = rootDir() + u"/last-reminder-day.txt"_ustr;
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(flag, url) == osl::FileBase::E_None)
        {
            osl::File f(url);
            if (f.open(osl_File_OpenFlag_Read) == osl::FileBase::E_None)
            {
                sal_uInt64 sz = 0;
                f.getSize(sz);
                if (sz > 0 && sz < 64)
                {
                    std::vector<char> buf(static_cast<size_t>(sz));
                    sal_uInt64 n = 0;
                    f.read(buf.data(), sz, n);
                    f.close();
                    const OUString prev
                        = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)))
                              .trim();
                    if (prev == today)
                        return false;
                }
                else
                    f.close();
            }
        }
    }

    if (isWeekArchived())
        return false;

    // Fri=5 Sat=6 Sun=0 in localtime
    TimeValue tv{};
    osl_getSystemTime(&tv);
    std::time_t sec = static_cast<std::time_t>(tv.Seconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &sec);
#else
    localtime_r(&sec, &tm);
#endif
    const int wday = tm.tm_wday; // 0=Sun ... 5=Fri 6=Sat
    if (!(wday == 5 || wday == 6 || wday == 0))
        return false;

    // Write throttle flag first to avoid spam if notify fails
    ensureDirSys(rootDir());
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(flag, url) == osl::FileBase::E_None)
        {
            osl::File f(url);
            auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
            if (e != osl::FileBase::E_None)
                e = f.open(osl_File_OpenFlag_Write);
            if (e == osl::FileBase::E_None)
            {
                f.setSize(0);
                const OString utf8 = OUStringToOString(today, RTL_TEXTENCODING_UTF8);
                sal_uInt64 n = 0;
                f.write(utf8.getStr(), utf8.getLength(), n);
                f.close();
            }
        }
    }

#if defined(MACOSX) || defined(__APPLE__)
    // Local notification — no network
    const char* cmd = "osascript -e 'display notification "
                      "\"本周工作报告尚未打包归档，打开可圈侧栏「工作中台」点打包ZIP或邮件\" "
                      "with title \"可圈 工作中台\"' 2>/dev/null";
    std::system(cmd);
#endif
    recordSimple(u"report_reminder"_ustr, 1);
    return true;
}

bool WorkTelemetryStore::maybeWeekGoalReachedReminder()
{
    const sal_Int32 goal = getWeekGoalMinutes();
    if (goal <= 0)
        return false;

    const WorkPeriodSummary cur = summarizeDaysOffset(0, 7);
    if (cur.activeMinutes < goal)
        return false;

    // Throttle: one celebration per local day
    const OUString today = todayKey();
    const OUString flag = rootDir() + u"/last-goal-reached-day.txt"_ustr;
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(flag, url) == osl::FileBase::E_None)
        {
            osl::File f(url);
            if (f.open(osl_File_OpenFlag_Read) == osl::FileBase::E_None)
            {
                sal_uInt64 sz = 0;
                f.getSize(sz);
                if (sz > 0 && sz < 64)
                {
                    std::vector<char> buf(static_cast<size_t>(sz));
                    sal_uInt64 n = 0;
                    f.read(buf.data(), sz, n);
                    f.close();
                    const OUString prev
                        = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)))
                              .trim();
                    if (prev == today)
                        return false;
                }
                else
                    f.close();
            }
        }
    }

    ensureDirSys(rootDir());
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(flag, url) == osl::FileBase::E_None)
        {
            osl::File f(url);
            auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
            if (e != osl::FileBase::E_None)
                e = f.open(osl_File_OpenFlag_Write);
            if (e == osl::FileBase::E_None)
            {
                f.setSize(0);
                const OString utf8 = OUStringToOString(today, RTL_TEXTENCODING_UTF8);
                sal_uInt64 n = 0;
                f.write(utf8.getStr(), utf8.getLength(), n);
                f.close();
            }
        }
    }

#if defined(MACOSX) || defined(__APPLE__)
    const char* cmd = "osascript -e 'display notification "
                      "\"本周活跃目标已达成，打开可圈侧栏「工作中台」查看进度\" "
                      "with title \"可圈 工作中台 · 目标达成\"' 2>/dev/null";
    std::system(cmd);
#endif
    recordSimple(u"goal_reached"_ustr, 1);
    return true;
}

OUString WorkTelemetryStore::lastArchiveArtifactPath()
{
    const OUString path = rootDir() + u"/last-week-archive.txt"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return OUString();
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return OUString();
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 2048)
    {
        f.close();
        return OUString();
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    const OUString content
        = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
    // line1 = key, line2 = path
    const sal_Int32 nl = content.indexOf(u'\n');
    if (nl < 0 || nl + 1 >= content.getLength())
        return OUString();
    OUString art = content.copy(nl + 1).trim();
    const sal_Int32 nl2 = art.indexOf(u'\n');
    if (nl2 >= 0)
        art = art.copy(0, nl2).trim();
    return art;
}

OUString WorkTelemetryStore::formatReportHtml(const WorkPeriodSummary& r, const OUString& rSvg,
                                              const OUString& rTitleExtra)
{
    OUString svgBody = rSvg;
    // drop XML declaration for inline embed
    if (svgBody.startsWith(u"<?xml"_ustr))
    {
        const sal_Int32 gt = svgBody.indexOf(u'>');
        if (gt >= 0)
            svgBody = svgBody.copy(gt + 1).trim();
    }

    OUStringBuffer b;
    b.append(u"<!DOCTYPE html>\n<html lang=\"zh-CN\"><head><meta charset=\"utf-8\"/>\n"_ustr);
    b.append(u"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n"_ustr);
    b.append(u"<title>可圈 工作中台 · "_ustr);
    b.append(xmlEsc(r.periodLabel));
    b.append(u"</title>\n<style>\n"_ustr);
    b.append(u"body{font-family:system-ui,-apple-system,sans-serif;max-width:860px;"
             u"margin:28px auto;padding:0 16px;color:#1a1a1a;background:#fafbfc;}\n"_ustr);
    b.append(u"h1{font-size:1.35rem;margin:0 0 8px;} .meta{color:#666;font-size:.9rem;"
             u"margin-bottom:20px;}\n"_ustr);
    b.append(u"table{border-collapse:collapse;width:100%;background:#fff;margin:12px 0 24px;}\n"_ustr);
    b.append(u"th,td{border:1px solid #e2e5ea;padding:8px 12px;text-align:left;}\n"_ustr);
    b.append(u"th{background:#f0f2f5;} .chart{background:#fff;border:1px solid #e2e5ea;"
             u"border-radius:8px;padding:12px;overflow:auto;}\n"_ustr);
    b.append(u"footer{color:#999;font-size:.8rem;margin-top:32px;}\n"_ustr);
    b.append(u"</style></head><body>\n"_ustr);
    b.append(u"<h1>可圈 工作中台报告</h1>\n<div class=\"meta\">周期："_ustr);
    b.append(xmlEsc(r.periodLabel));
    if (!rTitleExtra.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(xmlEsc(rTitleExtra));
    }
    b.append(u" · 本地生成 · 不上云</div>\n"_ustr);
    b.append(u"<div class=\"chart\">\n"_ustr);
    b.append(svgBody);
    b.append(u"\n</div>\n"_ustr);
    b.append(u"<h2>概览</h2>\n<table><tr><th>指标</th><th>数值</th></tr>\n"_ustr);
    auto row = [&](const OUString& k, sal_Int32 v) {
        b.append(u"<tr><td>"_ustr);
        b.append(k);
        b.append(u"</td><td>"_ustr);
        b.append(v);
        b.append(u"</td></tr>\n"_ustr);
    };
    row(u"活跃分钟"_ustr, r.activeMinutes);
    row(u"文稿打开"_ustr, r.docOpens);
    row(u"文稿保存"_ustr, r.docSaves);
    row(u"新建文稿"_ustr, r.docNews);
    row(u"独立文稿"_ustr, r.uniqueDocs);
    row(u"AI 调用"_ustr, r.aiCalls);
    row(u"预估 Token"_ustr, r.tokensEst);
    row(u"写回批准"_ustr, r.applies);
    row(u"截图"_ustr, r.screenshots);
    row(u"语音"_ustr, r.voiceEvents);
    row(u"测试通过"_ustr, r.testsPassed);
    row(u"记事本事件"_ustr, r.notebookNotes);
    b.append(u"</table>\n"_ustr);

    b.append(u"<h2>本周目标</h2>\n"_ustr);
    b.append(u"<pre style=\"background:#fff;border:1px solid #e2e5ea;border-radius:8px;"
             u"padding:12px;overflow:auto;font-size:13px;line-height:1.5\">"_ustr);
    b.append(xmlEsc(formatWeekGoalProgress()));
    b.append(u"</pre>\n"_ustr);

    b.append(u"<h2>本周 vs 上周</h2>\n"_ustr);
    b.append(u"<pre style=\"background:#fff;border:1px solid #e2e5ea;border-radius:8px;"
             u"padding:12px;overflow:auto;font-size:13px;line-height:1.5\">"_ustr);
    b.append(xmlEsc(formatWeekOverWeekSparkline()));
    b.append(u"\n\n"_ustr);
    b.append(xmlEsc(formatWeekOverWeekCompare()));
    b.append(u"</pre>\n"_ustr);

    b.append(u"<h2>模型使用</h2>\n"_ustr);
    if (r.models.empty())
        b.append(u"<p>（暂无）</p>\n"_ustr);
    else
    {
        b.append(u"<table><tr><th>模型</th><th>调用</th><th>token≈</th></tr>\n"_ustr);
        for (size_t i = 0; i < r.models.size() && i < 12; ++i)
        {
            const auto& m = r.models[i];
            b.append(u"<tr><td>"_ustr);
            b.append(xmlEsc(m.model));
            b.append(u"</td><td>"_ustr);
            b.append(m.calls);
            b.append(u"</td><td>"_ustr);
            b.append(m.tokensEst);
            b.append(u"</td></tr>\n"_ustr);
        }
        b.append(u"</table>\n"_ustr);
    }

    b.append(u"<footer>可圈office · local-first workbench</footer>\n</body></html>\n"_ustr);
    return b.makeStringAndClear();
}

bool WorkTelemetryStore::createDesktopShortcuts(OUString* pMessage)
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
    {
        if (pMessage)
            *pMessage = u"无法定位用户主目录"_ustr;
        return false;
    }
    const OUString desktop = OUString::fromUtf8(home) + u"/Desktop"_ustr;
    ensureDirSys(desktop);

#if defined(MACOSX) || defined(__APPLE__)
    const OUString wPath = desktop + u"/可圈-工作中台.command"_ustr;
    const OUString nPath = desktop + u"/可圈-记事本.command"_ustr;
    const OUString pPath = desktop + u"/可圈-效率挂坠.command"_ustr;
    const OUString wBody
        = u"#!/bin/bash\n"
          u"open -a \"可圈office\" 2>/dev/null || open -a \"可圈办公\" 2>/dev/null || true\n"
          u"osascript -e 'display notification \"左侧：工作中台 · 快捷键 ⌘⇧E 挂坠\" with title "
          u"\"可圈 工作中台\"' 2>/dev/null || true\n"_ustr;
    const OUString nBody
        = u"#!/bin/bash\n"
          u"open -a \"可圈office\" 2>/dev/null || open -a \"可圈办公\" 2>/dev/null || true\n"
          u"osascript -e 'display notification \"左侧「记事本」· 材料库 · 发给 AI\" with title "
          u"\"可圈 本地记事本\"' 2>/dev/null || true\n"_ustr;
    const OUString pBody
        = u"#!/bin/bash\n"
          u"open -a \"可圈office\" 2>/dev/null || open -a \"可圈办公\" 2>/dev/null || true\n"
          u"osascript -e 'display notification \"应用内按 ⌘⇧E 打开效率挂坠\" with title "
          u"\"可圈 效率挂坠\"' 2>/dev/null || true\n"_ustr;
    auto writeCmd = [](const OUString& path, const OUString& body) {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
            return false;
        osl::File f(url);
        auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
        if (e != osl::FileBase::E_None)
            e = f.open(osl_File_OpenFlag_Write);
        if (e != osl::FileBase::E_None)
            return false;
        f.setSize(0);
        const OString utf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);
        sal_uInt64 n = 0;
        f.write(utf8.getStr(), utf8.getLength(), n);
        f.close();
        // best-effort chmod +x via system
        const OString cmd = "chmod +x " + OUStringToOString(path, RTL_TEXTENCODING_UTF8);
        std::system(cmd.getStr());
        return true;
    };
    const bool ok = writeCmd(wPath, wBody) && writeCmd(nPath, nBody) && writeCmd(pPath, pBody);
    if (pMessage)
        *pMessage = ok ? (u"已创建桌面快捷方式：\n"_ustr + wPath + u"\n"_ustr + nPath + u"\n"_ustr
                          + pPath)
                       : u"创建桌面快捷方式失败"_ustr;
    return ok;
#else
    // Linux .desktop stubs
    const OUString wPath = desktop + u"/kqoffice-workbench.desktop"_ustr;
    const OUString body
        = u"[Desktop Entry]\nType=Application\nName=可圈 工作中台\n"
          u"Comment=左侧「工作中台」· Ctrl+Shift+E 挂坠\n"
          u"Exec=soffice\nTerminal=false\nCategories=Office;\n"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(wPath, url) != osl::FileBase::E_None)
    {
        if (pMessage)
            *pMessage = u"路径无效"_ustr;
        return false;
    }
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
    {
        if (pMessage)
            *pMessage = u"写入失败"_ustr;
        return false;
    }
    f.setSize(0);
    const OString utf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
    if (pMessage)
        *pMessage = u"已创建："_ustr + wPath;
    return true;
#endif
}

OUString WorkTelemetryStore::maybeOnboardingTip(bool bDismiss)
{
    const OUString flag = rootDir() + u"/onboarding-seen"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(flag, url) != osl::FileBase::E_None)
        return OUString();

    if (!bDismiss)
    {
        osl::DirectoryItem item;
        if (osl::DirectoryItem::get(url, item) == osl::FileBase::E_None)
            return OUString(); // already seen
        return u"欢迎使用工作中台：⌘⇧E 挂坠 · 看图表生成 HTML · 报告夹本地归档 · "
               u"可点「桌面图标」创建快捷方式（仅一次提示）"_ustr;
    }

    ensureDirSys(rootDir());
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e == osl::FileBase::E_None)
    {
        f.setSize(0);
        const OString done = "seen\n"_ostr;
        sal_uInt64 n = 0;
        f.write(done.getStr(), done.getLength(), n);
        f.close();
    }
    return OUString();
}

} // namespace kqoffice::ai::workbench

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
