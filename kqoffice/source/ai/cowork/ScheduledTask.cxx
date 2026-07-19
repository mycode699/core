/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (local scheduled tasks skeleton).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ScheduledTask.hxx"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <osl/file.hxx>
#include <osl/mutex.hxx>
#include <osl/security.hxx>
#include <osl/time.h>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

namespace kqoffice::ai::cowork
{
namespace
{

OUString envVar(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return OUString();
    return OUString(v, std::strlen(v), RTL_TEXTENCODING_UTF8);
}

OUString toFileUrl(const OUString& systemOrUrl)
{
    if (systemOrUrl.isEmpty())
        return systemOrUrl;
    if (systemOrUrl.startsWith("file://"))
        return systemOrUrl;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemOrUrl, url) == osl::FileBase::E_None
        && !url.isEmpty())
        return url;
    return systemOrUrl;
}

bool ensureDirSys(const OUString& rSys)
{
    const OUString url = toFileUrl(rSys);
    if (url.isEmpty())
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

bool writeFile(const OUString& rSysPath, const OUString& rContent)
{
    const OUString url = toFileUrl(rSysPath);
    if (url.isEmpty())
        return false;
    // Atomic-ish replace: remove then create.
    osl::File::remove(url);
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
    const OUString url = toFileUrl(rSysPath);
    if (url.isEmpty())
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
    if (sz > 1024 * 1024)
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

void appendEscaped(OUStringBuffer& buf, const OUString& s)
{
    buf.append('"');
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        switch (c)
        {
            case '"':
                buf.append("\\\"");
                break;
            case '\\':
                buf.append("\\\\");
                break;
            case '\n':
                buf.append("\\n");
                break;
            case '\r':
                buf.append("\\r");
                break;
            case '\t':
                buf.append("\\t");
                break;
            default:
                if (c < 0x20)
                {
                    char tmp[8];
                    std::snprintf(tmp, sizeof(tmp), "\\u%04x", static_cast<unsigned>(c));
                    buf.appendAscii(tmp);
                }
                else
                {
                    buf.append(c);
                }
        }
    }
    buf.append('"');
}

/// Find value start after `"key":` (tolerant of whitespace).
sal_Int32 findJsonKey(const OUString& json, std::u16string_view key)
{
    const OUString needle = u"\""_ustr + OUString(key) + u"\""_ustr;
    sal_Int32 idx = json.indexOf(needle);
    if (idx < 0)
        return -1;
    idx += needle.getLength();
    while (idx < json.getLength()
           && (json[idx] == ' ' || json[idx] == '\t' || json[idx] == '\n'
               || json[idx] == '\r'))
        ++idx;
    if (idx >= json.getLength() || json[idx] != ':')
        return -1;
    ++idx;
    while (idx < json.getLength()
           && (json[idx] == ' ' || json[idx] == '\t' || json[idx] == '\n'
               || json[idx] == '\r'))
        ++idx;
    return idx;
}

OUString extractString(const OUString& json, std::u16string_view key)
{
    sal_Int32 idx = findJsonKey(json, key);
    if (idx < 0 || idx >= json.getLength() || json[idx] != '"')
        return OUString();
    ++idx;
    OUStringBuffer out;
    for (sal_Int32 i = idx; i < json.getLength(); ++i)
    {
        if (json[i] == u'\\' && i + 1 < json.getLength())
        {
            ++i;
            switch (json[i])
            {
                case u'n':
                    out.append(u'\n');
                    break;
                case u'r':
                    out.append(u'\r');
                    break;
                case u't':
                    out.append(u'\t');
                    break;
                case u'"':
                    out.append(u'"');
                    break;
                case u'\\':
                    out.append(u'\\');
                    break;
                default:
                    out.append(json[i]);
                    break;
            }
            continue;
        }
        if (json[i] == u'"')
            break;
        out.append(json[i]);
    }
    return out.makeStringAndClear();
}

sal_Int64 extractInt64(const OUString& json, std::u16string_view key, sal_Int64 def = 0)
{
    sal_Int32 idx = findJsonKey(json, key);
    if (idx < 0)
        return def;
    return json.copy(idx).toInt64();
}

bool extractBool(const OUString& json, std::u16string_view key, bool def = false)
{
    sal_Int32 idx = findJsonKey(json, key);
    if (idx < 0)
        return def;
    if (json.match(u"true"_ustr, idx))
        return true;
    if (json.match(u"false"_ustr, idx))
        return false;
    return def;
}

OUString kindToToken(ScheduleKind kind)
{
    switch (kind)
    {
        case ScheduleKind::Once:
            return u"once"_ustr;
        case ScheduleKind::IntervalMinutes:
            return u"interval_minutes"_ustr;
        case ScheduleKind::DailyAt:
            return u"daily_at"_ustr;
    }
    return u"once"_ustr;
}

ScheduleKind kindFromToken(const OUString& token)
{
    if (token == u"interval_minutes" || token == u"interval")
        return ScheduleKind::IntervalMinutes;
    if (token == u"daily_at" || token == u"daily")
        return ScheduleKind::DailyAt;
    return ScheduleKind::Once;
}

/// Next local wall-clock occurrence of hour:minute at or after nowMs.
sal_Int64 nextDailyAtMs(sal_Int32 hour, sal_Int32 minute, sal_Int64 nowMs)
{
    if (hour < 0)
        hour = 0;
    if (hour > 23)
        hour = 23;
    if (minute < 0)
        minute = 0;
    if (minute > 59)
        minute = 59;

    const std::time_t nowSec = static_cast<std::time_t>(nowMs / 1000);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &nowSec);
#else
    localtime_r(&nowSec, &local);
#endif
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = 0;
    std::time_t cand = std::mktime(&local);
    if (cand == static_cast<std::time_t>(-1))
        return 0;

    sal_Int64 candMs = static_cast<sal_Int64>(cand) * 1000;
    // If today's slot is already past (or equal and we want "next" after now),
    // advance one calendar day. Use < so an exact match is still due today.
    if (candMs < nowMs)
    {
        local.tm_mday += 1;
        cand = std::mktime(&local);
        if (cand == static_cast<std::time_t>(-1))
            return 0;
        candMs = static_cast<sal_Int64>(cand) * 1000;
    }
    return candMs;
}

