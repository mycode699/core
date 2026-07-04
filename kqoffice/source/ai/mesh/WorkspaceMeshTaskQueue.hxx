/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Mesh task queue: dependency-aware priority queue for multi-agent
 * task scheduling. Inspired by imux WorkspaceMeshTaskQueue.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_MESH_WORKSPACEMESHTASKQUEUE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_MESH_WORKSPACEMESHTASKQUEUE_HXX

#include <osl/mutex.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::mesh
{

/// Task priority levels for scheduling order.
enum class MeshTaskPriority
{
    Low = 0,
    Normal = 1,
    High = 2,
};

/// Task lifecycle states.
enum class MeshTaskState
{
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
};

/// A task in the mesh queue with dependency tracking.
struct MeshTask
{
    OUString taskId;
    OUString parentTaskId;          // empty for root tasks
    OUString assignedAgentId;       // empty until dispatched
    MeshTaskPriority priority = MeshTaskPriority::Normal;
    std::vector<OUString> dependsOn; // taskIds this task depends on
    std::vector<OUString> blocks;    // taskIds blocked by this task
    MeshTaskState state = MeshTaskState::Pending;

    // Execution context
    OUString instruction;
    OUString targetSurface;          // "writer", "calc", "impress"

    // Result
    OUString resultPlanId;
    std::vector<OUString> evidenceIds;
};

class WorkspaceMeshTaskQueue
{
public:
    WorkspaceMeshTaskQueue();
    ~WorkspaceMeshTaskQueue();

    // ── Enqueue ───────────────────────────────────────────────────────

    /// Enqueue a single task. Returns false if taskId already exists.
    bool enqueue(const MeshTask& task);

    /// Enqueue multiple tasks at once. Returns the count successfully
    /// enqueued (skips duplicates).
    sal_Int32 enqueueBatch(const std::vector<MeshTask>& tasks);

    // ── Dequeue ───────────────────────────────────────────────────────

    /// Dequeue the highest-priority ready task for a specific agent.
    /// Returns nullptr if no ready task is available.
    MeshTask* dequeueForAgent(const OUString& agentId);

    /// Dequeue up to maxCount ready tasks (highest priority first).
    std::vector<MeshTask*> dequeueReady(int maxCount);

    // ── Dependency resolution ─────────────────────────────────────────

    /// Check whether a task is ready to run (all dependencies satisfied).
    bool isReady(const OUString& taskId);

    /// List task IDs that are blocked by the given task (not yet complete).
    std::vector<OUString> blockedTasks(const OUString& taskId);

    // ── Priority sorting ──────────────────────────────────────────────

    /// Sort all pending tasks by priority (High first), then by taskId
    /// for stable ordering.
    void sortByPriority();

    // ── State management ──────────────────────────────────────────────

    /// Update a task's state. Returns false if task not found.
    bool updateState(const OUString& taskId, MeshTaskState state);

    /// Assign an agent to a task.
    bool assignAgent(const OUString& taskId, const OUString& agentId);

    /// List all tasks in a given state.
    std::vector<MeshTask*> tasksByState(MeshTaskState state);

    /// Count tasks in each state.
    sal_Int32 countByState(MeshTaskState state);

    /// Total number of tasks in the queue.
    sal_Int32 totalCount();

    /// Find a task by ID. Returns nullptr if not found.
    MeshTask* findById(const OUString& taskId);

private:
    std::vector<MeshTask> m_aTasks;
    osl::Mutex m_aMutex;
};

} // namespace kqoffice::ai::mesh

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
