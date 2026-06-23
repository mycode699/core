/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent task state store).
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

struct AIChatAgentStepResultEntry
{
    OUString ResultId;
    OUString SchemaVersion;
    OUString TaskId;
    sal_Int32 StepIndex = 0;
    OUString Kind;
    OUString Status;
    OUString StartedAt;
    OUString FinishedAt;
    OUString OutputKind;
    OUString OutputSchemaRef;
    OUString OutputRefId;
    bool StoresDocumentContent = false;
    bool ApplyPlanRuntimeValidated = false;
    OUString SandboxMode;
    OUString ShadowBranchId;
    bool MainDocumentUnchanged = true;
    OUString FailureIsolation;
    bool PolicyPreflight = true;
    bool PolicyAuditLog = true;
    OUString PolicyDecision;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    OUString FailureCode;
    bool FailureRecoverable = false;
    bool RetryAllowed = false;
};

struct AIChatAgentTaskStateEntry
{
    OUString TaskId;
    OUString SchemaVersion;
    OUString UpdatedAt;
    OUString OwnerSurface;
    OUString State;
    sal_Int32 CurrentStepIndex = 0;
    sal_Int32 MaxSteps = 0;
    sal_Int32 CompletedSteps = 0;
    sal_Int32 FailedSteps = 0;
    sal_Int32 CancelledSteps = 0;
    bool UsesV2AsyncCowork = true;
    OUString TaskKind;
    OUString CoworkTaskState;
    OUString ApprovalMode;
    bool WholeTaskApprovalRequired = true;
    bool PerStepApprovalSupported = true;
    bool SoftCancelSupported = true;
    bool HardCancelSupported = true;
    bool UserDecisionRequired = true;
    bool MainDocumentUnchangedOnFailure = true;
    OUString MergeTarget;
    bool MergeRequiresApproval = true;
    bool RequiresApplyPlanRuntimeValidation = true;
    bool AuditLogRequired = true;
    std::vector<OUString> RequiredForEveryStep;
    std::vector<OUString> TaskEvidenceIds;
    OUString CheckpointId;
    bool EvidenceCompleteCheckpoint = false;
    OUString DocumentHashReference;
    OUString ShadowSnapshotRef;
    OUString AuditReplayRef;
    bool ResumeRequiresUserConfirmation = true;
};

struct AIChatAgentTaskStateResult
{
    bool Success = false;
    AIChatAgentTaskStateEntry State;
    OUString Message;
};

class AIChatAgentTaskStateStore final
{
public:
    AIChatAgentTaskStateStore();

    const OUString& GetTaskStateUrl() const { return m_sTaskStateUrl; }
    const OUString& GetStepResultUrl() const { return m_sStepResultUrl; }

    bool RecordStepResult(const AIChatAgentStepResultEntry& rEntry) const;
    AIChatAgentTaskStateResult SaveTaskState(const AIChatAgentTaskStateEntry& rEntry) const;
    AIChatAgentTaskStateResult RecordTaskState(const AIChatAgentTaskStateEntry& rEntry) const;
    AIChatAgentTaskStateResult TransitionTaskState(const AIChatAgentTaskStateEntry& rCurrent,
                                                   const OUString& rNewState,
                                                   const OUString& rUpdatedAt,
                                                   const OUString& rEvidenceId) const;
    AIChatAgentTaskStateResult RequestCancel(const AIChatAgentTaskStateEntry& rCurrent,
                                             const OUString& rCancelMode,
                                             const OUString& rUpdatedAt,
                                             const OUString& rEvidenceId) const;
    AIChatAgentTaskStateResult ValidateResumeCheckpoint(
        const AIChatAgentTaskStateEntry& rEntry, const OUString& rCurrentDocumentHashReference,
        bool bUserConfirmed) const;
    AIChatAgentTaskStateResult GetLatestTaskState(const OUString& rTaskId) const;
    std::vector<AIChatAgentTaskStateEntry> LoadTaskStates() const;
    std::vector<AIChatAgentStepResultEntry> LoadStepResults(const OUString& rTaskId) const;

    static bool IsTaskIdAllowed(const OUString& rTaskId);
    static bool IsStepResultIdAllowed(const OUString& rResultId);
    static bool IsShadowBranchIdAllowed(const OUString& rBranchId);
    static bool IsEvidenceIdAllowed(const OUString& rEvidenceId);
    static bool IsOutputRefAllowed(const OUString& rRefId);
    static bool IsTaskStateAllowed(const OUString& rState);
    static bool IsCoworkTaskStateAllowed(const OUString& rState);
    static bool IsStepResultStatusAllowed(const OUString& rStatus);
    static bool IsAllowedTransition(const OUString& rFromState, const OUString& rToState);
    static bool IsTerminalState(const OUString& rState);
    static bool IsBaseEvidenceComplete(const std::vector<OUString>& rEvidence);
    static bool IsTaskEvidenceComplete(const AIChatAgentTaskStateEntry& rEntry);
    static bool IsStepResultAllowed(const AIChatAgentStepResultEntry& rEntry);
    static bool IsTaskStateShapeAllowed(const AIChatAgentTaskStateEntry& rEntry);
    static OUString MakeStepResultId(const OUString& rTaskId, sal_Int32 nStepIndex);
    static OUString MakeCheckpointId(const OUString& rTaskId, sal_Int32 nStepIndex);
    static OUString MakeTaskHashReference(const AIChatAgentTaskStateEntry& rEntry);

private:
    OUString m_sStorageRootUrl;
    OUString m_sTaskStateUrl;
    OUString m_sStepResultUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