bool isSafeId(const OUString& id)
{
    if (id.isEmpty() || id.getLength() > 128)
        return false;
    for (sal_Int32 i = 0; i < id.getLength(); ++i)
    {
        const sal_Unicode c = id[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                        || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok)
            return false;
    }
    return true;
}

} // namespace

// ── Root resolution ─────────────────────────────────────────────────────

OUString ScheduledTaskStore::resolveRootDir()
{
    const OUString overrideDir = envVar("KQOFFICE_AI_SCHEDULED_TASKS_DIR");
    if (!overrideDir.isEmpty())
        return overrideDir;

    OUString home;
    if (osl::Security().getHomeDir(home) && !home.isEmpty())
    {
        // getHomeDir may return a file URL on some platforms.
        if (home.startsWith("file://"))
        {
            OUString sys;
            if (osl::FileBase::getSystemPathFromFileURL(home, sys) == osl::FileBase::E_None
                && !sys.isEmpty())
                home = sys;
        }
        return home + u"/.config/kqoffice/scheduled-tasks"_ustr;
    }

    const char* homeEnv = std::getenv("HOME");
    if (homeEnv && *homeEnv)
        return OUString::fromUtf8(homeEnv) + u"/.config/kqoffice/scheduled-tasks"_ustr;

    return u"/tmp/kqoffice-scheduled-tasks"_ustr;
}

OUString ScheduledTaskStore::newId()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "st-%u-%u", static_cast<unsigned>(tv.Seconds),
                  static_cast<unsigned>(tv.Nanosec % 1000000));
    return OUString::fromUtf8(buf);
}

// ── Ctors ───────────────────────────────────────────────────────────────

