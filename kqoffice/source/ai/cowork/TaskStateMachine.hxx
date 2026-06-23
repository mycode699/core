/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5 Day-0: Async Cowork Task State Machine).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKSTATEMACHINE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKSTATEMACHINE_HXX

#include "AsyncTask.hxx"

#include <vector>

namespace kqoffice::ai::cowork
{
/// Single point of truth for legal `TaskState` transitions. Mirrors W5
/// spec §"Token lock" table exactly:
///
///   pending          -> running, cancelled
///   running          -> awaiting-review, failed, cancelled
///   awaiting-review  -> running (refine), applied, cancelled
///   applied          -> (terminal)
///   failed           -> (terminal)
///   cancelled        -> (terminal)
///
/// Any other transition returns false. Terminal states never permit
/// further transitions, including self-loops.
inline bool canTransition(TaskState from, TaskState to)
{
    if (from == to)
        return false;

    switch (from)
    {
        case TaskState::Pending:
            return to == TaskState::Running
                || to == TaskState::Cancelled;
        case TaskState::Running:
            return to == TaskState::AwaitingReview
                || to == TaskState::Failed
                || to == TaskState::Cancelled;
        case TaskState::AwaitingReview:
            return to == TaskState::Running
                || to == TaskState::Applied
                || to == TaskState::Cancelled;
        case TaskState::Applied:
        case TaskState::Failed:
        case TaskState::Cancelled:
            return false;
    }
    return false;
}

/// True when the state has no successor (applied / failed / cancelled).
inline bool isTerminalTaskState(TaskState s)
{
    return s == TaskState::Applied
        || s == TaskState::Failed
        || s == TaskState::Cancelled;
}

/// All legal successor states for `from`. Empty for terminals.
/// Order matches W5 spec §"Token lock" table left-to-right.
inline std::vector<TaskState> legalTransitions(TaskState from)
{
    switch (from)
    {
        case TaskState::Pending:
            return { TaskState::Running, TaskState::Cancelled };
        case TaskState::Running:
            return { TaskState::AwaitingReview,
                     TaskState::Failed,
                     TaskState::Cancelled };
        case TaskState::AwaitingReview:
            return { TaskState::Running,
                     TaskState::Applied,
                     TaskState::Cancelled };
        case TaskState::Applied:
        case TaskState::Failed:
        case TaskState::Cancelled:
            return {};
    }
    return {};
}

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
