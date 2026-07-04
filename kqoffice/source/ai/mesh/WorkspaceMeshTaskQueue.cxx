/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of WorkspaceMeshTaskQueue.
 */

#include "WorkspaceMeshTaskQueue.hxx"

#include <algorithm>
#include <sal/log.hxx>

namespace kqoffice::ai::mesh
{

WorkspaceMeshTaskQueue::WorkspaceMeshTaskQueue() {}
WorkspaceMeshTaskQueue::~WorkspaceMeshTaskQueue() {}

// ── Enqueue ───────────────────────────────────────────────────────────────

bool WorkspaceMeshTaskQueue::enqueue(const MeshTask& task)
{
    osl::MutexGuard guard(m_aMutex);

    for (const auto& t : m_aTasks)
    {
        if (t.taskId == task.taskId)
        {
            SAL_INFO("kqoffice.ai.mesh",
                "enqueue: duplicate taskId " << task.taskId);
            return false;
        }
    }

    m_aTasks.push_back(task);
    SAL_INFO("kqoffice.ai.mesh", "enqueue: " << task.taskId
        << " priority=" << static_cast<int>(task.priority));
    return true;
}

sal_Int32 WorkspaceMeshTaskQueue::enqueueBatch(const std::vector<MeshTask>& tasks)
{
    sal_Int32 count = 0;
    for (const auto& t : tasks)
    {
        if (enqueue(t))
            count++;
    }
    return count;
}

// ── Dequeue ───────────────────────────────────────────────────────────────

MeshTask* WorkspaceMeshTaskQueue::dequeueForAgent(const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);

    // Find highest-priority ready task.
    MeshTask* best = nullptr;
    for (auto& t : m_aTasks)
    {
        if (t.state != MeshTaskState::Pending)
            continue;
        if (!isReady(t.taskId))
            continue;

        if (!best
            || static_cast<int>(t.priority) > static_cast<int>(best->priority))
        {
            best = &t;
        }
    }

    if (best)
    {
        best->assignedAgentId = agentId;
        best->state = MeshTaskState::Running;
        SAL_INFO("kqoffice.ai.mesh", "dequeueForAgent: " << best->taskId
            << " → " << agentId);
    }
    return best;
}

std::vector<MeshTask*> WorkspaceMeshTaskQueue::dequeueReady(int maxCount)
{
    osl::MutexGuard guard(m_aMutex);

    std::vector<MeshTask*> ready;
    for (auto& t : m_aTasks)
    {
        if (t.state == MeshTaskState::Pending && isReady(t.taskId))
            ready.push_back(&t);
    }

    // Sort by priority (High first), then taskId for stability.
    std::sort(ready.begin(), ready.end(),
        [](const MeshTask* a, const MeshTask* b) {
            if (a->priority != b->priority)
                return static_cast<int>(a->priority) > static_cast<int>(b->priority);
            return a->taskId < b->taskId;
        });

    if (static_cast<int>(ready.size()) > maxCount)
        ready.resize(maxCount);

    for (auto* t : ready)
        t->state = MeshTaskState::Running;

    return ready;
}

// ── Dependency resolution ─────────────────────────────────────────────────

bool WorkspaceMeshTaskQueue::isReady(const OUString& taskId)
{
    for (const auto& t : m_aTasks)
    {
        if (t.taskId == taskId)
        {
            for (const auto& depId : t.dependsOn)
            {
                bool depCompleted = false;
                for (const auto& d : m_aTasks)
                {
                    if (d.taskId == depId && d.state == MeshTaskState::Completed)
                    {
                        depCompleted = true;
                        break;
                    }
                }
                if (!depCompleted)
                    return false;
            }
            return true;
        }
    }
    return false;
}

std::vector<OUString> WorkspaceMeshTaskQueue::blockedTasks(const OUString& taskId)
{
    osl::MutexGuard guard(m_aMutex);
    std::vector<OUString> result;
    for (const auto& t : m_aTasks)
    {
        for (const auto& depId : t.dependsOn)
        {
            if (depId == taskId)
            {
                result.push_back(t.taskId);
                break;
            }
        }
    }
    return result;
}

// ── Priority sorting ──────────────────────────────────────────────────────

void WorkspaceMeshTaskQueue::sortByPriority()
{
    osl::MutexGuard guard(m_aMutex);
    std::stable_sort(m_aTasks.begin(), m_aTasks.end(),
        [](const MeshTask& a, const MeshTask& b) {
            return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        });
}

// ── State management ──────────────────────────────────────────────────────

bool WorkspaceMeshTaskQueue::updateState(const OUString& taskId, MeshTaskState state)
{
    osl::MutexGuard guard(m_aMutex);
    for (auto& t : m_aTasks)
    {
        if (t.taskId == taskId)
        {
            t.state = state;
            SAL_INFO("kqoffice.ai.mesh", "updateState: " << taskId
                << " → " << static_cast<int>(state));
            return true;
        }
    }
    return false;
}

bool WorkspaceMeshTaskQueue::assignAgent(const OUString& taskId, const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);
    for (auto& t : m_aTasks)
    {
        if (t.taskId == taskId)
        {
            t.assignedAgentId = agentId;
            SAL_INFO("kqoffice.ai.mesh", "assignAgent: " << taskId
                << " → " << agentId);
            return true;
        }
    }
    return false;
}

std::vector<MeshTask*> WorkspaceMeshTaskQueue::tasksByState(MeshTaskState state)
{
    osl::MutexGuard guard(m_aMutex);
    std::vector<MeshTask*> result;
    for (auto& t : m_aTasks)
    {
        if (t.state == state)
            result.push_back(&t);
    }
    return result;
}

sal_Int32 WorkspaceMeshTaskQueue::countByState(MeshTaskState state)
{
    osl::MutexGuard guard(m_aMutex);
    sal_Int32 count = 0;
    for (const auto& t : m_aTasks)
    {
        if (t.state == state)
            count++;
    }
    return count;
}

sal_Int32 WorkspaceMeshTaskQueue::totalCount()
{
    osl::MutexGuard guard(m_aMutex);
    return static_cast<sal_Int32>(m_aTasks.size());
}

MeshTask* WorkspaceMeshTaskQueue::findById(const OUString& taskId)
{
    osl::MutexGuard guard(m_aMutex);
    for (auto& t : m_aTasks)
    {
        if (t.taskId == taskId)
            return &t;
    }
    return nullptr;
}

} // namespace kqoffice::ai::mesh

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
