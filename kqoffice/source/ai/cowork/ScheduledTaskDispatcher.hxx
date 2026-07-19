/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (local scheduled task dispatcher).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Due-task dispatch: scan ScheduledTaskStore::tick, inject prompt into
 * pending-prompt-inject for AIChatPanel to consume, markRun, optional OS notify.
 *
 * Inject path:
 *   1. env KQOFFICE_AI_PENDING_PROMPT_INJECT (tests / override)
 *   2. ~/.config/kqoffice/pending-prompt-inject
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_SCHEDULEDTASKDISPATCHER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_SCHEDULEDTASKDISPATCHER_HXX

#include "ScheduledTask.hxx"
#include "TaskOsNotificationBridge.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <memory>

namespace kqoffice::ai::cowork
{

/// Summary of one processDue / dispatchNow call.
struct SAL_DLLPUBLIC_EXPORT ScheduledDispatchResult
{
    sal_Int32 dueCount = 0;
    sal_Int32 dispatched = 0;
    sal_Int32 failed = 0;
    OUString lastMessageZh;
};

/// Process due (or force-run) local scheduled tasks.
///
/// For each due task:
///   1) load task; skip if !enabled
///   2) write inject text to pending-prompt-inject
///   3) optionally post OS notification via TaskOsNotificationSink
///   4) markRun(id, true/false, resultZh)
class SAL_DLLPUBLIC_EXPORT ScheduledTaskDispatcher
{
public:
    /// Uses default ScheduledTaskStore root (env / XDG).
    ScheduledTaskDispatcher();
    /// Own store rooted at system path (tests / explicit root).
    explicit ScheduledTaskDispatcher(const OUString& storeRootDir);
    /// Non-owning store (caller keeps store alive for dispatcher lifetime).
    explicit ScheduledTaskDispatcher(ScheduledTaskStore& store);

    /// Override inject file path (system path). Empty → resolveInjectPath().
    void setInjectPath(const OUString& systemPath);
    OUString injectPath() const;

    /// Optional non-owning OS notification sink (may be null).
    void setOsNotificationSink(TaskOsNotificationSink* sink);
    TaskOsNotificationSink* osNotificationSink() const { return m_pOsSink; }

    /// Process all due tasks at nowMs. nowMs <= 0 → current wall time.
    ScheduledDispatchResult processDue(sal_Int64 nowMs = 0);

    /// Force-run one task by id without requiring due (enabled check still applies
    /// unless forceEvenIfDisabled). nowMs <= 0 → current wall time.
    ScheduledDispatchResult dispatchNow(const OUString& id, sal_Int64 nowMs = 0,
                                        bool forceEvenIfDisabled = false);

    /// Build user-facing inject body: 【定时任务】title\n + promptOrScenarioId.
    static OUString buildInjectText(const ScheduledTask& task);

    /// Resolve pending-prompt-inject path (env or ~/.config/kqoffice/...).
    static OUString resolveInjectPath();

    /// Write UTF-8 text to inject path (creates parent dir). Returns false on I/O error.
    static bool writeInjectFile(const OUString& systemPath, const OUString& text);

    /// Current epoch milliseconds (UTC-ish wall via osl_getSystemTime).
    static sal_Int64 nowEpochMs();

    ScheduledTaskStore& store() { return *m_pStore; }
    const ScheduledTaskStore& store() const { return *m_pStore; }

private:
    bool dispatchOne(const ScheduledTask& task, sal_Int64 nowMs,
                     ScheduledDispatchResult& result);
    void maybeNotify(const ScheduledTask& task, bool success, const OUString& messageZh);

    std::unique_ptr<ScheduledTaskStore> m_ownedStore;
    ScheduledTaskStore* m_pStore = nullptr;
    OUString m_injectPathOverride;
    TaskOsNotificationSink* m_pOsSink = nullptr;
};

/// Headless convenience: processDue with default store + no OS sink.
/// Intended for UI/timer hooks (AIChatPanel 60s tick, refresh, run-now).
SAL_DLLPUBLIC_EXPORT ScheduledDispatchResult processDueScheduledTasks(sal_Int64 nowMs = 0);

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
