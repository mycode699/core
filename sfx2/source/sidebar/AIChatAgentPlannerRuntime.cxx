/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent planner runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatAgentPlannerRuntime.hxx"

#include "AIChatKnowledgeIndexStore.hxx"

#include <algorithm>

namespace sfx2::sidebar
{
namespace
{
bool IsLowerHex(const OUString& rValue, sal_Int32 nLength)
{
    if (rValue.getLength() != nLength)
        return false;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    }
    return true;
}

bool ContainsString(const std::vector<OUString>& rValues, const OUString& rNeedle)
{
    return std::find(rValues.begin(), rValues.end(), rNeedle) != rValues.end();
}

bool ContainsDuplicateDependency(const std::vector<sal_Int32>& rDependencies)
{
    for (auto it = rDependencies.begin(); it != rDependencies.end(); ++it)
    {
        if (std::find(it + 1, rDependencies.end(), *it) != rDependencies.end())
            return true;
    }
    return false;
}

bool ContainsDuplicateEvidence(const std::vector<OUString>& rEvidence)
{
    for (auto it = rEvidence.begin(); it != rEvidence.end(); ++it)
    {
        if (std::find(it + 1, rEvidence.end(), *it) != rEvidence.end())
            return true;
    }
    return false;
}

bool IsAllowedEvidence(const OUString& rEvidence)
{
    return rEvidence == u"provider-call"_ustr || rEvidence == u"connector-fetch"_ustr
           || rEvidence == u"kb-query"_ustr || rEvidence == u"policy-decision"_ustr
           || rEvidence == u"user-approval"_ustr || rEvidence == u"shadow-doc-diff"_ustr
           || rEvidence == u"apply-plan-runtime-validated"_ustr;
}

OUString MakeFailureMessage(const OUString& rReason)
{
    return u"agent-plan-invalid reason="_ustr + rReason
           + u" schema-validated=false fail-closed-user-visible=true blocks-execution=true"_ustr
           + u" invalid-plan-evidence=required auto-retry=false auto-simplification=false"_ustr
           + u" step-execution=false actor-runtime=false observer-runtime=false"_ustr
           + u" main-document-mutation=false public-egress=false"_ustr;
}
}

