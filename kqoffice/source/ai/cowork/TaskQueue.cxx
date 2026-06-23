/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork Task Queue).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskQueue.hxx"

#include "TaskStateMachine.hxx"

#include <algorithm>

namespace kqoffice::ai::cowork
{
namespace
{
OUString monthDirFor(const AsyncTaskEnvelope& env)
{
    if (env.createdAt.getLength() >= 7 && env.createdAt[4] == '-')
        return env.createdAt.copy(0, 7);
    return OUString();
}

void appendEvidence(AsyncTaskEnvelope& env, const OUString& evidenceId)
{
    if (!evidenceId.isEmpty())
        env.evidenceIds.push_back(evidenceId);
}

OUString defaultCancelReason(TaskState state)
{
    if (state == TaskState::Pending)
        return u"user-cancelled-before-dispatch"_ustr;
    if (state == TaskState::Running)
        return u"user-cancelled-mid-run"_ustr;
    if (state == TaskState::AwaitingReview)
        return u"user-cancelled"_ustr;
    return u"user-cancelled"_ustr;
}
}

TaskQueue::TaskQueue(TaskStore& store, sal_Int32 maxParallelism)
    : m_store(store)
    , m_maxParallelism(maxParallelism > 0 ? maxParallelism : 1)
{
}

bool TaskQueue::enqueue(AsyncTaskEnvelope env)
{
    if (env.taskId.isEmpty())
        return false;

    OUString monthDir = monthDirFor(env);
    if (!monthDir.isEmpty())
    {
        auto pending = m_store.listByState(monthDir, TaskState::Pending);
        if (pending.size() >= 100)
            return false;
    }

    env.state = TaskState::Pending;
    env.resultPlanId.clear();
    env.failureReason.clear();
    appendEvidence(env, u"enqueued"_ustr);
    return m_store.write(env);
}

sal_Int32 TaskQueue::runningCount(const OUString& monthDir)
{
    return static_cast<sal_Int32>(m_store.listByState(monthDir, TaskState::Running).size());
}

bool TaskQueue::dispatchNext(const OUString& monthDir, AsyncTaskEnvelope& out)
{
    if (runningCount(monthDir) >= m_maxParallelism)
        return false;

    auto pending = m_store.listByState(monthDir, TaskState::Pending);
    if (pending.empty())
        return false;

    // Read each pending task to sort by priority (HIGH > NORMAL > LOW),
    // then by task ID for stable ordering.
    std::vector<AsyncTaskEnvelope> pendingEnvelopes;
    for (const auto& id : pending)
    {
        AsyncTaskEnvelope env;
        if (m_store.read(monthDir, id, env))
            pendingEnvelopes.push_back(std::move(env));
    }

    std::sort(pendingEnvelopes.begin(), pendingEnvelopes.end(),
              [](const AsyncTaskEnvelope& a, const AsyncTaskEnvelope& b) {
                  if (a.priority != b.priority)
                      return static_cast<int>(a.priority) > static_cast<int>(b.priority);
                  return a.taskId.compareTo(b.taskId) < 0;
              });

    return transition(monthDir, pendingEnvelopes.front().taskId, TaskState::Running,
                      u"dispatched"_ustr, OUString(), &out);
}

bool TaskQueue::markAwaitingReview(const OUString& monthDir,
                                   const OUString& taskId,
                                   const OUString& resultPlanId,
                                   const OUString& evidenceId,
                                   AsyncTaskEnvelope* out)
{
    return transition(monthDir, taskId, TaskState::AwaitingReview,
                      evidenceId, resultPlanId, out);
}

bool TaskQueue::markApplied(const OUString& monthDir,
                            const OUString& taskId,
                            AsyncTaskEnvelope* out)
{
    return transition(monthDir, taskId, TaskState::Applied,
                      u"user-accepted"_ustr, OUString(), out);
}

bool TaskQueue::cancel(const OUString& monthDir,
                       const OUString& taskId,
                       AsyncTaskEnvelope* out)
{
    AsyncTaskEnvelope env;
    if (!m_store.read(monthDir, taskId, env))
        return false;
    return transition(monthDir, taskId, TaskState::Cancelled,
                      defaultCancelReason(env.state), OUString(), out);
}

bool TaskQueue::cancel(const OUString& monthDir,
                       const OUString& taskId,
                       const OUString& reason,
                       AsyncTaskEnvelope* out)
{
    OUString transitionReason = reason.isEmpty() ? u"user-cancelled"_ustr : reason;
    return transition(monthDir, taskId, TaskState::Cancelled,
                      transitionReason, OUString(), out);
}

bool TaskQueue::markFailed(const OUString& monthDir,
                           const OUString& taskId,
                           const OUString& reason,
                           AsyncTaskEnvelope* out)
{
    return transition(monthDir, taskId, TaskState::Failed,
                      reason, OUString(), out);
}

bool TaskQueue::refineFailed(const OUString& monthDir,
                             const OUString& failedTaskId,
                             const OUString& newTaskId,
                             const OUString& refinedPrompt,
                             AsyncTaskEnvelope& out)
{
    AsyncTaskEnvelope failed;
    if (!m_store.read(monthDir, failedTaskId, failed))
        return false;
    if (failed.state != TaskState::Failed)
        return false;
    if (newTaskId.isEmpty())
        return false;

    AsyncTaskEnvelope refined = failed;
    refined.taskId = newTaskId;
    refined.state = TaskState::Pending;
    refined.userPrompt = refinedPrompt;
    refined.resultPlanId.clear();
    refined.failureReason.clear();
    refined.evidenceIds.push_back(u"refined-resubmit"_ustr);
    refined.evidenceIds.push_back(u"refined-from-"_ustr + failedTaskId);

    if (!m_store.write(refined))
        return false;

    out = refined;
    return true;
}

bool TaskQueue::transition(const OUString& monthDir,
                           const OUString& taskId,
                           TaskState to,
                           const OUString& reason,
                           const OUString& resultPlanId,
                           AsyncTaskEnvelope* out)
{
    AsyncTaskEnvelope env;
    if (!m_store.read(monthDir, taskId, env))
        return false;

    if (!canTransition(env.state, to))
        return false;

    env.state = to;
    if (!resultPlanId.isEmpty())
        env.resultPlanId = resultPlanId;

    if (to == TaskState::Failed)
        env.failureReason = reason;
    else if (to != TaskState::Failed)
        env.failureReason.clear();

    appendEvidence(env, reason);

    if (!m_store.write(env))
        return false;

    if (out)
        *out = env;
    return true;
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
