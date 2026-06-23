/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5 Day-0: Async Cowork Task).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_ASYNCTASK_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_ASYNCTASK_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::cowork
{
/// Scenario kinds for Day-0 async cowork. Order locked by W5 spec
/// §"Token lock" — schema mirror at docs/schemas/async-task.schema.json.
/// Day-0 scenarios: weekly-report, outline-to-slides, contract-review,
/// data-cleanup.
enum class TaskKind
{
    WeeklyReport,
    OutlineToSlides,
    ContractReview,
    DataCleanup,
};

/// Task-level state machine. Order locked by W5 spec §"Token lock"
/// (matches §"状态机" diagram). The post-running review state is the
/// standard pre-apply state — never "needs-review".
enum class TaskState
{
    Pending,
    Running,
    AwaitingReview,
    Applied,
    Failed,
    Cancelled,
};

/// Per-step state. Narrower than TaskState — steps never enter
/// awaiting-review (the apply-plan that triggers awaiting-review is a
/// task-level rollup, not a per-step concern). Mirrors schema
/// steps[].state enum.
enum class TaskStepState
{
    Pending,
    Running,
    Completed,
    Failed,
};

/// Per-step record. Maps 1:1 to schema steps[] item.
struct TaskStep
{
    OUString stepId;     // schema steps[].step_id, pattern ^s[0-9]+$
    OUString title;      // schema steps[].title
    TaskStepState state = TaskStepState::Pending;
    OUString evidenceId; // schema steps[].evidence, empty when absent
};

/// Per-task envelope. Maps 1:1 to async-task.schema.json envelope
/// (schema_version=1). Day-0 fields only — additional per-kind input
/// shape lands incrementally per W5 spec §"后续".
struct AsyncTaskEnvelope
{
    OUString taskId;           // ^tk-[0-9]{8}-[0-9]{3}$
    TaskKind kind = TaskKind::WeeklyReport;
    TaskState state = TaskState::Pending;
    OUString title;
    OUString createdAt;        // ISO-8601 UTC seconds with 'Z'
    OUString updatedAt;        // ISO-8601 UTC seconds with 'Z'
    OUString serviceMode;      // "offline" | "private" | "cloud"
    OUString userPrompt;       // input.user_prompt (optional)
    std::vector<OUString> sourceDocs;
    OUString targetTemplate;
    std::vector<TaskStep> steps;
    OUString resultPlanId;     // ^ap-[0-9a-f]{16}$, empty when null
    std::vector<OUString> evidenceIds;
    OUString failureReason;    // required iff state == Failed
    sal_Int32 schemaVersion = 1;
};

/// TaskKind -> schema token. W5 spec §"Token lock" order locked.
inline OUString taskKindToken(TaskKind kind)
{
    switch (kind)
    {
        case TaskKind::WeeklyReport:    return u"weekly-report"_ustr;
        case TaskKind::OutlineToSlides: return u"outline-to-slides"_ustr;
        case TaskKind::ContractReview:  return u"contract-review"_ustr;
        case TaskKind::DataCleanup:     return u"data-cleanup"_ustr;
    }
    return OUString();
}

inline bool parseTaskKind(const OUString& token, TaskKind& out)
{
    if (token == u"weekly-report")    { out = TaskKind::WeeklyReport;    return true; }
    if (token == u"outline-to-slides"){ out = TaskKind::OutlineToSlides; return true; }
    if (token == u"contract-review")  { out = TaskKind::ContractReview;  return true; }
    if (token == u"data-cleanup")     { out = TaskKind::DataCleanup;     return true; }
    return false;
}

/// TaskState -> schema token. W5 spec §"Token lock" order locked.
inline OUString taskStateToken(TaskState state)
{
    switch (state)
    {
        case TaskState::Pending:        return u"pending"_ustr;
        case TaskState::Running:        return u"running"_ustr;
        case TaskState::AwaitingReview: return u"awaiting-review"_ustr;
        case TaskState::Applied:        return u"applied"_ustr;
        case TaskState::Failed:         return u"failed"_ustr;
        case TaskState::Cancelled:      return u"cancelled"_ustr;
    }
    return OUString();
}

inline bool parseTaskState(const OUString& token, TaskState& out)
{
    if (token == u"pending")         { out = TaskState::Pending;        return true; }
    if (token == u"running")         { out = TaskState::Running;        return true; }
    if (token == u"awaiting-review") { out = TaskState::AwaitingReview; return true; }
    if (token == u"applied")         { out = TaskState::Applied;        return true; }
    if (token == u"failed")          { out = TaskState::Failed;         return true; }
    if (token == u"cancelled")       { out = TaskState::Cancelled;      return true; }
    return false;
}

/// TaskStepState <-> schema token. Step states: pending / running /
/// completed / failed (no awaiting-review at step level).
inline OUString taskStepStateToken(TaskStepState state)
{
    switch (state)
    {
        case TaskStepState::Pending:   return u"pending"_ustr;
        case TaskStepState::Running:   return u"running"_ustr;
        case TaskStepState::Completed: return u"completed"_ustr;
        case TaskStepState::Failed:    return u"failed"_ustr;
    }
    return OUString();
}

inline bool parseTaskStepState(const OUString& token, TaskStepState& out)
{
    if (token == u"pending")   { out = TaskStepState::Pending;   return true; }
    if (token == u"running")   { out = TaskStepState::Running;   return true; }
    if (token == u"completed") { out = TaskStepState::Completed; return true; }
    if (token == u"failed")    { out = TaskStepState::Failed;    return true; }
    return false;
}

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
