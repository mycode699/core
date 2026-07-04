/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M4: Control Plane — Resource Budget Watchdog).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Watches per-surface resource consumption and enforces budget limits.
 * Escalation policy: None → Warn → Throttle → Detach → Suspend → Kill.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_RESOURCEBUDGETWATCHDOG_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_RESOURCEBUDGETWATCHDOG_HXX

#include <osl/mutex.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <map>
#include <vector>

namespace kqoffice::ai::control
{

struct ResourceBudget
{
    double   maxCpuPercent     = 80.0;
    sal_Int64 maxRssMb         = 4096;
    sal_Int32 maxProcesses     = 8;
    sal_Int64 maxOutputBytes   = 100 * 1024 * 1024; // 100 MB
    sal_Int64 maxIdleBusyTimeMs = 600000;           // 10 min
    sal_Int64 maxRuntimeMs     = 3600000;            // 1 hour
    sal_Int64 maxNoOutputTimeMs = 180000;            // 3 min
};

enum class BudgetAction
{
    None,     // 无操作
    Warn,     // 警告
    Throttle, // 限流
    Detach,   // 分离
    Suspend,  // 暂停
    Kill      // 终止
};

struct BudgetViolation
{
    OUString    surfaceId;
    OUString    resource; // "cpu", "rss", "processes", "output", "runtime", "no_output", "idle_busy"
    double      currentValue = 0.0;
    double      limit        = 0.0;
    BudgetAction recommendedAction = BudgetAction::None;
    OUString    message;
};

class ResourceBudgetWatchdog
{
public:
    ResourceBudgetWatchdog() = default;

    void setBudget(const OUString& surfaceId, const ResourceBudget& budget);
    void setGlobalBudget(const ResourceBudget& budget);
    ResourceBudget budget(const OUString& surfaceId) const;

    BudgetViolation              check(const OUString& surfaceId);
    std::vector<BudgetViolation> checkAll();

    BudgetAction escalate(const BudgetViolation& violation);
    bool        enforce(const OUString& surfaceId, BudgetAction action);

private:
    ResourceBudget             m_globalBudget;
    std::map<OUString, ResourceBudget> m_perSurfaceBudgets;
    mutable osl::Mutex         m_mutex;
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */