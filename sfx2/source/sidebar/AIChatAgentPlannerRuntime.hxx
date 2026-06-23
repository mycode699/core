/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent planner runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatAgentPlanStep
{
    sal_Int32 Index = 0;
    OUString Kind;
    OUString TitleHash;
    OUString DescriptionHash;
    std::vector<sal_Int32> Dependencies;
    OUString ExpectedOutputKind;
    OUString ExpectedOutputSchemaRef;
    std::vector<OUString> Evidence;
    bool PolicyPreflight = false;
    bool PolicyAuditLog = false;
};

struct AIChatAgentPlannerPolicy
{
    OUString ApprovalMode;
    OUString SelectedModeSource;
    OUString PromptStrategy;
    bool PerStepRequiresExplicitUserChoice = false;
    bool ImplicitPerStepPromptsAllowed = false;
    bool ReviewStepRequired = false;
    OUString ApprovalEvidence;
    OUString RuntimeApprovalUiImplementation;

    bool CrossSessionResumeAllowed = false;
    OUString ResumePoint;
    bool RequiresUserConfirmation = false;
    bool RequiresDocumentHashMatch = false;
    bool RequiresShadowSnapshot = false;
    bool RequiresAuditReplay = false;
    bool AutoResumeAllowed = false;
    OUString StaleCheckpointBehavior;
    OUString CheckpointEvidence;
    OUString RuntimeResumeImplementation;

    OUString DefaultServiceMode;
    bool AllowPublicEgress = false;
    bool RequiresPolicyPreflight = false;

    OUString SandboxMode;
    OUString MergeTarget;
    OUString FailureIsolation;

    OUString ShadowDocMode;
    OUString WriterCompatibilityTarget;
    bool RequiresExistingSwDocShellCompatibility = false;
    bool CreatesNewDocShellType = false;
    OUString MergePath;
    bool MainDocMutationBeforeApprovalAllowed = false;
    bool MergeRequiresApproval = false;
    OUString RuntimeShadowDocImplementation;

    OUString GraphType;
    OUString ExecutionOrder;
    bool AllowsFanIn = false;
    bool AllowsFanOut = false;
    bool AllowsCycles = false;
    bool AllowsFutureDependencies = false;
    bool AllowsParallelRuntime = false;
    OUString RuntimeSchedulerImplementation;

    OUString ValidationPhase;
    OUString OnInvalidPlan;
    bool BlocksExecution = false;
    bool AutoRetryAllowed = false;
    bool AutoSimplificationAllowed = false;
    bool UserRetryAllowed = false;
    OUString InvalidPlanEvidence;
    OUString RuntimePlannerImplementation;

    OUString PromptSetId;
    OUString PromptSetVersion;
    OUString PlannerPromptId;
    OUString ActorPromptId;
    OUString ObserverPromptId;
    sal_Int32 Temperature = 0;
    sal_Int32 TopP = 0;
    bool SeedRequired = false;
    bool PublicEgressAllowed = false;
    OUString RuntimePromptExecution;

    bool PatchStepMustUseApplyPlanRuntime = false;
    sal_Int32 ParagraphAction = 0;
    sal_Int32 CellAction = 0;
    sal_Int32 SlideElementAction = 0;
};

struct AIChatAgentPlannerRequest
{
    OUString TaskId;
    OUString SchemaVersion;
    OUString GoalHash;
    sal_Int32 GoalLength = 0;
    OUString CreatedAt;
    OUString OwnerSurface;
    sal_Int32 MaxSteps = 0;
    AIChatAgentPlannerPolicy Policy;
    std::vector<AIChatAgentPlanStep> Steps;
    bool RootEvidenceRequiresPolicyDecision = false;
    bool RootEvidenceRequiresEvidenceRecord = false;
    bool RootAuditLogRequired = false;
    bool BlocksMergeOnMissingEvidence = false;
};

struct AIChatAgentPlannerResult
{
    bool Success = false;
    OUString TaskId;
    OUString PlanId;
    OUString PlannerState;
    sal_Int32 StepCount = 0;
    OUString EvidenceId;
    OUString HashReference;
    OUString Message;
};

class AIChatAgentPlannerRuntime final
{
public:
    AIChatAgentPlannerResult ValidatePlan(const AIChatAgentPlannerRequest& rRequest) const;

    static AIChatAgentPlannerPolicy MakeDefaultPolicy(const OUString& rApprovalMode);
    static bool IsTaskIdAllowed(const OUString& rTaskId);
    static bool IsOwnerSurfaceAllowed(const OUString& rOwnerSurface);
    static bool IsApprovalPolicyAllowed(const AIChatAgentPlannerPolicy& rPolicy);
    static bool IsDataBoundaryAllowed(const AIChatAgentPlannerPolicy& rPolicy);
    static bool IsSandboxAllowed(const AIChatAgentPlannerPolicy& rPolicy);
    static bool IsShadowDocPolicyAllowed(const AIChatAgentPlannerPolicy& rPolicy);
    static bool IsDependencyPolicyAllowed(const AIChatAgentPlannerPolicy& rPolicy);
    static bool IsPlannerValidationAllowed(const AIChatAgentPlannerPolicy& rPolicy);
    static bool IsPromptPolicyAllowed(const AIChatAgentPlannerPolicy& rPolicy);
    static bool IsTokenLockAllowed(const AIChatAgentPlannerPolicy& rPolicy);
    static bool IsStepKindAllowed(const OUString& rKind);
    static bool IsExpectedOutputAllowed(const AIChatAgentPlanStep& rStep);
    static bool IsStepEvidenceAllowed(const AIChatAgentPlanStep& rStep);
    static bool HasForwardOnlyDag(const std::vector<AIChatAgentPlanStep>& rSteps);
    static bool HasReviewStep(const std::vector<AIChatAgentPlanStep>& rSteps);
    static bool PatchStepsUseApplyPlanEvidence(const std::vector<AIChatAgentPlanStep>& rSteps);
    static bool EveryStepHasPolicy(const std::vector<AIChatAgentPlanStep>& rSteps);
    static bool IsPlanShapeAllowed(const AIChatAgentPlannerRequest& rRequest);
    static OUString MakePlanId(const AIChatAgentPlannerRequest& rRequest);
    static OUString MakePlannerEvidenceId(const OUString& rPlanId);
    static OUString MakePlannerHashReference(const AIChatAgentPlannerRequest& rRequest);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