AIChatAgentPlannerPolicy
AIChatAgentPlannerRuntime::MakeDefaultPolicy(const OUString& rApprovalMode)
{
    AIChatAgentPlannerPolicy aPolicy;
    aPolicy.ApprovalMode = rApprovalMode;
    aPolicy.SelectedModeSource = rApprovalMode == u"per-step"_ustr
                                     ? u"explicit-user-choice"_ustr
                                     : u"default-whole-task"_ustr;
    aPolicy.PromptStrategy = rApprovalMode == u"per-step"_ustr
                                 ? u"explicit-per-step-review"_ustr
                                 : u"final-review-only"_ustr;
    aPolicy.PerStepRequiresExplicitUserChoice = true;
    aPolicy.ImplicitPerStepPromptsAllowed = false;
    aPolicy.ReviewStepRequired = true;
    aPolicy.ApprovalEvidence = u"user-approval"_ustr;
    aPolicy.RuntimeApprovalUiImplementation = u"not-started"_ustr;

    aPolicy.CrossSessionResumeAllowed = true;
    aPolicy.ResumePoint = u"evidence-complete-checkpoint"_ustr;
    aPolicy.RequiresUserConfirmation = true;
    aPolicy.RequiresDocumentHashMatch = true;
    aPolicy.RequiresShadowSnapshot = true;
    aPolicy.RequiresAuditReplay = true;
    aPolicy.AutoResumeAllowed = false;
    aPolicy.StaleCheckpointBehavior = u"fail-closed-user-visible"_ustr;
    aPolicy.CheckpointEvidence = u"required"_ustr;
    aPolicy.RuntimeResumeImplementation = u"not-started"_ustr;

    aPolicy.DefaultServiceMode = u"private"_ustr;
    aPolicy.AllowPublicEgress = false;
    aPolicy.RequiresPolicyPreflight = true;

    aPolicy.SandboxMode = u"shadow-doc"_ustr;
    aPolicy.MergeTarget = u"main-doc"_ustr;
    aPolicy.FailureIsolation = u"discard-step-branch"_ustr;

    aPolicy.ShadowDocMode = u"per-step-compatible-branch"_ustr;
    aPolicy.WriterCompatibilityTarget = u"v2-w3-swdocshell"_ustr;
    aPolicy.RequiresExistingSwDocShellCompatibility = true;
    aPolicy.CreatesNewDocShellType = false;
    aPolicy.MergePath = u"v2-apply-plan-runtime"_ustr;
    aPolicy.MainDocMutationBeforeApprovalAllowed = false;
    aPolicy.MergeRequiresApproval = true;
    aPolicy.RuntimeShadowDocImplementation = u"not-started"_ustr;

    aPolicy.GraphType = u"forward-only-dag"_ustr;
    aPolicy.ExecutionOrder = u"topological-index"_ustr;
    aPolicy.AllowsFanIn = true;
    aPolicy.AllowsFanOut = true;
    aPolicy.AllowsCycles = false;
    aPolicy.AllowsFutureDependencies = false;
    aPolicy.AllowsParallelRuntime = false;
    aPolicy.RuntimeSchedulerImplementation = u"not-started"_ustr;

    aPolicy.ValidationPhase = u"before-execution"_ustr;
    aPolicy.OnInvalidPlan = u"fail-closed-user-visible"_ustr;
    aPolicy.BlocksExecution = true;
    aPolicy.AutoRetryAllowed = false;
    aPolicy.AutoSimplificationAllowed = false;
    aPolicy.UserRetryAllowed = true;
    aPolicy.InvalidPlanEvidence = u"required"_ustr;
    aPolicy.RuntimePlannerImplementation = u"not-started"_ustr;

    aPolicy.PromptSetId = u"w6-plan-act-observe-v1"_ustr;
    aPolicy.PromptSetVersion = u"v1"_ustr;
    aPolicy.PlannerPromptId = u"planner-v1"_ustr;
    aPolicy.ActorPromptId = u"actor-v1"_ustr;
    aPolicy.ObserverPromptId = u"observer-v1"_ustr;
    aPolicy.Temperature = 0;
    aPolicy.TopP = 1;
    aPolicy.SeedRequired = true;
    aPolicy.PublicEgressAllowed = false;
    aPolicy.RuntimePromptExecution = u"not-started"_ustr;

    aPolicy.PatchStepMustUseApplyPlanRuntime = true;
    aPolicy.ParagraphAction = 7;
    aPolicy.CellAction = 5;
    aPolicy.SlideElementAction = 4;
    return aPolicy;
}

bool AIChatAgentPlannerRuntime::IsTaskIdAllowed(const OUString& rTaskId)
{
    return rTaskId.startsWith(u"agt-"_ustr) && IsLowerHex(rTaskId.copy(4), 16);
}

bool AIChatAgentPlannerRuntime::IsOwnerSurfaceAllowed(const OUString& rOwnerSurface)
{
    return rOwnerSurface == u"writer"_ustr || rOwnerSurface == u"calc"_ustr
           || rOwnerSurface == u"impress"_ustr;
}

bool AIChatAgentPlannerRuntime::IsApprovalPolicyAllowed(
    const AIChatAgentPlannerPolicy& rPolicy)
{
    const bool bWholeTask = rPolicy.ApprovalMode == u"whole-task"_ustr
                            && rPolicy.SelectedModeSource == u"default-whole-task"_ustr
                            && rPolicy.PromptStrategy == u"final-review-only"_ustr;
    const bool bPerStep = rPolicy.ApprovalMode == u"per-step"_ustr
                          && rPolicy.SelectedModeSource == u"explicit-user-choice"_ustr
                          && rPolicy.PromptStrategy == u"explicit-per-step-review"_ustr;

    return (bWholeTask || bPerStep) && rPolicy.PerStepRequiresExplicitUserChoice
           && !rPolicy.ImplicitPerStepPromptsAllowed && rPolicy.ReviewStepRequired
           && rPolicy.ApprovalEvidence == u"user-approval"_ustr
           && rPolicy.RuntimeApprovalUiImplementation == u"not-started"_ustr;
}

