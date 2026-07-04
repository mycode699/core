/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of WorkspaceSupervisorAgentAPI.
 */

#include "WorkspaceSupervisorAgentAPI.hxx"

#include <osl/time.h>
#include <sal/log.hxx>

namespace kqoffice::ai::mesh
{

namespace
{
sal_Int64 currentTimeMs()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
         + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}
}

WorkspaceSupervisorAgentAPI::WorkspaceSupervisorAgentAPI(
    WorkspaceAgentMesh& mesh, WorkspaceMeshTaskQueue& queue)
    : m_rMesh(mesh)
    , m_rQueue(queue)
{
}

WorkspaceSupervisorAgentAPI::~WorkspaceSupervisorAgentAPI() {}

// ── Lifecycle management ─────────────────────────────────────────────────

bool WorkspaceSupervisorAgentAPI::startAgent(const OUString& agentId)
{
    osl::MutexGuard guard(m_aStateMutex);
    AgentRuntimeState& state = ensureRuntimeState(agentId);
    state.lifecycleState = "running";
    state.startTimeMs = currentTimeMs();
    m_rMesh.heartbeat(agentId);
    SAL_INFO("kqoffice.ai.mesh", "startAgent: " << agentId);
    return true;
}

bool WorkspaceSupervisorAgentAPI::stopAgent(const OUString& agentId)
{
    osl::MutexGuard guard(m_aStateMutex);
    AgentRuntimeState& state = ensureRuntimeState(agentId);
    state.lifecycleState = "stopped";
    SAL_INFO("kqoffice.ai.mesh", "stopAgent: " << agentId);
    return true;
}

bool WorkspaceSupervisorAgentAPI::restartAgent(const OUString& agentId)
{
    stopAgent(agentId);
    return startAgent(agentId);
}

bool WorkspaceSupervisorAgentAPI::pauseAgent(const OUString& agentId)
{
    osl::MutexGuard guard(m_aStateMutex);
    AgentRuntimeState& state = ensureRuntimeState(agentId);
    state.lifecycleState = "paused";
    SAL_INFO("kqoffice.ai.mesh", "pauseAgent: " << agentId);
    return true;
}

bool WorkspaceSupervisorAgentAPI::resumeAgent(const OUString& agentId)
{
    osl::MutexGuard guard(m_aStateMutex);
    AgentRuntimeState& state = ensureRuntimeState(agentId);
    state.lifecycleState = "running";
    SAL_INFO("kqoffice.ai.mesh", "resumeAgent: " << agentId);
    return true;
}

// ── Budget enforcement ───────────────────────────────────────────────────

bool WorkspaceSupervisorAgentAPI::setBudget(const OUString& agentId,
                                             sal_Int64 maxCpuPercent,
                                             sal_Int64 maxRssMb,
                                             sal_Int64 maxRuntimeMs)
{
    osl::MutexGuard guard(m_aStateMutex);
    AgentRuntimeState& state = ensureRuntimeState(agentId);
    state.maxCpuPercent = maxCpuPercent;
    state.maxRssMb = maxRssMb;
    state.maxRuntimeMs = maxRuntimeMs;
    SAL_INFO("kqoffice.ai.mesh", "setBudget: " << agentId
        << " cpu=" << maxCpuPercent << "% rss=" << maxRssMb
        << "MB runtime=" << maxRuntimeMs << "ms");
    return true;
}

BudgetStatus WorkspaceSupervisorAgentAPI::checkBudget(const OUString& agentId)
{
    BudgetStatus status;
    osl::MutexGuard guard(m_aStateMutex);
    AgentRuntimeState* state = findRuntimeState(agentId);
    if (!state)
    {
        status.exceeded = false;
        return status;
    }

    // Stub: real monitoring needs OS integration.
    // For now, check runtime against budget.
    sal_Int64 now = currentTimeMs();
    status.runtimeMs = now - state->startTimeMs;
    if (status.runtimeMs > state->maxRuntimeMs)
    {
        status.exceeded = true;
        status.exceededReason = "runtime exceeded: "
            + OUString::number(status.runtimeMs) + "ms > "
            + OUString::number(state->maxRuntimeMs) + "ms";
    }

    return status;
}

// ── Recovery ─────────────────────────────────────────────────────────────

bool WorkspaceSupervisorAgentAPI::recoverAgent(const OUString& agentId)
{
    m_rMesh.heartbeat(agentId);

    // Re-dispatch any failed tasks assigned to this agent.
    for (auto* t : m_rQueue.tasksByState(MeshTaskState::Failed))
    {
        if (t->assignedAgentId == agentId)
        {
            t->state = MeshTaskState::Pending;
            t->assignedAgentId.clear();
        }
    }

    SAL_INFO("kqoffice.ai.mesh", "recoverAgent: " << agentId);
    return true;
}

sal_Int32 WorkspaceSupervisorAgentAPI::evacuateTasks(const OUString& agentId,
                                                      const OUString& targetAgentId)
{
    sal_Int32 count = 0;
    for (auto* t : m_rQueue.tasksByState(MeshTaskState::Running))
    {
        if (t->assignedAgentId == agentId)
        {
            t->assignedAgentId = targetAgentId;
            count++;
        }
    }
    for (auto* t : m_rQueue.tasksByState(MeshTaskState::Pending))
    {
        if (t->assignedAgentId == agentId)
        {
            t->assignedAgentId = targetAgentId;
            count++;
        }
    }

    SAL_INFO("kqoffice.ai.mesh", "evacuateTasks: " << count
        << " tasks from " << agentId << " → " << targetAgentId);
    return count;
}

// ── Diagnostics ──────────────────────────────────────────────────────────

AgentDiagnostics WorkspaceSupervisorAgentAPI::diagnose(const OUString& agentId)
{
    AgentDiagnostics d;
    d.agentId = agentId;

    osl::MutexGuard guard(m_aStateMutex);
    AgentRuntimeState* state = findRuntimeState(agentId);
    if (state)
    {
        d.state = state->lifecycleState;
        sal_Int64 now = currentTimeMs();
        d.uptimeMs = now - state->startTimeMs;
    }
    else
    {
        d.state = "unknown";
    }

    // Count completed/failed tasks for this agent.
    for (const auto* t : m_rQueue.tasksByState(MeshTaskState::Completed))
    {
        if (t->assignedAgentId == agentId)
            d.tasksCompleted++;
    }
    for (const auto* t : m_rQueue.tasksByState(MeshTaskState::Failed))
    {
        if (t->assignedAgentId == agentId)
            d.tasksFailed++;
    }

    sal_Int32 total = d.tasksCompleted + d.tasksFailed;
    d.successRate = total > 0
        ? static_cast<double>(d.tasksCompleted) / total
        : 0.0;

    d.budget = checkBudget(agentId);
    return d;
}

std::vector<AgentDiagnostics> WorkspaceSupervisorAgentAPI::diagnoseAll()
{
    std::vector<AgentDiagnostics> results;
    auto agents = m_rMesh.listAgents();
    for (const auto& a : agents)
    {
        results.push_back(diagnose(a.agentId));
    }
    return results;
}

// ── Private ───────────────────────────────────────────────────────────────

WorkspaceSupervisorAgentAPI::AgentRuntimeState*
WorkspaceSupervisorAgentAPI::findRuntimeState(const OUString& agentId)
{
    for (auto& s : m_aRuntimeStates)
    {
        if (s.agentId == agentId)
            return &s;
    }
    return nullptr;
}

WorkspaceSupervisorAgentAPI::AgentRuntimeState&
WorkspaceSupervisorAgentAPI::ensureRuntimeState(const OUString& agentId)
{
    for (auto& s : m_aRuntimeStates)
    {
        if (s.agentId == agentId)
            return s;
    }
    AgentRuntimeState s;
    s.agentId = agentId;
    m_aRuntimeStates.push_back(s);
    return m_aRuntimeStates.back();
}

} // namespace kqoffice::ai::mesh

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
