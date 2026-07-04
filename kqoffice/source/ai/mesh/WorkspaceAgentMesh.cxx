/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of WorkspaceAgentMesh.
 */

#include "WorkspaceAgentMesh.hxx"

#include <algorithm>
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

WorkspaceAgentMesh::WorkspaceAgentMesh() {}
WorkspaceAgentMesh::~WorkspaceAgentMesh() {}

// ── Registration ──────────────────────────────────────────────────────────

bool WorkspaceAgentMesh::registerAgent(const AgentDescriptor& desc)
{
    osl::MutexGuard guard(m_aMutex);

    for (const auto& a : m_aAgents)
    {
        if (a.agentId == desc.agentId)
        {
            SAL_INFO("kqoffice.ai.mesh",
                "registerAgent: duplicate agentId " << desc.agentId);
            return false;
        }
    }

    m_aAgents.push_back(desc);

    // Create default routes for each capability token.
    sal_Int32 start = 0;
    const OUString caps = desc.capabilities;
    while (start < caps.getLength())
    {
        sal_Int32 comma = caps.indexOf(',', start);
        if (comma < 0)
            comma = caps.getLength();
        OUString token = caps.copy(start, comma - start).trim();
        if (!token.isEmpty())
        {
            ensureRoute(desc.agentId, token);
        }
        start = comma + 1;
    }

    SAL_INFO("kqoffice.ai.mesh", "registerAgent: registered " << desc.agentId
        << " type=" << desc.agentType << " caps=" << desc.capabilities);
    return true;
}

bool WorkspaceAgentMesh::unregisterAgent(const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);

    auto it = std::find_if(m_aAgents.begin(), m_aAgents.end(),
        [&](const AgentDescriptor& a) { return a.agentId == agentId; });
    if (it == m_aAgents.end())
        return false;

    m_aAgents.erase(it);

    // Remove all routes for this agent.
    m_aRoutes.erase(
        std::remove_if(m_aRoutes.begin(), m_aRoutes.end(),
            [&](const AgentRoute& r) { return r.agentId == agentId; }),
        m_aRoutes.end());

    SAL_INFO("kqoffice.ai.mesh", "unregisterAgent: removed " << agentId);
    return true;
}

std::vector<AgentDescriptor> WorkspaceAgentMesh::listAgents()
{
    osl::MutexGuard guard(m_aMutex);
    return m_aAgents;
}

// ── Discovery ─────────────────────────────────────────────────────────────

AgentDescriptor* WorkspaceAgentMesh::findById(const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);
    for (auto& a : m_aAgents)
    {
        if (a.agentId == agentId)
            return &a;
    }
    return nullptr;
}

std::vector<AgentDescriptor> WorkspaceAgentMesh::findByCapability(const OUString& capability)
{
    osl::MutexGuard guard(m_aMutex);
    std::vector<AgentDescriptor> result;
    for (const auto& a : m_aAgents)
    {
        sal_Int32 start = 0;
        const OUString caps = a.capabilities;
        while (start < caps.getLength())
        {
            sal_Int32 comma = caps.indexOf(',', start);
            if (comma < 0)
                comma = caps.getLength();
            OUString token = caps.copy(start, comma - start).trim();
            if (token.equalsIgnoreAsciiCase(capability))
            {
                result.push_back(a);
                break;
            }
            start = comma + 1;
        }
    }
    return result;
}

std::vector<AgentDescriptor> WorkspaceAgentMesh::findByType(const OUString& agentType)
{
    osl::MutexGuard guard(m_aMutex);
    std::vector<AgentDescriptor> result;
    for (const auto& a : m_aAgents)
    {
        if (a.agentType.equalsIgnoreAsciiCase(agentType))
            result.push_back(a);
    }
    return result;
}

// ── Routing ───────────────────────────────────────────────────────────────