ScheduledTaskStore::ScheduledTaskStore()
    : m_rootDir(resolveRootDir())
{
}

ScheduledTaskStore::ScheduledTaskStore(const OUString& rootDir)
    : m_rootDir(rootDir)
{
}

OUString ScheduledTaskStore::rootDir() const { return m_rootDir; }

bool ScheduledTaskStore::ensureRoot() const
{
    return ensureDirSys(m_rootDir);
}

OUString ScheduledTaskStore::taskPath(const OUString& id) const
{
    return m_rootDir + u"/"_ustr + id + u".json"_ustr;
}

// ── Serialize / parse ───────────────────────────────────────────────────

OUString ScheduledTaskStore::serializeJson(const ScheduledTask& task)
{
    OUStringBuffer b;
    b.append(u"{\n"_ustr);
    b.append(u"  \"schema_version\": \"v1-scheduled-task\",\n"_ustr);
    b.append(u"  \"id\": "_ustr);
    appendEscaped(b, task.id);
    b.append(u",\n  \"titleZh\": "_ustr);
    appendEscaped(b, task.titleZh);
    b.append(u",\n  \"promptOrScenarioId\": "_ustr);
    appendEscaped(b, task.promptOrScenarioId);
    b.append(u",\n  \"kind\": "_ustr);
    appendEscaped(b, kindToToken(task.kind));
    b.append(u",\n  \"intervalMinutes\": "_ustr);
    b.append(OUString::number(task.intervalMinutes));
    b.append(u",\n  \"dailyHour\": "_ustr);
    b.append(OUString::number(task.dailyHour));
    b.append(u",\n  \"dailyMinute\": "_ustr);
    b.append(OUString::number(task.dailyMinute));
    b.append(u",\n  \"nextRunAtMs\": "_ustr);
    b.append(OUString::number(task.nextRunAtMs));
    b.append(u",\n  \"lastRunAtMs\": "_ustr);
    b.append(OUString::number(task.lastRunAtMs));
    b.append(u",\n  \"enabled\": "_ustr);
    b.append(task.enabled ? u"true"_ustr : u"false"_ustr);
    b.append(u",\n  \"lastResultZh\": "_ustr);
    appendEscaped(b, task.lastResultZh);
    b.append(u"\n}\n"_ustr);
    return b.makeStringAndClear();
}

bool ScheduledTaskStore::parseJson(const OUString& json, ScheduledTask& out)
{
    if (json.isEmpty())
        return false;
    ScheduledTask t;
    t.id = extractString(json, u"id");
    if (t.id.isEmpty())
        return false;
    t.titleZh = extractString(json, u"titleZh");
    t.promptOrScenarioId = extractString(json, u"promptOrScenarioId");
    t.kind = kindFromToken(extractString(json, u"kind"));
    t.intervalMinutes = static_cast<sal_Int32>(extractInt64(json, u"intervalMinutes"));
    t.dailyHour = static_cast<sal_Int32>(extractInt64(json, u"dailyHour"));
    t.dailyMinute = static_cast<sal_Int32>(extractInt64(json, u"dailyMinute"));
    t.nextRunAtMs = extractInt64(json, u"nextRunAtMs");
    t.lastRunAtMs = extractInt64(json, u"lastRunAtMs");
    t.enabled = extractBool(json, u"enabled", true);
    t.lastResultZh = extractString(json, u"lastResultZh");
    out = std::move(t);
    return true;
}

// ── Labels ──────────────────────────────────────────────────────────────

OUString ScheduledTaskStore::kindLabelZh(ScheduleKind kind)
{
    switch (kind)
    {
        case ScheduleKind::Once:
            return u"单次"_ustr;
        case ScheduleKind::IntervalMinutes:
            return u"间隔"_ustr;
        case ScheduleKind::DailyAt:
            return u"每日"_ustr;
    }
    return u"未知"_ustr;
}

OUString ScheduledTaskStore::statusLabelZh(const ScheduledTask& task, sal_Int64 nowMs)
{
    if (!task.enabled)
    {
        if (task.kind == ScheduleKind::Once && task.lastRunAtMs > 0)
            return u"已完成(单次)"_ustr;
        return u"已禁用"_ustr;
    }
    if (task.nextRunAtMs <= 0)
        return u"未排程"_ustr;
    if (task.nextRunAtMs <= nowMs)
        return u"已到期"_ustr;
    return u"待执行"_ustr;
}

// ── computeNextRun ──────────────────────────────────────────────────────

sal_Int64 ScheduledTaskStore::computeNextRun(const ScheduledTask& task, sal_Int64 nowMs)
{
    switch (task.kind)
    {
        case ScheduleKind::Once:
            // Keep a future one-shot time; otherwise no further fire.
            if (task.nextRunAtMs > nowMs)
                return task.nextRunAtMs;
            return 0;

        case ScheduleKind::IntervalMinutes:
        {
            if (task.intervalMinutes <= 0)
                return 0;
            const sal_Int64 delta
                = static_cast<sal_Int64>(task.intervalMinutes) * 60 * 1000;
            // If nextRun is still in the future, keep it; else schedule from now.
            if (task.nextRunAtMs > nowMs)
                return task.nextRunAtMs;
            return nowMs + delta;
        }

        case ScheduleKind::DailyAt:
            return nextDailyAtMs(task.dailyHour, task.dailyMinute, nowMs);
    }
    return 0;
}

// ── Persistence helpers ─────────────────────────────────────────────────

bool ScheduledTaskStore::writeTask(const ScheduledTask& task) const
{
    if (!isSafeId(task.id))
        return false;
    if (!ensureDirSys(m_rootDir))
        return false;
    return writeFile(taskPath(task.id), serializeJson(task));
}

bool ScheduledTaskStore::readTask(const OUString& id, ScheduledTask& out) const
{
    if (!isSafeId(id))
        return false;
    const OUString json = readFile(taskPath(id));
    return parseJson(json, out);
}

// ── Public API ──────────────────────────────────────────────────────────

std::vector<ScheduledTask> ScheduledTaskStore::listImpl() const
{
    std::vector<ScheduledTask> out;

    const OUString url = toFileUrl(m_rootDir);
    if (url.isEmpty())
        return out;

    osl::Directory dir(url);
    if (dir.open() != osl::FileBase::E_None)
        return out;

    osl::DirectoryItem item;
    while (dir.getNextItem(item) == osl::FileBase::E_None)
    {
        osl::FileStatus st(osl_FileStatus_Mask_FileName | osl_FileStatus_Mask_Type);
        if (item.getFileStatus(st) != osl::FileBase::E_None)
            continue;
        if (!st.isValid(osl_FileStatus_Mask_Type) || !st.isRegular())
            continue;
        if (!st.isValid(osl_FileStatus_Mask_FileName))
            continue;
        const OUString name = st.getFileName();
        if (!name.endsWith(u".json"_ustr))
            continue;
        const OUString id = name.copy(0, name.getLength() - 5);
        ScheduledTask t;
        if (readTask(id, t))
            out.push_back(std::move(t));
    }
    dir.close();
    return out;
}

std::vector<ScheduledTask> ScheduledTaskStore::list() const
{
    osl::MutexGuard guard(m_mutex);
    return listImpl();
}

bool ScheduledTaskStore::upsert(ScheduledTask& task)
{
    osl::MutexGuard guard(m_mutex);
    if (task.id.isEmpty())
        task.id = newId();
    if (!isSafeId(task.id))
        return false;

    // If enabled and nextRun unset, schedule from "now".
    if (task.enabled && task.nextRunAtMs <= 0)
    {
        TimeValue tv{};
        osl_getSystemTime(&tv);
        const sal_Int64 nowMs = static_cast<sal_Int64>(tv.Seconds) * 1000
                                + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
        // For Once with no next, leave 0 (caller must set nextRunAtMs).
        // For Interval/Daily, compute from now.
        if (task.kind != ScheduleKind::Once)
            task.nextRunAtMs = computeNextRun(task, nowMs);
    }

    return writeTask(task);
}

