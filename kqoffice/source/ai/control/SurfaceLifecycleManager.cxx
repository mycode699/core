/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M4: Control Plane — Surface Lifecycle Manager).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SurfaceLifecycleManager.hxx"

#include <comphelper/processfactory.hxx>
#include <osl/time.h>
#include <sal/log.hxx>

namespace kqoffice::ai::control
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

bool SurfaceLifecycleManager::isValidTransition(SurfaceState from, SurfaceState to) const
{
    if (from == to)
        return false;

    // Reaped is terminal — nothing can transition out of it
    if (from == SurfaceState::Reaped)
        return false;

    switch (from)
    {
        case SurfaceState::Created:
            return to == SurfaceState::Running;

        case SurfaceState::Running:
            return to == SurfaceState::Idle
                || to == SurfaceState::Busy
                || to == SurfaceState::Blocked
                || to == SurfaceState::Hung
                || to == SurfaceState::Crashed
                || to == SurfaceState::Reaped;

        case SurfaceState::Idle:
            return to == SurfaceState::Running
                || to == SurfaceState::Reaped;

        case SurfaceState::Busy:
            return to == SurfaceState::Idle
                || to == SurfaceState::Blocked
                || to == SurfaceState::Hung
                || to == SurfaceState::Crashed
                || to == SurfaceState::Reaped;

        case SurfaceState::Blocked:
            return to == SurfaceState::Running
                || to == SurfaceState::Hung
                || to == SurfaceState::Crashed
                || to == SurfaceState::Reaped;

        case SurfaceState::Hung:
            return to == SurfaceState::Idle       // recovered
                || to == SurfaceState::Running    // recovered with work
                || to == SurfaceState::Crashed    // unrecoverable
                || to == SurfaceState::Reaped;

        case SurfaceState::Crashed:
            return to == SurfaceState::Reaped;    // only cleanup allowed

        case SurfaceState::Reaped:
            return false; // terminal
    }
    return false;
}

bool SurfaceLifecycleManager::isTerminal(SurfaceState s) const
{
    return s == SurfaceState::Crashed || s == SurfaceState::Reaped;
}

bool SurfaceLifecycleManager::transition(const OUString& surfaceId, SurfaceState newState,
                                         const OUString& reason)
{
    osl::MutexGuard guard(m_mutex);

    for (auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            SurfaceState oldState = info.state;
            if (!isValidTransition(oldState, newState))
            {
                SAL_INFO("kqoffice.ai.control",
                    "SurfaceLifecycleManager: invalid transition " << surfaceId
                    << " from " << static_cast<int>(oldState) << " to "
                    << static_cast<int>(newState));
                return false;
            }

            info.state = newState;
            info.stateEnteredMs = currentTimeMs();
            info.stateReason = reason;

            SAL_INFO("kqoffice.ai.control",
                "SurfaceLifecycleManager: transition " << surfaceId
                << " from " << static_cast<int>(oldState) << " to "
                << static_cast<int>(newState) << " reason: " << reason);
            return true;
        }
    }
    return false;
}

bool SurfaceLifecycleManager::create(const OUString& surfaceId, const OUString& workspaceId)
{
    osl::MutexGuard guard(m_mutex);

    // Check for duplicate
    for (const auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            SAL_INFO("kqoffice.ai.control",
                "SurfaceLifecycleManager: surface already exists: " << surfaceId);
            return false;
        }
    }

    SurfaceInfo info;
    info.surfaceId = surfaceId;
    info.workspaceId = workspaceId;
    info.state = SurfaceState::Created;
    info.stateEnteredMs = currentTimeMs();
    info.stateReason = "created";

    m_surfaces.push_back(info);

    SAL_INFO("kqoffice.ai.control",
        "SurfaceLifecycleManager: created surface " << surfaceId
        << " in workspace " << workspaceId);
    return true;
}

bool SurfaceLifecycleManager::start(const OUString& surfaceId)
{
    return transition(surfaceId, SurfaceState::Running, "started");
}

bool SurfaceLifecycleManager::stop(const OUString& surfaceId)
{
    return transition(surfaceId, SurfaceState::Reaped, "stopped");
}

bool SurfaceLifecycleManager::reap(const OUString& surfaceId)
{
    return transition(surfaceId, SurfaceState::Reaped, "reaped");
}

SurfaceState SurfaceLifecycleManager::state(const OUString& surfaceId)
{
    osl::MutexGuard guard(m_mutex);

    for (const auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            return info.state;
        }
    }
    return SurfaceState::Reaped; // treat unknown as reaped
}

SurfaceInfo SurfaceLifecycleManager::info(const OUString& surfaceId)
{
    osl::MutexGuard guard(m_mutex);

    for (const auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            return info;
        }
    }
    return SurfaceInfo{};
}

std::vector<SurfaceInfo> SurfaceLifecycleManager::listByState(SurfaceState filter)
{
    osl::MutexGuard guard(m_mutex);

    std::vector<SurfaceInfo> result;
    for (const auto& info : m_surfaces)
    {
        if (info.state == filter)
        {
            result.push_back(info);
        }
    }
    return result;
}

std::vector<SurfaceInfo> SurfaceLifecycleManager::listByWorkspace(const OUString& workspaceId)
{
    osl::MutexGuard guard(m_mutex);

    std::vector<SurfaceInfo> result;
    for (const auto& info : m_surfaces)
    {
        if (info.workspaceId == workspaceId)
        {
            result.push_back(info);
        }
    }
    return result;
}

bool SurfaceLifecycleManager::updateMetrics(const OUString& surfaceId, const SurfaceMetrics& m)
{
    osl::MutexGuard guard(m_mutex);

    for (auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            info.metrics = m;
            return true;
        }
    }
    return false;
}

SurfaceMetrics SurfaceLifecycleManager::metrics(const OUString& surfaceId)
{
    osl::MutexGuard guard(m_mutex);

    for (const auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            return info.metrics;
        }
    }
    return SurfaceMetrics{};
}

bool SurfaceLifecycleManager::isHealthy(const OUString& surfaceId)
{
    osl::MutexGuard guard(m_mutex);

    for (const auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            SurfaceState s = info.state;
            return s != SurfaceState::Crashed
                && s != SurfaceState::Hung
                && s != SurfaceState::Reaped;
        }
    }
    return false;
}

std::vector<SurfaceInfo> SurfaceLifecycleManager::hungSurfaces(sal_Int64 noOutputThresholdMs)
{
    osl::MutexGuard guard(m_mutex);

    std::vector<SurfaceInfo> result;
    sal_Int64 now = currentTimeMs();

    for (const auto& info : m_surfaces)
    {
        // Only check running/busy surfaces
        if (info.state != SurfaceState::Running
         && info.state != SurfaceState::Busy
         && info.state != SurfaceState::Blocked)
        {
            continue;
        }

        sal_Int64 noOutputMs = now - info.metrics.lastOutputTimeMs;
        if (noOutputMs > noOutputThresholdMs)
        {
            result.push_back(info);
        }
    }
    return result;
}

std::vector<SurfaceInfo> SurfaceLifecycleManager::crashedSurfaces()
{
    return listByState(SurfaceState::Crashed);
}

bool SurfaceLifecycleManager::sample(const OUString& surfaceId)
{
    // In a real implementation, this would collect diagnostic data
    // from the surface process (stack trace, memory map, etc.)
    SAL_INFO("kqoffice.ai.control",
        "SurfaceLifecycleManager: collecting diagnostic sample for " << surfaceId);
    return true;
}

bool SurfaceLifecycleManager::detach(const OUString& surfaceId)
{
    osl::MutexGuard guard(m_mutex);

    for (auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            info.workspaceId.clear();
            SAL_INFO("kqoffice.ai.control",
                "SurfaceLifecycleManager: detached surface " << surfaceId);
            return true;
        }
    }
    return false;
}

bool SurfaceLifecycleManager::restart(const OUString& surfaceId)
{
    // Restart: if running, stop first, then start again
    SurfaceState s = state(surfaceId);
    if (s == SurfaceState::Reaped)
    {
        return false;
    }

    // Force to Created state, then start
    osl::MutexGuard guard(m_mutex);
    for (auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            info.state = SurfaceState::Created;
            info.stateEnteredMs = currentTimeMs();
            info.stateReason = "restarted";
            SAL_INFO("kqoffice.ai.control",
                "SurfaceLifecycleManager: restarted surface " << surfaceId);
            return true;
        }
    }
    return false;
}

bool SurfaceLifecycleManager::kill(const OUString& surfaceId)
{
    // Kill: force transition to Crashed, then reap
    osl::MutexGuard guard(m_mutex);

    for (auto& info : m_surfaces)
    {
        if (info.surfaceId == surfaceId)
        {
            info.state = SurfaceState::Crashed;
            info.stateEnteredMs = currentTimeMs();
            info.stateReason = "killed";
            SAL_INFO("kqoffice.ai.control",
                "SurfaceLifecycleManager: killed surface " << surfaceId);
            return true;
        }
    }
    return false;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */