/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: unified turn phases).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Single phase machine for: plan → generate → await apply → applied/failed.
 * "Ready" only when Idle or Awaiting* (user can act); never Ready while tools open.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_WORKBENCHPHASE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_WORKBENCHPHASE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

enum class WorkbenchPhase : sal_uInt8
{
    Idle = 0,
    Planning, ///< Work plan pending user confirm
    Generating, ///< LLM / tools streaming
    ToolsOpen, ///< Multi-round tools; not Ready
    AwaitingApply, ///< ApplyPlan staged; needs human approve
    Applying, ///< Write-back in progress
    Applied,
    Failed,
    Cancelled,
};

struct WorkbenchStep
{
    sal_Int32 index = 0; ///< 1-based for display
    OUString titleZh;
    bool done = false;
    bool current = false;
};

struct WorkbenchSnapshot
{
    WorkbenchPhase phase = WorkbenchPhase::Idle;
    OUString workPlanId;
    OUString applyPlanId;
    sal_Int32 stepIndex = 0; ///< 0 = none
    sal_Int32 stepTotal = 0;
    std::vector<WorkbenchStep> steps;
    bool toolsOpen = false;
    bool streamOpen = false;
    /// True when UI may show Ready / accept new primary send without queue.
    bool readyForPrimaryInput = true;
    OUString stepBarZh;
    OUString phaseLabelZh;
};

/// Pure helpers for phase transitions + step bar copy.
class SAL_DLLPUBLIC_EXPORT WorkbenchPhaseMachine
{
public:
    static OUString phaseId(WorkbenchPhase p);
    static OUString phaseLabelZh(WorkbenchPhase p);

    /// Parse approach / markdown lines into ordered steps (max 12).
    static std::vector<WorkbenchStep> parseStepsFromApproach(const OUString& approachMarkdown);

    /// Build snapshot for step bar.
    static WorkbenchSnapshot makeSnapshot(WorkbenchPhase phase, const OUString& workPlanId,
                                          const OUString& applyPlanId,
                                          const std::vector<WorkbenchStep>& steps,
                                          sal_Int32 currentStepIndex, bool streamOpen,
                                          bool toolsOpen);

    /// Legal transition? Fail-closed for unknown edges (caller may still force).
    static bool canTransition(WorkbenchPhase from, WorkbenchPhase to);

    /// Apply transition; returns new phase (or from if illegal and bStrict).
    static WorkbenchPhase transition(WorkbenchPhase from, WorkbenchPhase to, bool bStrict = false);

    /// Ready means user can start a *primary* turn without enqueue (Idle / terminal / plan wait).
    static bool isReadyForPrimaryInput(WorkbenchPhase p, bool streamOpen, bool toolsOpen);

    /// Busy for composer: stream or tools or applying.
    static bool isTurnBusy(WorkbenchPhase p, bool streamOpen, bool toolsOpen);
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
