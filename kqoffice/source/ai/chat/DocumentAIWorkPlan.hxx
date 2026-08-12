/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Work Plan gate (Grok Build plan-mode analogue for document quality).
 *
 * Large / ambiguous edit tasks: present a structured plan → human confirm →
 * then generate write-back proposals. Main document still requires ApplyPlan
 * approval after generation. Local-only; no upload.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIWORKPLAN_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIWORKPLAN_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// User action while a work plan is pending confirmation.
enum class WorkPlanAction
{
    None, ///< not a plan-gate command
    Approve, ///< 按此计划执行 / /approve-plan
    Revise, ///< 修改计划：… / /revise-plan …
    Cancel, ///< 取消计划 / /cancel-plan
    Show ///< /view-plan — redisplay only
};

/// Structured work plan (before ApplyPlan write-back staging).
struct WorkPlan
{
    OUString planId; ///< wp-<n>
    OUString objective;
    OUString surface; ///< writer|calc|impress|any
    OUString approach; ///< multi-line steps
    OUString scopeIn;
    OUString scopeOut;
    OUString risks;
    OUString acceptance;
    OUString skillId; ///< optional matched quality skill
    OUString skillTitleZh;
    OUString originalPrompt; ///< user utterance that created the plan
    OUString executionSeed; ///< skill-expanded / prepared prompt for post-approve run
    OUString reviseNotes; ///< latest user revision notes
    OUString markdown; ///< full user-visible card
    sal_Int32 version = 1;
};

struct WorkPlanInput
{
    OUString userPrompt; ///< raw or prepared
    OUString surface;
    bool hasSelection = false;
    sal_Int32 selectionChars = 0;
    OUString skillId;
    OUString skillTitleZh;
    OUString skillDescription;
    OUString bootstrapObjective; ///< from TaskBootstrap if available
    OUString forcedCapability;
    bool agentCheckbox = false;
    /// Prior plan when revising.
    const WorkPlan* prior = nullptr;
    OUString reviseNotes;
};

class SAL_DLLPUBLIC_EXPORT DocumentAIWorkPlan
{
public:
    /// True when task should pause for plan confirmation (or user forced /plan).
    /// Delegates to FactRouter (measured facts; model confidence not consulted).
    static bool looksLikeLargeTask(const OUString& rPrompt, const OUString& rSurface,
                                   bool bHasSelection, bool bAgentCheckbox,
                                   const OUString& rForcedCap);

    /// Prefer multi-round document-tools without a full work-plan card.
    static bool prefersBoundedLoop(const OUString& rPrompt, const OUString& rSurface,
                                   bool bHasSelection, sal_Int32 nSelectionChars,
                                   bool bAgentCheckbox, const OUString& rForcedCap);

    /// User forced plan mode (/plan, 先规划, 先出计划…).
    static bool looksLikeForcePlan(const OUString& rPrompt);

    /// Classify plan-gate command (approve / revise / cancel / show).
    static WorkPlanAction classifyAction(const OUString& rPrompt);

    /// Notes after 修改计划： or /revise-plan .
    static OUString extractReviseNotes(const OUString& rPrompt);

    /// Strip leading /plan or 先规划 markers for cleaner objective text.
    static OUString stripPlanForcePrefix(const OUString& rPrompt);

    /// Build rule-based plan (no network). Fills markdown.
    static WorkPlan build(const WorkPlanInput& rIn);

    /// Re-render markdown from fields (after revise).
    static OUString formatMarkdown(const WorkPlan& rPlan);

    /// Prepend approved-plan contract to the execution prompt for the model.
    static OUString applyContractToPrompt(const OUString& rWorkPrompt, const WorkPlan& rPlan);

    /// Short status chip label (≤42 chars friendly).
    static OUString chipLabelZh(const WorkPlan& rPlan, bool bApproved);

    /// Numbered approach steps for the unified step bar (max 12).
    static std::vector<OUString> approachStepTitles(const WorkPlan& rPlan);

    /// Step bar line: 计划待确认 · 1/4 · …
    static OUString stepBarZh(const WorkPlan& rPlan, bool bApproved, sal_Int32 currentStep = 0);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
