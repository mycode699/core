/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (local scheduled task dispatcher).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ScheduledTaskDispatcher.hxx"

#include <cstdlib>
#include <cstring>
#include <string_view>

#include <osl/file.hxx>
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

bool ensureParentDir(const OUString& rSysFile)
{
    const sal_Int32 slash = rSysFile.lastIndexOf(u'/');
    if (slash <= 0)
        return true;
    const OUString dir = rSysFile.copy(0, slash);
    const OUString url = toFileUrl(dir);
    if (url.isEmpty())
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

OUString homeConfigDir()
{
    OUString home;
    if (osl::Security().getHomeDir(home) && !home.isEmpty())
    {
        if (home.startsWith("file://"))
        {
            OUString sys;
            if (osl::FileBase::getSystemPathFromFileURL(home, sys) == osl::FileBase::E_None
                && !sys.isEmpty())
                home = sys;
        }
        return home + u"/.config/kqoffice"_ustr;
    }
    const char* homeEnv = std::getenv("HOME");
    if (homeEnv && *homeEnv)
        return OUString::fromUtf8(homeEnv) + u"/.config/kqoffice"_ustr;
    return u"/tmp/kqoffice"_ustr;
}

} // namespace

// ── Static helpers ──────────────────────────────────────────────────────

sal_Int64 ScheduledTaskDispatcher::nowEpochMs()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
           + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

OUString ScheduledTaskDispatcher::resolveInjectPath()
{
    const OUString overridePath = envVar("KQOFFICE_AI_PENDING_PROMPT_INJECT");
    if (!overridePath.isEmpty())
        return overridePath;
    return homeConfigDir() + u"/pending-prompt-inject"_ustr;
}

