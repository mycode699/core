/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Multi-agent mesh: agent registration, discovery, routing, and health.
 * Inspired by imux WorkspaceAgentMesh — maps agent identity to capabilities
 * and maintains routing statistics for intelligent task dispatch.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_MESH_WORKSPACEAGENTMESH_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_MESH_WORKSPACEAGENTMESH_HXX

#include <osl/mutex.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <map>
#include <vector>

namespace kqoffice::ai::mesh
{

/// Descriptor for a registered agent in the mesh.
struct AgentDescriptor
{
    OUString agentId;       // unique ID, e.g. "writer-001"
    OUString agentType;     // "writer", "calc", "impress", "reviewer", etc.
    OUString displayName;   // "Writer 改写助手", "Calc 公式助手", etc.
    OUString capabilities;  // comma-separated: "rewrite,expand,shorten"
    sal_Int32 maxConcurrency = 1;
    sal_Int32 priority = 50; // 0=lowest, 100=highest
    sal_Int64 lastHeartbeatMs = 0;
    bool healthy = true;
};

/// Routing statistics for an agent-capability pair.
struct AgentRoute
{
    OUString agentId;
    OUString routingKey;    // capability key: "rewrite", "formula", "chart", etc.
    double successRate = 1.0; // 0.0-1.0, starts optimistic
    sal_Int64 avgLatencyMs = 0;
    sal_Int32 totalDispatches = 0;
    sal_Int32 totalSuccesses = 0;
};

/// Health status for UI display.
enum class AgentHealth
{
    Healthy,   // green  — recent heartbeat, no failures
    Degraded,  // yellow — stale heartbeat or elevated failure rate
    Unhealthy, // red    — no heartbeat or repeated failures
};

class WorkspaceAgentMesh
{
public:
    WorkspaceAgentMesh();
    ~WorkspaceAgentMesh();

    // ── Registration ──────────────────────────────────────────────────

    /// Register a new agent. Returns false if agentId already exists.
    bool registerAgent(const AgentDescriptor& desc);

    /// Remove an agent from the mesh. Returns false if not found.
    bool unregisterAgent(const OUString& agentId);

    /// List all registered agents.
    std::vector<AgentDescriptor> listAgents();

    // ── Discovery ─────────────────────────────────────────────────────

    /// Find an agent by exact ID. Returns nullptr if not found.
    AgentDescriptor* findById(const OUString& agentId);

    /// Find all agents that support a given capability token.
    std::vector<AgentDescriptor> findByCapability(const OUString& capability);

    /// Find all agents of a given type.
    std::vector<AgentDescriptor> findByType(const OUString& agentType);

    // ── Routing ───────────────────────────────────────────────────────

    /// Return the best route for a capability, or nullptr if none available.
    /// Selection: highest successRate, then lowest avgLatencyMs, then
    /// highest agent priority.
    AgentRoute* bestRoute(const OUString& capability);

    /// Update routing statistics after a dispatch completes.
    void updateRouteStats(const OUString& agentId, bool success,
                          sal_Int64 latencyMs);

    /// List all routes for a given agent.
    std::vector<AgentRoute> routesForAgent(const OUString& agentId);

    // ── Health ────────────────────────────────────────────────────────

    /// Check whether an agent is currently healthy.
    bool isAgentHealthy(const OUString& agentId);

    /// Record a heartbeat from an agent, updating its lastHeartbeatMs
    /// and marking it healthy.
    void heartbeat(const OUString& agentId);

    /// Get the health enum for UI display.
    AgentHealth agentHealth(const OUString& agentId);

    /// Mark an agent as unhealthy (e.g. after repeated failures).
    void markUnhealthy(const OUString& agentId);

    /// Heartbeat timeout in milliseconds. Agents without a heartbeat
    /// within this window are considered degraded.
    static constexpr sal_Int64 kHeartbeatTimeoutMs = 30000;

private:
    /// Find or create a route entry for an agent+key pair.
    AgentRoute& ensureRoute(const OUString& agentId,
                            const OUString& routingKey);

    std::vector<AgentDescriptor> m_aAgents;
    std::vector<AgentRoute> m_aRoutes;
    osl::Mutex m_aMutex;
};

} // namespace kqoffice::ai::mesh

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