bool AIChatAgentPlannerRuntime::IsDataBoundaryAllowed(
    const AIChatAgentPlannerPolicy& rPolicy)
{
    return (rPolicy.DefaultServiceMode == u"offline"_ustr
            || rPolicy.DefaultServiceMode == u"private"_ustr)
           && !rPolicy.AllowPublicEgress && rPolicy.RequiresPolicyPreflight
           && !rPolicy.PublicEgressAllowed;
}

bool AIChatAgentPlannerRuntime::IsSandboxAllowed(const AIChatAgentPlannerPolicy& rPolicy)
{
    return rPolicy.SandboxMode == u"shadow-doc"_ustr
           && rPolicy.MergeTarget == u"main-doc"_ustr
           && rPolicy.FailureIsolation == u"discard-step-branch"_ustr;
}

bool AIChatAgentPlannerRuntime::IsShadowDocPolicyAllowed(
    const AIChatAgentPlannerPolicy& rPolicy)
{
    return rPolicy.ShadowDocMode == u"per-step-compatible-branch"_ustr
           && rPolicy.WriterCompatibilityTarget == u"v2-w3-swdocshell"_ustr
           && rPolicy.RequiresExistingSwDocShellCompatibility
           && !rPolicy.CreatesNewDocShellType
           && rPolicy.MergePath == u"v2-apply-plan-runtime"_ustr
           && !rPolicy.MainDocMutationBeforeApprovalAllowed && rPolicy.MergeRequiresApproval
           && rPolicy.RuntimeShadowDocImplementation == u"not-started"_ustr;
}

bool AIChatAgentPlannerRuntime::IsDependencyPolicyAllowed(
    const AIChatAgentPlannerPolicy& rPolicy)
{
    return rPolicy.GraphType == u"forward-only-dag"_ustr
           && rPolicy.ExecutionOrder == u"topological-index"_ustr && rPolicy.AllowsFanIn
           && rPolicy.AllowsFanOut && !rPolicy.AllowsCycles
           && !rPolicy.AllowsFutureDependencies && !rPolicy.AllowsParallelRuntime
           && rPolicy.RuntimeSchedulerImplementation == u"not-started"_ustr;
}

bool AIChatAgentPlannerRuntime::IsPlannerValidationAllowed(
    const AIChatAgentPlannerPolicy& rPolicy)
{
    return rPolicy.ValidationPhase == u"before-execution"_ustr
           && rPolicy.OnInvalidPlan == u"fail-closed-user-visible"_ustr
           && rPolicy.BlocksExecution && !rPolicy.AutoRetryAllowed
           && !rPolicy.AutoSimplificationAllowed && rPolicy.UserRetryAllowed
           && rPolicy.InvalidPlanEvidence == u"required"_ustr
           && rPolicy.RuntimePlannerImplementation == u"not-started"_ustr;
}

bool AIChatAgentPlannerRuntime::IsPromptPolicyAllowed(
    const AIChatAgentPlannerPolicy& rPolicy)
{
    return rPolicy.PromptSetId == u"w6-plan-act-observe-v1"_ustr
           && rPolicy.PromptSetVersion == u"v1"_ustr
           && rPolicy.PlannerPromptId == u"planner-v1"_ustr
           && rPolicy.ActorPromptId == u"actor-v1"_ustr
           && rPolicy.ObserverPromptId == u"observer-v1"_ustr && rPolicy.Temperature == 0
           && rPolicy.TopP == 1 && rPolicy.SeedRequired && !rPolicy.PublicEgressAllowed
           && rPolicy.RuntimePromptExecution == u"not-started"_ustr;
}

bool AIChatAgentPlannerRuntime::IsTokenLockAllowed(
    const AIChatAgentPlannerPolicy& rPolicy)
{
    return rPolicy.PatchStepMustUseApplyPlanRuntime && rPolicy.ParagraphAction == 7
           && rPolicy.CellAction == 5 && rPolicy.SlideElementAction == 4;
}

