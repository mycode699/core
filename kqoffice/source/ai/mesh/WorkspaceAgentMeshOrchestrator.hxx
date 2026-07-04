/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Orchestrator: decomposes user requests into sub-tasks, dispatches to
 * agents via the mesh, and merges results. Inspired by imux
 * WorkspaceAgentMeshOrchestrator.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_MESH_WORKSPACEAGENTMESHORCHESTRATOR_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_MESH_WORKSPACEAGENTMESHORCHESTRATOR_HXX

#include "WorkspaceAgentMesh.hxx"
#include "WorkspaceMeshTaskQueue.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::mesh
{

/// Result of merging sub-task outputs.
struct MergeResult
{
    OUString mergedPlanId;
    bool allSucceeded = false;
    std::vector<OUString> failedTasks;
    OUString summary;
};

/// Progress snapshot for a parent task and its sub-tasks.
struct MeshProgress
{
    sal_Int32 total = 0;
    sal_Int32 completed = 0;
    sal_Int32 failed = 0;
    sal_Int32 running = 0;
    sal_Int32 pending = 0;

    double percentComplete() const
    {
        return total > 0 ? (completed * 100.0 / total) : 0.0;
    }
};

class WorkspaceAgentMeshOrchestrator
{
public:
    WorkspaceAgentMeshOrchestrator(WorkspaceAgentMesh& mesh,
                                   WorkspaceMeshTaskQueue& queue);
    ~WorkspaceAgentMeshOrchestrator();

    // ── Decomposition ────────────────────────────────────────────────

    /// Decompose a user request into sub-tasks based on the target
    /// surface and available agents. Context provides additional
    /// hints (e.g. document type, selected text).
    std::vector<MeshTask> decompose(const OUString& userRequest,
                                    const OUString& surface,
                                    const OUString& context);

    // ── Dispatch ─────────────────────────────────────────────────────

    /// Dispatch a single task to the best available agent.
    /// Returns false if no suitable agent is found.
    bool dispatch(const MeshTask& task);

    /// Dispatch all ready tasks in the queue.
    sal_Int32 dispatchAll();

    // ── Merge ─────────────────────────────────────────────────────────

    /// Merge results from all sub-tasks of a parent task.
    MergeResult merge(const OUString& parentTaskId);

    // ── Progress ─────────────────────────────────────────────────────

    /// Compute progress for a parent task and its sub-tasks.
    MeshProgress progress(const OUString& parentTaskId);

    // ── Accessors ─────────────────────────────────────────────────────

    WorkspaceAgentMesh& mesh() { return m_rMesh; }
    WorkspaceMeshTaskQueue& queue() { return m_rQueue; }

private:
    /// Generate a unique sub-task ID from a parent ID and index.
    static OUString makeSubTaskId(const OUString& parentId,
                                  sal_Int32 index);

    /// Select the best agent for a task based on capability match.
    AgentDescriptor* selectAgent(const MeshTask& task);

    WorkspaceAgentMesh& m_rMesh;
    WorkspaceMeshTaskQueue& m_rQueue;
};

} // namespace kqoffice::ai::mesh

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