bool ScheduledTaskStore::remove(const OUString& id)
{
    osl::MutexGuard guard(m_mutex);
    if (!isSafeId(id))
        return false;
    const OUString url = toFileUrl(taskPath(id));
    const auto rc = osl::File::remove(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_NOENT;
}

bool ScheduledTaskStore::setEnabled(const OUString& id, bool enabled)
{
    osl::MutexGuard guard(m_mutex);
    ScheduledTask t;
    if (!readTask(id, t))
        return false;
    t.enabled = enabled;
    if (enabled && t.nextRunAtMs <= 0 && t.kind != ScheduleKind::Once)
    {
        TimeValue tv{};
        osl_getSystemTime(&tv);
        const sal_Int64 nowMs = static_cast<sal_Int64>(tv.Seconds) * 1000
                                + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
        t.nextRunAtMs = computeNextRun(t, nowMs);
    }
    return writeTask(t);
}

bool ScheduledTaskStore::get(const OUString& id, ScheduledTask& out) const
{
    osl::MutexGuard guard(m_mutex);
    return readTask(id, out);
}

std::vector<ScheduledTask> ScheduledTaskStore::dueTasks(sal_Int64 nowMs) const
{
    osl::MutexGuard guard(m_mutex);
    std::vector<ScheduledTask> due;
    for (auto& t : listImpl())
    {
        if (!t.enabled)
            continue;
        if (t.nextRunAtMs > 0 && t.nextRunAtMs <= nowMs)
            due.push_back(std::move(t));
    }
    return due;
}

bool ScheduledTaskStore::markRun(const OUString& id, bool success, const OUString& resultZh,
                                 sal_Int64 nowMs)
{
    osl::MutexGuard guard(m_mutex);
    ScheduledTask t;
    if (!readTask(id, t))
        return false;

    t.lastRunAtMs = nowMs;
    if (!resultZh.isEmpty())
        t.lastResultZh = resultZh;
    else
        t.lastResultZh = success ? u"成功"_ustr : u"失败"_ustr;

    switch (t.kind)
    {
        case ScheduleKind::Once:
            // One-shot done — disable so it no longer appears in dueTasks.
            t.enabled = false;
            t.nextRunAtMs = 0;
            break;
        case ScheduleKind::IntervalMinutes:
        {
            // Advance from now (after this run).
            ScheduledTask probe = t;
            probe.nextRunAtMs = 0; // force recompute from nowMs
            t.nextRunAtMs = computeNextRun(probe, nowMs);
            break;
        }
        case ScheduleKind::DailyAt:
        {
            // Next calendar slot strictly after nowMs.
            t.nextRunAtMs = nextDailyAtMs(t.dailyHour, t.dailyMinute, nowMs + 1);
            break;
        }
    }

    // NOTE: No AI / CoworkUiBridge execution here — ledger only.
    return writeTask(t);
}

std::vector<OUString> ScheduledTaskStore::tick(sal_Int64 nowMs) const
{
    // Headless due-id scan for a future timer.
    //
    // Future integration point (NOT implemented in this skeleton):
    //   for (const OUString& id : store.tick(nowMs)) {
    //       ScheduledTask t;
    //       if (!store.get(id, t)) continue;
    //       // Hook: hand t.promptOrScenarioId to CoworkUiBridge / TaskQueue
    //       // e.g. coworkRunNewTask(t.promptOrScenarioId, ...) or
    //       // DocumentAIScenarioStore::queuePendingRun(t.promptOrScenarioId)
    //       store.markRun(id, ok, resultZh, nowMs);
    //   }
    std::vector<OUString> ids;
    for (const auto& t : dueTasks(nowMs))
        ids.push_back(t.id);
    return ids;
}

// Note: tick() calls dueTasks() (each takes m_mutex separately) — no nested lock.

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