bool AIChatAgentPlannerRuntime::IsStepKindAllowed(const OUString& rKind)
{
    return rKind == u"fetch"_ustr || rKind == u"query"_ustr || rKind == u"transform"_ustr
           || rKind == u"patch"_ustr || rKind == u"review"_ustr;
}

bool AIChatAgentPlannerRuntime::IsExpectedOutputAllowed(const AIChatAgentPlanStep& rStep)
{
    if (rStep.Kind == u"fetch"_ustr)
        return rStep.ExpectedOutputKind == u"connector-payload"_ustr
               && rStep.ExpectedOutputSchemaRef == u"connector-manifest"_ustr;
    if (rStep.Kind == u"query"_ustr)
        return rStep.ExpectedOutputKind == u"knowledge-chunks"_ustr
               && rStep.ExpectedOutputSchemaRef == u"knowledge-index-result"_ustr;
    if (rStep.Kind == u"transform"_ustr)
        return rStep.ExpectedOutputKind == u"analysis-summary"_ustr
               && rStep.ExpectedOutputSchemaRef == u"agent-step-result"_ustr;
    if (rStep.Kind == u"patch"_ustr)
        return rStep.ExpectedOutputKind == u"apply-plan-runtime"_ustr
               && rStep.ExpectedOutputSchemaRef == u"apply-plan-runtime"_ustr;
    if (rStep.Kind == u"review"_ustr)
        return rStep.ExpectedOutputKind == u"approval-decision"_ustr
               && rStep.ExpectedOutputSchemaRef == u"user-approval"_ustr;
    return false;
}

bool AIChatAgentPlannerRuntime::IsStepEvidenceAllowed(const AIChatAgentPlanStep& rStep)
{
    if (rStep.Evidence.empty() || ContainsDuplicateEvidence(rStep.Evidence)
        || !ContainsString(rStep.Evidence, u"policy-decision"_ustr))
        return false;

    for (const OUString& rEvidence : rStep.Evidence)
    {
        if (!IsAllowedEvidence(rEvidence))
            return false;
    }

    if (rStep.Kind == u"fetch"_ustr)
        return ContainsString(rStep.Evidence, u"connector-fetch"_ustr);
    if (rStep.Kind == u"query"_ustr)
        return ContainsString(rStep.Evidence, u"kb-query"_ustr);
    if (rStep.Kind == u"transform"_ustr)
        return ContainsString(rStep.Evidence, u"provider-call"_ustr);
    if (rStep.Kind == u"patch"_ustr)
        return ContainsString(rStep.Evidence, u"provider-call"_ustr)
               && ContainsString(rStep.Evidence, u"shadow-doc-diff"_ustr)
               && ContainsString(rStep.Evidence, u"apply-plan-runtime-validated"_ustr);
    if (rStep.Kind == u"review"_ustr)
        return ContainsString(rStep.Evidence, u"user-approval"_ustr);
    return false;
}

bool AIChatAgentPlannerRuntime::HasForwardOnlyDag(
    const std::vector<AIChatAgentPlanStep>& rSteps)
{
    for (size_t i = 0; i < rSteps.size(); ++i)
    {
        const AIChatAgentPlanStep& rStep = rSteps[i];
        if (rStep.Index != static_cast<sal_Int32>(i) || ContainsDuplicateDependency(rStep.Dependencies))
            return false;

        for (sal_Int32 nDependency : rStep.Dependencies)
        {
            if (nDependency < 0 || nDependency >= rStep.Index)
                return false;
        }
    }
    return true;
}

bool AIChatAgentPlannerRuntime::HasReviewStep(
    const std::vector<AIChatAgentPlanStep>& rSteps)
{
    return std::any_of(rSteps.begin(), rSteps.end(), [](const AIChatAgentPlanStep& rStep) {
        return rStep.Kind == u"review"_ustr
               && ContainsString(rStep.Evidence, u"user-approval"_ustr);
    });
}

bool AIChatAgentPlannerRuntime::PatchStepsUseApplyPlanEvidence(
    const std::vector<AIChatAgentPlanStep>& rSteps)
{
    return std::all_of(rSteps.begin(), rSteps.end(), [](const AIChatAgentPlanStep& rStep) {
        if (rStep.Kind != u"patch"_ustr)
            return true;
        return rStep.ExpectedOutputSchemaRef == u"apply-plan-runtime"_ustr
               && ContainsString(rStep.Evidence, u"shadow-doc-diff"_ustr)
               && ContainsString(rStep.Evidence, u"apply-plan-runtime-validated"_ustr);
    });
}

