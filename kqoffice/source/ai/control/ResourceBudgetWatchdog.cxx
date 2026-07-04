/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈 office project (V4 M4: Control Plane — Resource Budget Watchdog).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ResourceBudgetWatchdog.hxx"

#include <sal/log.hxx>

namespace kqoffice::ai::control
{

void ResourceBudgetWatchdog::setBudget(const OUString& surfaceId, const ResourceBudget& budget)
{
    osl::MutexGuard guard(m_mutex);
    m_perSurfaceBudgets[surfaceId] = budget;
    SAL_INFO("kqoffice.ai.control",
        "ResourceBudgetWatchdog: set per-surface budget for " << surfaceId);
}

void ResourceBudgetWatchdog::setGlobalBudget(const ResourceBudget& budget)
{
    osl::MutexGuard guard(m_mutex);
    m_globalBudget = budget;
    SAL_INFO("kqoffice.ai.control",
        "ResourceBudgetWatchdog: set global budget");
}

ResourceBudget ResourceBudgetWatchdog::budget(const OUString& surfaceId) const
{
    osl::MutexGuard guard(m_mutex);

    auto it = m_perSurfaceBudgets.find(surfaceId);
    if (it != m_perSurfaceBudgets.end())
    {
        return it->second;
    }
    return m_globalBudget;
}

BudgetViolation ResourceBudgetWatchdog::check(const OUString& surfaceId)
{
    // Note: In real implementation, this would query actual metrics
    // from SurfaceLifecycleManager. For now, we return empty violation.
    BudgetViolation violation;
    violation.surfaceId = surfaceId;
    return violation;
}

std::vector<BudgetViolation> ResourceBudgetWatchdog::checkAll()
{
    std::vector<BudgetViolation> violations;
    // In real implementation, iterate all surfaces and check budgets
    return violations;
}

BudgetAction ResourceBudgetWatchdog::escalate(const BudgetViolation& violation)
{
    if (violation.recommendedAction != BudgetAction::None)
    {
        return violation.recommendedAction;
    }

    // Default escalation based on resource type
    if (violation.resource == "cpu")
    {
        if (violation.currentValue > violation.limit * 1.5)
            return BudgetAction::Kill;
        if (violation.currentValue > violation.limit * 1.2)
            return BudgetAction::Suspend;
        if (violation.currentValue > violation.limit * 1.1)
            return BudgetAction::Throttle;
        return BudgetAction::Warn;
    }

    if (violation.resource == "rss")
    {
        if (violation.currentValue > violation.limit * 1.5)
            return BudgetAction::Kill;
        if (violation.currentValue > violation.limit * 1.2)
            return BudgetAction::Suspend;
        return BudgetAction::Warn;
    }

    if (violation.resource == "processes")
    {
        if (violation.currentValue > violation.limit * 2)
            return BudgetAction::Kill;
        return BudgetAction::Throttle;
    }

    if (violation.resource == "runtime" || violation.resource == "no_output")
    {
        return BudgetAction::Kill;
    }

    return BudgetAction::Warn;
}

bool ResourceBudgetWatchdog::enforce(const OUString& surfaceId, BudgetAction action)
{
    switch (action)
    {
        case BudgetAction::None:
            SAL_INFO("kqoffice.ai.control",
                "ResourceBudgetWatchdog: no enforcement needed for " << surfaceId);
            return true;

        case BudgetAction::Warn:
            SAL_WARN("kqoffice.ai.control",
                "ResourceBudgetWatchdog: budget warning for " << surfaceId);
            return true;

        case BudgetAction::Throttle:
            SAL_WARN("kqoffice.ai.control",
                "ResourceBudgetWatchdog: throttling " << surfaceId);
            // In real implementation: reduce CPU priority, limit concurrency
            return true;

        case BudgetAction::Detach:
            SAL_WARN("kqoffice.ai.control",
                "ResourceBudgetWatchdog: detaching " << surfaceId);
            // In real implementation: detach from workspace
            return true;

        case BudgetAction::Suspend:
            SAL_WARN("kqoffice.ai.control",
                "ResourceBudgetWatchdog: suspending " << surfaceId);
            // In real implementation: SIGSTOP or equivalent
            return true;

        case BudgetAction::Kill:
            SAL_WARN("kqoffice.ai.control",
                "ResourceBudgetWatchdog: killing " << surfaceId);
            // In real implementation: SIGKILL or equivalent
            return true;
    }
    return false;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */