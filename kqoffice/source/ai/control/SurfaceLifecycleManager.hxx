/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M4: Control Plane — Surface Lifecycle Manager).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * 8-state surface lifecycle machine inspired by imux design principles.
 * State transitions follow a strict DAG: Created→Running, Running→{Idle,Busy,
 * Blocked,Hung,Crashed}, all states→Reaped (terminal).
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_SURFACELIFECYCLEMANAGER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_SURFACELIFECYCLEMANAGER_HXX

#include <osl/mutex.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

enum class SurfaceState
{
    Created,  // 已创建 — initialized but not yet started
    Running,  // 运行中 — actively executing
    Idle,     // 空闲 — running but no active work
    Busy,     // 忙碌 — processing a task
    Blocked,  // 阻塞 — waiting on dependency
    Hung,     // 挂起 — no output for threshold period
    Crashed,  // 崩溃 — terminated unexpectedly
    Reaped    // 已回收 — cleaned up after termination (terminal)
};

struct SurfaceMetrics
{
    sal_Int64  lastInputTimeMs   = 0;
    sal_Int64  lastOutputTimeMs  = 0;
    double     cpuPercent        = 0.0;
    sal_Int64  rssMb             = 0;
    sal_Int32  childProcessCount = 0;
    sal_Int64  bytesIn           = 0;
    sal_Int64  bytesOut          = 0;
    OUString   currentCommand;
    sal_Int32  exitCode          = 0;
    sal_Int64  heartbeatMs       = 0;
};

struct SurfaceInfo
{
    OUString       surfaceId;
    OUString       workspaceId;
    SurfaceState   state           = SurfaceState::Created;
    SurfaceMetrics metrics;
    sal_Int64      stateEnteredMs  = 0;
    OUString       stateReason; // why we entered this state
};

class SurfaceLifecycleManager
{
public:
    SurfaceLifecycleManager() = default;

    // State transitions
    bool transition(const OUString& surfaceId, SurfaceState newState,
                    const OUString& reason);
    bool create(const OUString& surfaceId, const OUString& workspaceId);
    bool start(const OUString& surfaceId);
    bool stop(const OUString& surfaceId);
    bool reap(const OUString& surfaceId);

    // State query
    SurfaceState              state(const OUString& surfaceId);
    SurfaceInfo               info(const OUString& surfaceId);
    std::vector<SurfaceInfo>  listByState(SurfaceState filter);
    std::vector<SurfaceInfo>  listByWorkspace(const OUString& workspaceId);

    // Metrics
    bool           updateMetrics(const OUString& surfaceId, const SurfaceMetrics& m);
    SurfaceMetrics metrics(const OUString& surfaceId);

    // Health
    bool                   isHealthy(const OUString& surfaceId);
    std::vector<SurfaceInfo> hungSurfaces(sal_Int64 noOutputThresholdMs = 180000);
    std::vector<SurfaceInfo> crashedSurfaces();

    // Actions
    bool sample(const OUString& surfaceId);  // get diagnostic sample
    bool detach(const OUString& surfaceId);  // detach from workspace
    bool restart(const OUString& surfaceId);
    bool kill(const OUString& surfaceId);

private:
    bool isValidTransition(SurfaceState from, SurfaceState to) const;
    bool isTerminal(SurfaceState s) const;

    std::vector<SurfaceInfo> m_surfaces;
    osl::Mutex               m_mutex;
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */