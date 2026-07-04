/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of WorkspaceAgentMeshOrchestrator.
 */

#include "WorkspaceAgentMeshOrchestrator.hxx"

#include <sal/log.hxx>

namespace kqoffice::ai::mesh
{

WorkspaceAgentMeshOrchestrator::WorkspaceAgentMeshOrchestrator(
    WorkspaceAgentMesh& mesh, WorkspaceMeshTaskQueue& queue)
    : m_rMesh(mesh)
    , m_rQueue(queue)
{
}

WorkspaceAgentMeshOrchestrator::~WorkspaceAgentMeshOrchestrator() {}

// ── Decomposition ────────────────────────────────────────────────────────

std::vector<MeshTask> WorkspaceAgentMeshOrchestrator::decompose(
    const OUString& userRequest, const OUString& surface, const OUString& context)
{
    std::vector<MeshTask> subTasks;
    (void)context;

    OUString parentId = "task-" + OUString::number(
        static_cast<sal_Int64>(m_rQueue.totalCount() + 1));

    if (surface.equalsIgnoreAsciiCase("writer"))
    {
        // Writer sub-tasks: rewrite, expand, shorten.
        MeshTask t1;
        t1.taskId = makeSubTaskId(parentId, 1);
        t1.parentTaskId = parentId;
        t1.instruction = "Rewrite: " + userRequest;
        t1.targetSurface = "writer";
        t1.priority = MeshTaskPriority::High;
        subTasks.push_back(t1);

        MeshTask t2;
        t2.taskId = makeSubTaskId(parentId, 2);
        t2.parentTaskId = parentId;
        t2.instruction = "Expand: " + userRequest;
        t2.targetSurface = "writer";
        t2.priority = MeshTaskPriority::Normal;
        t2.dependsOn.push_back(t1.taskId);
        subTasks.push_back(t2);

        MeshTask t3;
        t3.taskId = makeSubTaskId(parentId, 3);
        t3.parentTaskId = parentId;
        t3.instruction = "Shorten: " + userRequest;
        t3.targetSurface = "writer";
        t3.priority = MeshTaskPriority::Normal;
        t3.dependsOn.push_back(t1.taskId);
        subTasks.push_back(t3);
    }
    else if (surface.equalsIgnoreAsciiCase("calc"))
    {
        // Calc sub-tasks: formula, analyze, chart.
        MeshTask t1;
        t1.taskId = makeSubTaskId(parentId, 1);
        t1.parentTaskId = parentId;
        t1.instruction = "Formula: " + userRequest;
        t1.targetSurface = "calc";
        t1.priority = MeshTaskPriority::High;
        subTasks.push_back(t1);

        MeshTask t2;
        t2.taskId = makeSubTaskId(parentId, 2);
        t2.parentTaskId = parentId;
        t2.instruction = "Analyze: " + userRequest;
        t2.targetSurface = "calc";
        t2.priority = MeshTaskPriority::Normal;
        t2.dependsOn.push_back(t1.taskId);
        subTasks.push_back(t2);

        MeshTask t3;
        t3.taskId = makeSubTaskId(parentId, 3);
        t3.parentTaskId = parentId;
        t3.instruction = "Chart: " + userRequest;
        t3.targetSurface = "calc";
        t3.priority = MeshTaskPriority::Normal;
        t3.dependsOn.push_back(t1.taskId);
        subTasks.push_back(t3);
    }
    else if (surface.equalsIgnoreAsciiCase("impress"))
    {
        // Impress sub-tasks: slide, create, design.
        MeshTask t1;
        t1.taskId = makeSubTaskId(parentId, 1);
        t1.parentTaskId = parentId;
        t1.instruction = "Slide: " + userRequest;
        t1.targetSurface = "impress";
        t1.priority = MeshTaskPriority::High;
        subTasks.push_back(t1);

        MeshTask t2;
        t2.taskId = makeSubTaskId(parentId, 2);
        t2.parentTaskId = parentId;
        t2.instruction = "Create: " + userRequest;
        t2.targetSurface = "impress";
        t2.priority = MeshTaskPriority::Normal;
        t2.dependsOn.push_back(t1.taskId);
        subTasks.push_back(t2);

        MeshTask t3;
        t3.taskId = makeSubTaskId(parentId, 3);
        t3.parentTaskId = parentId;
        t3.instruction = "Design: " + userRequest;
        t3.targetSurface = "impress";
        t3.priority = MeshTaskPriority::Normal;
        t3.dependsOn.push_back(t1.taskId);
        subTasks.push_back(t3);
    }

    SAL_INFO("kqoffice.ai.mesh", "decompose: " << userRequest
        << " → " << subTasks.size() << " sub-tasks for surface " << surface);
    return subTasks;
}

// ── Dispatch ─────────────────────────────────────────────────────────────

bool WorkspaceAgentMeshOrchestrator::dispatch(const MeshTask& task)
{
    AgentDescriptor* agent = selectAgent(task);
    if (!agent)
    {
        SAL_INFO("kqoffice.ai.mesh", "dispatch: no agent for task " << task.taskId);
        return false;
    }

    m_rQueue.assignAgent(task.taskId, agent->agentId);
    m_rQueue.updateState(task.taskId, MeshTaskState::Running);

    SAL_INFO("kqoffice.ai.mesh", "dispatch: " << task.taskId
        << " → agent " << agent->agentId);
    return true;
}

sal_Int32 WorkspaceAgentMeshOrchestrator::dispatchAll()
{
    sal_Int32 count = 0;
    auto ready = m_rQueue.dequeueReady(100);
    for (auto* t : ready)
    {
        if (dispatch(*t))
            count++;
    }
    return count;
}

// ── Merge ─────────────────────────────────────────────────────────────────

MergeResult WorkspaceAgentMeshOrchestrator::merge(const OUString& parentTaskId)
{
    MergeResult result;
    result.allSucceeded = true;

    // Find all sub-tasks for this parent.
    std::vector<MeshTask*> subTasks;
    for (auto& t : m_rQueue.tasksByState(MeshTaskState::Completed))
    {
        if (t->parentTaskId == parentTaskId)
            subTasks.push_back(t);
    }
    for (auto& t : m_rQueue.tasksByState(MeshTaskState::Failed))
    {
        if (t->parentTaskId == parentTaskId)
        {
            subTasks.push_back(t);
            result.allSucceeded = false;
            result.failedTasks.push_back(t->taskId);
        }
    }

    result.mergedPlanId = parentTaskId + "-merged";
    result.summary = "Merged " + OUString::number(
        static_cast<sal_Int32>(subTasks.size())) + " sub-tasks for " + parentTaskId;

    SAL_INFO("kqoffice.ai.mesh", "merge: " << parentTaskId
        << " allSucceeded=" << (result.allSucceeded ? "true" : "false"));
    return result;
}

// ── Progress ─────────────────────────────────────────────────────────────

MeshProgress WorkspaceAgentMeshOrchestrator::progress(const OUString& parentTaskId)
{
    MeshProgress p;
    for (const auto& t : m_rQueue.tasksByState(MeshTaskState::Pending))
    {
        if (t->parentTaskId == parentTaskId) { p.total++; p.pending++; }
    }
    for (const auto& t : m_rQueue.tasksByState(MeshTaskState::Running))
    {
        if (t->parentTaskId == parentTaskId) { p.total++; p.running++; }
    }
    for (const auto& t : m_rQueue.tasksByState(MeshTaskState::Completed))
    {
        if (t->parentTaskId == parentTaskId) { p.total++; p.completed++; }
    }
    for (const auto& t : m_rQueue.tasksByState(MeshTaskState::Failed))
    {
        if (t->parentTaskId == parentTaskId) { p.total++; p.failed++; }
    }
    return p;
}

// ── Private ───────────────────────────────────────────────────────────────

OUString WorkspaceAgentMeshOrchestrator::makeSubTaskId(const OUString& parentId,
                                                        sal_Int32 index)
{
    return parentId + "-sub-" + OUString::number(index);
}

AgentDescriptor* WorkspaceAgentMeshOrchestrator::selectAgent(const MeshTask& task)
{
    // Find agents matching the target surface capability.
    auto candidates = m_rMesh.findByCapability(task.targetSurface);
    if (candidates.empty())
        candidates = m_rMesh.findByType(task.targetSurface);

    if (candidates.empty())
        return nullptr;

    // Return the first healthy candidate.
    for (auto& c : candidates)
    {
        if (m_rMesh.isAgentHealthy(c.agentId))
            return m_rMesh.findById(c.agentId);
    }

    // Fallback: return first candidate even if unhealthy.
    return m_rMesh.findById(candidates[0].agentId);
}

} // namespace kqoffice::ai::mesh

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
