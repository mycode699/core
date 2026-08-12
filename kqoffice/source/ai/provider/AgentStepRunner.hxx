/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Plan → Act → Review step runner that calls Provider with five-slot
 * capabilities (plan / agent|chat / review). Does not mutate documents;
 * callers stage ApplyPlan and wait for human approval.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_AGENTSTEPRUNNER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_AGENTSTEPRUNNER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai
{

/// One provider-backed agent step.
struct AgentStepResult
{
    OUString stepKind; // "plan" | "act" | "review" | custom role
    OUString capability; // Provider capability used
    OUString status; // "ok" | "policy-denied" | "provider-error" | …
    OUString content;
    OUString evidenceId;
    OUString modelHint; // best-effort label from evidence/provider path
    sal_Int32 durationMs = 0;
};

/// Full pipeline result (still no document mutation).
struct AgentPipelineResult
{
    bool success = false;
    OUString goal;
    std::vector<AgentStepResult> steps;
    /// Concatenated user-visible transcript (plan + act + review).
    OUString combinedContent;
    /// Last successful content suitable for ApplyPlan extraction.
    OUString applyCandidateContent;
    OUString finalEvidenceId;
    OUString failureReason;
};

class SAL_DLLPUBLIC_EXPORT AgentStepRunner
{
public:
    /// Map cowork / planner agentRole tokens onto Provider capabilities.
    /// planner/outline → plan; reviewer/judge → review; others → agent.
    static OUString capabilityForAgentRole(const OUString& rAgentRole);

    /// Single Provider call with the given capability + prompt.
    static AgentStepResult runOne(const OUString& rCapability, const OUString& rPrompt,
                                  sal_Int32 nTimeoutMs = 60000);

    /// Plan (plan slot) → Act (agent slot) → Review (review slot).
    /// Never applies patches; combinedContent is for UI + stage-only apply.
    static AgentPipelineResult runPlanActReview(const OUString& rGoal,
                                                const OUString& rContext = OUString());

    /// Run ordered sub-agent roles (e.g. from AgentTaskDelegation) via Provider.
    /// Each role maps through capabilityForAgentRole. Stops on hard failure
    /// unless rContinueOnError is true.
    static AgentPipelineResult runRoleSequence(
        const OUString& rGoal, const std::vector<OUString>& rAgentRoles,
        const std::vector<OUString>& rInstructions, bool bContinueOnError = false);

    /// Ceiling Agent Mode: continuous bind→plan→act→review→verify (no human pause).
    /// rSurface: writer|calc|impress|none — shapes act prompt write-back markers.
    /// rDocToolsContext: skeleton / TOOL_RESULT text (may be empty).
    /// Still never mutates documents; applyCandidateContent is for stage-only ApplyPlan.
    static AgentPipelineResult runCeilingMode(const OUString& rGoal, const OUString& rContext,
                                              const OUString& rSurface = OUString(),
                                              const OUString& rDocToolsContext = OUString());

    /// Human-gate phase 1: bind + plan only. success when plan ok.
    /// failureReason "awaiting-continue" marks plan ready for Continue button.
    static AgentPipelineResult runCeilingPlanPhase(const OUString& rGoal,
                                                   const OUString& rContext = OUString(),
                                                   const OUString& rSurface = OUString(),
                                                   const OUString& rDocToolsContext = OUString());

    /// Human-gate phase 2: act → review → local verify using plan from phase 1.
    static AgentPipelineResult runCeilingExecutePhase(
        const OUString& rGoal, const OUString& rPlanContent, const OUString& rContext = OUString(),
        const OUString& rSurface = OUString(), const OUString& rDocToolsContext = OUString());

    /// Q3 Calc Agent Mode: probe → clean → aggregate → chart-suggest (stage only).
    /// Uses calc-specialized prompts + formula sandbox step. Never mutates sheet.
    static AgentPipelineResult runCalcAgentMode(const OUString& rGoal,
                                                const OUString& rContext = OUString(),
                                                const OUString& rDocToolsContext = OUString());

    /// Parse plan markdown into short step titles for the Agent tree UI (max 8).
    static std::vector<OUString> parsePlanStepTitles(const OUString& rPlanMarkdown);
};

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