bool ScheduledTaskDispatcher::writeInjectFile(const OUString& systemPath, const OUString& text)
{
    if (systemPath.isEmpty() || text.isEmpty())
        return false;
    if (!ensureParentDir(systemPath))
        return false;
    const OUString url = toFileUrl(systemPath);
    if (url.isEmpty())
        return false;
    osl::File::remove(url);
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString utf8 = OUStringToOString(text, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    const bool ok = f.write(utf8.getStr(), utf8.getLength(), n) == osl::FileBase::E_None
                    && n == static_cast<sal_uInt64>(utf8.getLength());
    f.close();
    return ok;
}

OUString ScheduledTaskDispatcher::buildInjectText(const ScheduledTask& task)
{
    OUStringBuffer b;
    b.append(u"【定时任务】"_ustr);
    if (!task.titleZh.isEmpty())
        b.append(task.titleZh);
    else if (!task.id.isEmpty())
        b.append(task.id);
    else
        b.append(u"未命名"_ustr);
    b.append(u"\n"_ustr);

    // Plain prompt text. Scenario-like ids (no spaces, has hyphen) are still
    // injected as text so the AI panel / user can edit before send.
    const OUString body = task.promptOrScenarioId.trim();
    if (!body.isEmpty())
        b.append(body);
    else
        b.append(u"（无提示词）"_ustr);
    return b.makeStringAndClear();
}

// ── Ctors ───────────────────────────────────────────────────────────────

ScheduledTaskDispatcher::ScheduledTaskDispatcher()
    : m_ownedStore(std::make_unique<ScheduledTaskStore>())
    , m_pStore(m_ownedStore.get())
{
}

ScheduledTaskDispatcher::ScheduledTaskDispatcher(const OUString& storeRootDir)
    : m_ownedStore(std::make_unique<ScheduledTaskStore>(storeRootDir))
    , m_pStore(m_ownedStore.get())
{
}

ScheduledTaskDispatcher::ScheduledTaskDispatcher(ScheduledTaskStore& store)
    : m_pStore(&store)
{
}

void ScheduledTaskDispatcher::setInjectPath(const OUString& systemPath)
{
    m_injectPathOverride = systemPath;
}

OUString ScheduledTaskDispatcher::injectPath() const
{
    if (!m_injectPathOverride.isEmpty())
        return m_injectPathOverride;
    return resolveInjectPath();
}

void ScheduledTaskDispatcher::setOsNotificationSink(TaskOsNotificationSink* sink)
{
    m_pOsSink = sink;
}

// ── Dispatch ────────────────────────────────────────────────────────────

void ScheduledTaskDispatcher::maybeNotify(const ScheduledTask& task, bool success,
                                          const OUString& messageZh)
{
    if (!m_pOsSink)
        return;

    TaskOsNotificationRequest request;
    request.valid = true;
    request.actionToken = taskOsNotificationPostedToken();
    request.title = task.titleZh.isEmpty() ? u"可圈定时任务"_ustr : task.titleZh;
    if (success)
        request.body = messageZh.isEmpty() ? u"已到期并注入可圈 AI 待办"_ustr : messageZh;
    else
        request.body = messageZh.isEmpty() ? u"定时任务派发失败"_ustr : messageZh;
    // No reviewRequest — schedule inject is not a cowork review payload.
    try
    {
        m_pOsSink->postNotification(request);
    }
    catch (...)
    {
        SAL_WARN("kqoffice.cowork", "ScheduledTaskDispatcher: OS notification threw");
    }
}

bool ScheduledTaskDispatcher::dispatchOne(const ScheduledTask& task, sal_Int64 nowMs,
                                          ScheduledDispatchResult& result)
{
    const OUString injectText = buildInjectText(task);
    const OUString path = injectPath();
    const bool wrote = writeInjectFile(path, injectText);

    const OUString okMsg = u"已到期并注入可圈 AI 待办"_ustr;
    const OUString failMsg = u"注入可圈 AI 待办失败"_ustr;

    if (wrote)
    {
        if (!m_pStore->markRun(task.id, true, okMsg, nowMs))
        {
            ++result.failed;
            result.lastMessageZh = u"注入成功但 markRun 失败: "_ustr + task.id;
            maybeNotify(task, false, result.lastMessageZh);
            return false;
        }
        ++result.dispatched;
        result.lastMessageZh = okMsg + u" ("_ustr + task.id + u")"_ustr;
        maybeNotify(task, true, okMsg);
        SAL_INFO("kqoffice.cowork",
                 "ScheduledTaskDispatcher dispatched id="
                     << task.id << " inject=" << path);
        return true;
    }

    // Persist failure so status UI can show lastResultZh; keep schedule for retry
    // on Interval/Daily (Once stays due if we don't mark — prefer mark failed once).
    m_pStore->markRun(task.id, false, failMsg, nowMs);
    ++result.failed;
    result.lastMessageZh = failMsg + u" ("_ustr + task.id + u")"_ustr;
    maybeNotify(task, false, failMsg);
    SAL_WARN("kqoffice.cowork",
             "ScheduledTaskDispatcher inject failed id=" << task.id << " path=" << path);
    return false;
}

ScheduledDispatchResult ScheduledTaskDispatcher::processDue(sal_Int64 nowMs)
{
    if (nowMs <= 0)
        nowMs = nowEpochMs();

    ScheduledDispatchResult result;
    const std::vector<OUString> ids = m_pStore->tick(nowMs);
    result.dueCount = static_cast<sal_Int32>(ids.size());

    for (const OUString& id : ids)
    {
        ScheduledTask task;
        if (!m_pStore->get(id, task))
        {
            ++result.failed;
            result.lastMessageZh = u"任务不存在: "_ustr + id;
            continue;
        }
        if (!task.enabled)
        {
            // tick should only return enabled due tasks; skip defensively.
            continue;
        }
        dispatchOne(task, nowMs, result);
    }

    if (result.dueCount == 0 && result.lastMessageZh.isEmpty())
        result.lastMessageZh = u"无到期定时任务"_ustr;

    return result;
}

ScheduledDispatchResult ScheduledTaskDispatcher::dispatchNow(const OUString& id, sal_Int64 nowMs,
                                                             bool forceEvenIfDisabled)
{
    if (nowMs <= 0)
        nowMs = nowEpochMs();

    ScheduledDispatchResult result;
    result.dueCount = 1;

    ScheduledTask task;
    if (!m_pStore->get(id, task))
    {
        result.failed = 1;
        result.lastMessageZh = u"任务不存在: "_ustr + id;
        return result;
    }
    if (!task.enabled && !forceEvenIfDisabled)
    {
        result.failed = 1;
        result.lastMessageZh = u"任务已禁用: "_ustr + id;
        return result;
    }

    dispatchOne(task, nowMs, result);
    return result;
}

ScheduledDispatchResult processDueScheduledTasks(sal_Int64 nowMs)
{
    ScheduledTaskDispatcher dispatcher;
    return dispatcher.processDue(nowMs);
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
