/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈 office project (V4 M4: Control Plane — Resource Budget Watchdog).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ResourceBudgetWatchdog.hxx"
#include "AiResourceEnvelope.hxx"

#include <sal/log.hxx>

#if defined(MACOSX) || defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace kqoffice::ai::control
{
namespace
{
sal_Int64 ProcessMaxRssMb()
{
#if defined(MACOSX) || defined(__APPLE__)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    return static_cast<sal_Int64>(ru.ru_maxrss) / (1024 * 1024);
#elif defined(__linux__)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    return static_cast<sal_Int64>(ru.ru_maxrss) / 1024;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!::GetProcessMemoryInfo(::GetCurrentProcess(),
                                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return 0;
    return static_cast<sal_Int64>(pmc.WorkingSetSize) / (1024 * 1024);
#else
    return 0;
#endif
}
} // namespace

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
    BudgetViolation violation;
    violation.surfaceId = surfaceId;

    const ResourceBudget b = budget(surfaceId);
    const sal_Int64 rssMb = ProcessMaxRssMb();
    if (rssMb > 0 && b.maxRssMb > 0 && rssMb > b.maxRssMb)
    {
        violation.resource = u"rss"_ustr;
        violation.currentValue = static_cast<double>(rssMb);
        violation.limit = static_cast<double>(b.maxRssMb);
        violation.recommendedAction = escalate(violation);
        violation.message = u"process rss "_ustr + OUString::number(rssMb) + u"MB > budget "_ustr
                            + OUString::number(b.maxRssMb) + u"MB"_ustr;
        return violation;
    }

    // Soft AI envelope pressure — warn only (do not kill desktop).
    if (AiResourceEnvelope::underSoftMemoryPressure())
    {
        violation.resource = u"rss"_ustr;
        violation.currentValue = static_cast<double>(rssMb);
        violation.limit = static_cast<double>(AiResourceEnvelope::softRssBudgetMb());
        violation.recommendedAction = BudgetAction::Warn;
        violation.message = u"soft AI memory pressure — background polish deferred"_ustr;
    }
    return violation;
}

std::vector<BudgetViolation> ResourceBudgetWatchdog::checkAll()
{
    std::vector<BudgetViolation> violations;
    const BudgetViolation v = check(u"process"_ustr);
    if (v.recommendedAction != BudgetAction::None || !v.message.isEmpty())
        violations.push_back(v);
    return violations;
}

BudgetAction ResourceBudgetWatchdog::escalate(const BudgetViolation& violation)
{
    if (violation.recommendedAction != BudgetAction::None)
    {
        return violation.recommendedAction;
    }

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
        // Never auto-kill the whole office process from soft RSS; prefer throttle/warn.
        if (violation.currentValue > violation.limit * 1.5)
            return BudgetAction::Throttle;
        if (violation.currentValue > violation.limit * 1.2)
            return BudgetAction::Throttle;
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
                "ResourceBudgetWatchdog: throttling " << surfaceId
                << " (soft — defer AI polish / shrink batches via envelope)");
            return true;

        case BudgetAction::Detach:
            SAL_WARN("kqoffice.ai.control",
                "ResourceBudgetWatchdog: detaching " << surfaceId);
            return true;

        case BudgetAction::Suspend:
            SAL_WARN("kqoffice.ai.control",
                "ResourceBudgetWatchdog: suspending " << surfaceId);
            return true;

        case BudgetAction::Kill:
            SAL_WARN("kqoffice.ai.control",
                "ResourceBudgetWatchdog: kill requested for " << surfaceId
                << " — demoted to throttle (protect host)");
            return enforce(surfaceId, BudgetAction::Throttle);
    }
    return false;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