bool AIChatAgentPlannerRuntime::EveryStepHasPolicy(
    const std::vector<AIChatAgentPlanStep>& rSteps)
{
    return std::all_of(rSteps.begin(), rSteps.end(), [](const AIChatAgentPlanStep& rStep) {
        return rStep.PolicyPreflight && rStep.PolicyAuditLog;
    });
}

bool AIChatAgentPlannerRuntime::IsPlanShapeAllowed(
    const AIChatAgentPlannerRequest& rRequest)
{
    if (rRequest.SchemaVersion != u"v3-agent-step-plan/0.1"_ustr || rRequest.GoalLength < 8
        || !IsLowerHex(rRequest.GoalHash, 64) || rRequest.MaxSteps < 1
        || rRequest.MaxSteps > 25 || rRequest.Steps.empty()
        || rRequest.Steps.size() > static_cast<size_t>(rRequest.MaxSteps)
        || rRequest.Steps.size() > 25)
        return false;

    for (const AIChatAgentPlanStep& rStep : rRequest.Steps)
    {
        if (!IsStepKindAllowed(rStep.Kind) || !IsExpectedOutputAllowed(rStep)
            || !IsStepEvidenceAllowed(rStep) || !rStep.PolicyPreflight
            || !rStep.PolicyAuditLog || !IsLowerHex(rStep.TitleHash, 64)
            || !IsLowerHex(rStep.DescriptionHash, 64))
            return false;
    }
    return true;
}

OUString AIChatAgentPlannerRuntime::MakePlanId(const AIChatAgentPlannerRequest& rRequest)
{
    return u"agp-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rRequest.TaskId + u":agent-plan:"_ustr
                                                         + rRequest.GoalHash)
                 .copy(0, 16);
}

OUString AIChatAgentPlannerRuntime::MakePlannerEvidenceId(const OUString& rPlanId)
{
    return u"evidence:agent-plan:"_ustr + rPlanId;
}

OUString AIChatAgentPlannerRuntime::MakePlannerHashReference(
    const AIChatAgentPlannerRequest& rRequest)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rRequest.TaskId + u":"_ustr + rRequest.GoalHash + u":"_ustr
                 + OUString::number(static_cast<sal_Int32>(rRequest.Steps.size())));
}

