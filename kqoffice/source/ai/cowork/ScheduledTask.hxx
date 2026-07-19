/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (local scheduled tasks skeleton).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * DuMate-style 定时任务 — local-only schedule ledger + due detection.
 * No cloud, no AI execution in this skeleton (BatchJob-style first step).
 *
 * Persist root:
 *   1. env KQOFFICE_AI_SCHEDULED_TASKS_DIR (tests / override)
 *   2. ~/.config/kqoffice/scheduled-tasks/
 *
 * One JSON file per task: <root>/<id>.json
 *
 * Future hook: when a task is due, CoworkUiBridge (or TaskQueue) should
 * enqueue promptOrScenarioId — see ScheduledTaskStore::tick() comment.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_SCHEDULEDTASK_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_SCHEDULEDTASK_HXX

#include <osl/mutex.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::cowork
{

/// How the task is scheduled.
enum class ScheduleKind : sal_uInt8
{
    Once = 0, ///< fire once at nextRunAtMs, then disable
    IntervalMinutes, ///< every intervalMinutes after last/next run
    DailyAt, ///< every day at dailyHour:dailyMinute (local wall clock)
};

/// Local scheduled-task ledger entry (no runtime worker state).
struct ScheduledTask
{
    OUString id;
    OUString titleZh;
    /// Prompt text or DocumentAI scenario id — what to run later (not executed here).
    OUString promptOrScenarioId;
    ScheduleKind kind = ScheduleKind::Once;
    sal_Int32 intervalMinutes = 0; ///< for IntervalMinutes; must be > 0 when used
    sal_Int32 dailyHour = 0; ///< 0–23 for DailyAt
    sal_Int32 dailyMinute = 0; ///< 0–59 for DailyAt
    sal_Int64 nextRunAtMs = 0; ///< UTC epoch ms; 0 means unset / no further run
    sal_Int64 lastRunAtMs = 0;
    bool enabled = true;
    /// Last outcome Chinese message (success / skip / failed).
    OUString lastResultZh;
};

/// Local-only schedule store: CRUD + next-run math + due detection.
///
/// This is intentionally a ledger only. Actual AI / cowork execution is out of
/// scope — callers (future timer + CoworkUiBridge) own dispatch.
class SAL_DLLPUBLIC_EXPORT ScheduledTaskStore
{
public:
    /// Resolve default root (env override or ~/.config/kqoffice/scheduled-tasks).
    ScheduledTaskStore();
    /// Explicit root directory (system path). Creates dir on first write.
    explicit ScheduledTaskStore(const OUString& rootDir);

    /// Root directory (system path).
    OUString rootDir() const;

    /// Ensure root directory exists. Returns false on failure.
    bool ensureRoot() const;

    /// All tasks on disk (unsorted). Empty vector if root missing / unreadable.
    std::vector<ScheduledTask> list() const;

    /// Insert or replace by id. Generates id if empty. Recomputes nextRunAtMs
    /// when it is 0 and the task is enabled. Returns false if id empty after
    /// generation failure or write fails.
    bool upsert(ScheduledTask& task);

    /// Delete task file. Returns true if removed or already absent.
    bool remove(const OUString& id);

    /// Toggle enabled and persist. Returns false if id not found.
    bool setEnabled(const OUString& id, bool enabled);

    /// Load one task by id. Returns false if missing / unreadable.
    bool get(const OUString& id, ScheduledTask& out) const;

    /// Compute next fire time (epoch ms) from kind + fields + nowMs.
    /// For Once: preserves existing nextRunAtMs if still in the future; else 0.
    /// For IntervalMinutes: nowMs + intervalMinutes * 60_000 (interval must be > 0).
    /// For DailyAt: next local wall-clock occurrence of dailyHour:dailyMinute
    ///   (if that time today is still ahead of nowMs, use today; else tomorrow).
    static sal_Int64 computeNextRun(const ScheduledTask& task, sal_Int64 nowMs);

    /// Enabled tasks with nextRunAtMs > 0 and nextRunAtMs <= nowMs.
    std::vector<ScheduledTask> dueTasks(sal_Int64 nowMs) const;

    /// After a run attempt: set lastRunAtMs=nowMs, lastResultZh, recompute
    /// nextRunAtMs (Once → disable + next=0; Interval/Daily → computeNextRun).
    /// Does NOT invoke AI. Returns false if id not found or persist fails.
    bool markRun(const OUString& id, bool success, const OUString& resultZh,
                 sal_Int64 nowMs);

    /// Headless due-id scan for a future timer loop. Returns ids of due tasks.
    /// Does not execute, mark, or mutate. Callers should:
    ///   1) for each id, load task and hand promptOrScenarioId to CoworkUiBridge
    ///      (or TaskQueue) — hook point reserved, not implemented here;
    ///   2) call markRun(id, ok, resultZh, nowMs).
    std::vector<OUString> tick(sal_Int64 nowMs) const;

    /// Chinese label for schedule kind.
    static OUString kindLabelZh(ScheduleKind kind);

    /// Chinese status helper from enabled + next/last vs nowMs.
    /// e.g. 已禁用 / 待执行 / 已到期 / 已完成(单次) / 未排程
    static OUString statusLabelZh(const ScheduledTask& task, sal_Int64 nowMs);

    /// Resolve root path (env KQOFFICE_AI_SCHEDULED_TASKS_DIR or XDG config).
    static OUString resolveRootDir();

    /// New unique id (st-seconds-nanosec).
    static OUString newId();

    /// Serialize / parse for tests and round-trip.
    static OUString serializeJson(const ScheduledTask& task);
    static bool parseJson(const OUString& json, ScheduledTask& out);

private:
    OUString taskPath(const OUString& id) const;
    bool writeTask(const ScheduledTask& task) const;
    bool readTask(const OUString& id, ScheduledTask& out) const;
    /// Non-locking list — caller must hold m_mutex (or accept races in tests).
    std::vector<ScheduledTask> listImpl() const;

    OUString m_rootDir;
    mutable osl::Mutex m_mutex;
};

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
