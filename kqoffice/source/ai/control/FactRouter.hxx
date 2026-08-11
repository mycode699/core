/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: fact-based route).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Measured facts decide Direct / BoundedLoop / PlanGate — model confidence
 * never alone opens expensive paths (AutoHarness router spirit).
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_FACTROUTER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_FACTROUTER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::control
{

/// Execution shape for one user objective (office-adapted, not coding swarm).
enum class RouteShape : sal_uInt8
{
    Direct = 0, ///< Single-shot consult/edit proposal (still needs write-back approval)
    BoundedLoop, ///< Multi-round document-tools / light loop; no work-plan card
    PlanGate, ///< Pause for structured WorkPlan confirmation
};

struct TaskFacts
{
    sal_Int32 promptChars = 0;
    /// Whitespace-ish token estimate (CJK: chars/2 floor).
    sal_Int32 approxWords = 0;
    sal_Int32 selectionChars = 0;
    bool hasSelection = false;
    bool hasExplicitCheck = false; ///< user named 验收/dry-run/校验
    bool multiStepLanguage = false;
    bool fullDocLanguage = false;
    bool pureQa = false;
    bool continueTask = false;
    bool forcePlan = false;
    bool agentCheckbox = false;
    bool agentOrPlanCapability = false;
    sal_Int32 editVerbCount = 0;
    bool vagueLongEdit = false;
    bool selectionEditLong = false;
    OUString surface; ///< writer|calc|impress|…
    OUString forcedCapability;
};

struct FactRouteInput
{
    OUString prompt;
    OUString surface;
    bool hasSelection = false;
    sal_Int32 selectionChars = 0;
    bool agentCheckbox = false;
    OUString forcedCapability;
    bool forcePlanOnce = false;
    /// Optional: already classified pure-QA / continue (from TaskBootstrap).
    bool knownPureQa = false;
    bool knownContinue = false;
};

struct FactRouteDecision
{
    RouteShape shape = RouteShape::Direct;
    TaskFacts facts;
    bool needsWorkPlan = false;
    bool preferMultiRoundTools = false;
    bool needsComplexStartConfirm = false;
    OUString shapeId; ///< direct | bounded-loop | plan-gate
    OUString reasonZh;
    OUString reasonCode; ///< stable machine code
};

class SAL_DLLPUBLIC_EXPORT FactRouter
{
public:
    static TaskFacts measure(const FactRouteInput& in);
    static FactRouteDecision route(const FactRouteInput& in);

    static OUString shapeId(RouteShape s);
    static OUString shapeLabelZh(RouteShape s);

    /// Defaults (tunable dials — ceilings fall only via EvolutionGuard later).
    static sal_Int32 directMaxChars();
    static sal_Int32 planMinChars();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
