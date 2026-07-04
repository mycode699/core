/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Supervisor agent API: lifecycle management, budget enforcement,
 * recovery, and diagnostics for agents in the mesh. Inspired by imux
 * WorkspaceSupervisorAgentAPI.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_MESH_WORKSPACESUPERVISORAGENTAPI_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_MESH_WORKSPACESUPERVISORAGENTAPI_HXX

#include "WorkspaceAgentMesh.hxx"
#include "WorkspaceMeshTaskQueue.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::mesh
{

/// Budget status for a single agent.
struct BudgetStatus
{
    double cpuPercent = 0.0;
    sal_Int64 rssMb = 0;
    sal_Int64 runtimeMs = 0;
    bool exceeded = false;
    OUString exceededReason;
};

/// Full diagnostic snapshot for an agent.
struct AgentDiagnostics
{
    OUString agentId;
    OUString state;          // "running", "paused", "stopped", "unknown"
    sal_Int64 uptimeMs = 0;
    sal_Int32 tasksCompleted = 0;
    sal_Int32 tasksFailed = 0;
    double successRate = 0.0;
    BudgetStatus budget;
};

class WorkspaceSupervisorAgentAPI
{
public:
    WorkspaceSupervisorAgentAPI(WorkspaceAgentMesh& mesh,
                                WorkspaceMeshTaskQueue& queue);
    ~WorkspaceSupervisorAgentAPI();

    // ── Lifecycle management ─────────────────────────────────────────

    bool startAgent(const OUString& agentId);
    bool stopAgent(const OUString& agentId);
    bool restartAgent(const OUString& agentId);
    bool pauseAgent(const OUString& agentId);
    bool resumeAgent(const OUString& agentId);

    // ── Budget enforcement ───────────────────────────────────────────

    /// Set resource budget for an agent. Returns false if agent not found.
    bool setBudget(const OUString& agentId,
                   sal_Int64 maxCpuPercent,
                   sal_Int64 maxRssMb,
                   sal_Int64 maxRuntimeMs);

    /// Check current resource usage against budget.
    BudgetStatus checkBudget(const OUString& agentId);

    // ── Recovery ─────────────────────────────────────────────────────

    /// Attempt to recover a failed or unhealthy agent.
    bool recoverAgent(const OUString& agentId);

    /// Evacuate all tasks assigned to one agent to another.
    sal_Int32 evacuateTasks(const OUString& agentId,
                            const OUString& targetAgentId);

    // ── Diagnostics ──────────────────────────────────────────────────

    /// Get full diagnostics for a single agent.
    AgentDiagnostics diagnose(const OUString& agentId);

    /// Get diagnostics for all registered agents.
    std::vector<AgentDiagnostics> diagnoseAll();

private:
    WorkspaceAgentMesh& m_rMesh;
    WorkspaceMeshTaskQueue& m_rQueue;

    // Per-agent runtime state (not persisted).
    struct AgentRuntimeState
    {
        OUString agentId;
        OUString lifecycleState; // "running", "paused", "stopped"
        sal_Int64 startTimeMs = 0;
        sal_Int64 maxCpuPercent = 100;
        sal_Int64 maxRssMb = 4096;
        sal_Int64 maxRuntimeMs = 3600000; // 1 hour default
    };
    std::vector<AgentRuntimeState> m_aRuntimeStates;
    osl::Mutex m_aStateMutex;

    AgentRuntimeState* findRuntimeState(const OUString& agentId);
    AgentRuntimeState& ensureRuntimeState(const OUString& agentId);
};

} // namespace kqoffice::ai::mesh

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