AgentRoute* WorkspaceAgentMesh::bestRoute(const OUString& capability)
{
    osl::MutexGuard guard(m_aMutex);

    // Collect routes matching the capability key.
    std::vector<AgentRoute*> candidates;
    for (auto& r : m_aRoutes)
    {
        if (r.routingKey.equalsIgnoreAsciiCase(capability))
        {
            // Only consider routes for healthy agents.
            for (const auto& a : m_aAgents)
            {
                if (a.agentId == r.agentId && a.healthy)
                {
                    candidates.push_back(&r);
                    break;
                }
            }
        }
    }

    if (candidates.empty())
        return nullptr;

    // Sort: highest successRate, lowest avgLatencyMs, highest priority.
    std::sort(candidates.begin(), candidates.end(),
        [](const AgentRoute* a, const AgentRoute* b) {
            if (a->successRate != b->successRate)
                return a->successRate > b->successRate;
            if (a->avgLatencyMs != b->avgLatencyMs)
                return a->avgLatencyMs < b->avgLatencyMs;
            return a->agentId < b->agentId; // stable tie-break
        });

    return candidates.front();
}

void WorkspaceAgentMesh::updateRouteStats(const OUString& agentId, bool success,
                                          sal_Int64 latencyMs)
{
    osl::MutexGuard guard(m_aMutex);

    // Update all routes for this agent (generic stats update).
    for (auto& r : m_aRoutes)
    {
        if (r.agentId == agentId)
        {
            r.totalDispatches++;
            if (success)
                r.totalSuccesses++;
            // Rolling average success rate (0.8×old + 0.2×new).
            double newRate = r.totalDispatches > 0
                ? static_cast<double>(r.totalSuccesses) / r.totalDispatches
                : 0.0;
            r.successRate = r.successRate * 0.8 + newRate * 0.2;
            // Rolling average latency.
            r.avgLatencyMs = r.avgLatencyMs > 0
                ? (r.avgLatencyMs * 0.8 + latencyMs * 0.2)
                : latencyMs;
        }
    }
}

std::vector<AgentRoute> WorkspaceAgentMesh::routesForAgent(const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);
    std::vector<AgentRoute> result;
    for (const auto& r : m_aRoutes)
    {
        if (r.agentId == agentId)
            result.push_back(r);
    }
    return result;
}

// ── Health ────────────────────────────────────────────────────────────────

bool WorkspaceAgentMesh::isAgentHealthy(const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);
    for (const auto& a : m_aAgents)
    {
        if (a.agentId == agentId)
            return a.healthy;
    }
    return false;
}

void WorkspaceAgentMesh::heartbeat(const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);
    for (auto& a : m_aAgents)
    {
        if (a.agentId == agentId)
        {
            a.lastHeartbeatMs = currentTimeMs();
            a.healthy = true;
            return;
        }
    }
}

AgentHealth WorkspaceAgentMesh::agentHealth(const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);
    for (const auto& a : m_aAgents)
    {
        if (a.agentId == agentId)
        {
            if (!a.healthy)
                return AgentHealth::Unhealthy;

            sal_Int64 now = currentTimeMs();
            if (now - a.lastHeartbeatMs > kHeartbeatTimeoutMs)
                return AgentHealth::Degraded;

            return AgentHealth::Healthy;
        }
    }
    return AgentHealth::Unhealthy; // unknown agent
}

void WorkspaceAgentMesh::markUnhealthy(const OUString& agentId)
{
    osl::MutexGuard guard(m_aMutex);
    for (auto& a : m_aAgents)
    {
        if (a.agentId == agentId)
        {
            a.healthy = false;
            SAL_INFO("kqoffice.ai.mesh", "markUnhealthy: " << agentId);
            return;
        }
    }
}

// ── Private ───────────────────────────────────────────────────────────────

AgentRoute& WorkspaceAgentMesh::ensureRoute(const OUString& agentId,
                                             const OUString& routingKey)
{
    for (auto& r : m_aRoutes)
    {
        if (r.agentId == agentId && r.routingKey.equalsIgnoreAsciiCase(routingKey))
            return r;
    }

    AgentRoute route;
    route.agentId = agentId;
    route.routingKey = routingKey;
    m_aRoutes.push_back(route);
    return m_aRoutes.back();
}

} // namespace kqoffice::ai::mesh

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