AIChatAgentPlannerResult
AIChatAgentPlannerRuntime::ValidatePlan(const AIChatAgentPlannerRequest& rRequest) const
{
    AIChatAgentPlannerResult aResult;
    aResult.TaskId = rRequest.TaskId;
    aResult.StepCount = static_cast<sal_Int32>(rRequest.Steps.size());
    aResult.PlanId = MakePlanId(rRequest);
    aResult.EvidenceId = MakePlannerEvidenceId(aResult.PlanId);
    aResult.HashReference = MakePlannerHashReference(rRequest);

    if (!IsTaskIdAllowed(rRequest.TaskId))
    {
        aResult.Message = MakeFailureMessage(u"task-id-invalid"_ustr);
        return aResult;
    }
    if (!IsOwnerSurfaceAllowed(rRequest.OwnerSurface))
    {
        aResult.Message = MakeFailureMessage(u"owner-surface-invalid"_ustr);
        return aResult;
    }
    if (!IsPlanShapeAllowed(rRequest))
    {
        aResult.Message = MakeFailureMessage(u"schema-or-step-shape-invalid"_ustr);
        return aResult;
    }
    if (!IsApprovalPolicyAllowed(rRequest.Policy))
    {
        aResult.Message = MakeFailureMessage(u"approval-policy-invalid"_ustr);
        return aResult;
    }
    if (!IsDataBoundaryAllowed(rRequest.Policy))
    {
        aResult.Message = MakeFailureMessage(u"data-boundary-invalid"_ustr);
        return aResult;
    }
    if (!IsSandboxAllowed(rRequest.Policy) || !IsShadowDocPolicyAllowed(rRequest.Policy))
    {
        aResult.Message = MakeFailureMessage(u"shadow-doc-policy-invalid"_ustr);
        return aResult;
    }
    if (!IsDependencyPolicyAllowed(rRequest.Policy) || !HasForwardOnlyDag(rRequest.Steps))
    {
        aResult.Message = MakeFailureMessage(u"forward-only-dag-invalid"_ustr);
        return aResult;
    }
    if (!IsPlannerValidationAllowed(rRequest.Policy))
    {
        aResult.Message = MakeFailureMessage(u"planner-validation-policy-invalid"_ustr);
        return aResult;
    }
    if (!IsPromptPolicyAllowed(rRequest.Policy))
    {
        aResult.Message = MakeFailureMessage(u"deterministic-prompt-policy-invalid"_ustr);
        return aResult;
    }
    if (!IsTokenLockAllowed(rRequest.Policy) || !PatchStepsUseApplyPlanEvidence(rRequest.Steps))
    {
        aResult.Message = MakeFailureMessage(u"apply-plan-token-lock-invalid"_ustr);
        return aResult;
    }
    if (!EveryStepHasPolicy(rRequest.Steps) || !rRequest.RootEvidenceRequiresPolicyDecision
        || !rRequest.RootEvidenceRequiresEvidenceRecord || !rRequest.RootAuditLogRequired
        || !rRequest.BlocksMergeOnMissingEvidence)
    {
        aResult.Message = MakeFailureMessage(u"evidence-or-audit-policy-invalid"_ustr);
        return aResult;
    }
    if (!rRequest.Policy.ReviewStepRequired || !HasReviewStep(rRequest.Steps))
    {
        aResult.Message = MakeFailureMessage(u"review-step-required"_ustr);
        return aResult;
    }

    aResult.Success = true;
    aResult.PlannerState = u"schema-validated"_ustr;
    aResult.Message = u"agent-plan-validated task-id="_ustr + rRequest.TaskId
                      + u" plan-id="_ustr + aResult.PlanId
                      + u" state=schema-validated schema-validated=true"_ustr
                      + u" plan-act-observe=true graph=forward-only-dag"_ustr
                      + u" execution-order=topological-index step-count="_ustr
                      + OUString::number(aResult.StepCount) + u" approval-mode="_ustr
                      + rRequest.Policy.ApprovalMode
                      + u" selectedModeSource="_ustr + rRequest.Policy.SelectedModeSource
                      + u" perStepRequiresExplicitUserChoice=true"_ustr
                      + u" implicitPerStepPromptsAllowed=false"_ustr
                      + u" approval-evidence=user-approval"_ustr
                      + u" prompt-set=w6-plan-act-observe-v1 prompt-version=v1"_ustr
                      + u" planner-prompt=planner-v1 actor-prompt=actor-v1 observer-prompt=observer-v1"_ustr
                      + u" deterministic=true temperature=0 top-p=1 seed-required=true"_ustr
                      + u" public-egress=false allowPublicEgress=false"_ustr
                      + u" runtimePlannerImplementation=not-started"_ustr
                      + u" runtimeSchedulerImplementation=not-started"_ustr
                      + u" runtimePromptExecution=not-started"_ustr
                      + u" runtimeShadowDocImplementation=not-started"_ustr
                      + u" shadow-doc-mode=per-step-compatible-branch"_ustr
                      + u" writer-compatibility=v2-w3-swdocshell"_ustr
                      + u" creates-new-docshell=false main-document-mutation=false"_ustr
                      + u" merge-requires-approval=true apply-plan-runtime-required=true"_ustr
                      + u" token-lock=ParagraphAction:7,CellAction:5,SlideElementAction:4"_ustr
                      + u" evidence-required=true policy-preflight=true audit-log=true"_ustr
                      + u" goal-hash-only=true raw-goal=false raw-prompt=false raw-step-text=false"_ustr
                      + u" actor-runtime=false observer-runtime=false step-execution=false"_ustr
                      + u" evidence-id="_ustr + aResult.EvidenceId
                      + u" hash-reference="_ustr + aResult.HashReference;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
