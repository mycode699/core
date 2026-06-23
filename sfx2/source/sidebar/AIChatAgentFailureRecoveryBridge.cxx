/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent failure recovery bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatAgentFailureRecoveryBridge.hxx"

#include "AIChatEvidenceInspector.hxx"
#include "AIChatKnowledgeIndexStore.hxx"
#include "AIChatReviewStateSyncStore.hxx"
#include "AIChatSourceProvenance.hxx"
#include "AIChatWorkspaceActionBarStore.hxx"
#include "AIChatWorkspaceSessionStore.hxx"

#include <algorithm>

namespace sfx2::sidebar
{
namespace
{
bool ContainsString(const std::vector<OUString>& rValues, const OUString& rNeedle)
{
    return std::find(rValues.begin(), rValues.end(), rNeedle) != rValues.end();
}

OUString FirstEvidenceId(const AIChatAgentStepResultEntry& rStepResult)
{
    return rStepResult.EvidenceIds.empty() ? OUString() : rStepResult.EvidenceIds.front();
}
}

bool AIChatAgentFailureRecoveryBridge::IsDocumentBindingAllowed(
    const OUString& rDocumentBinding)
{
    return rDocumentBinding.startsWith(u"doc-"_ustr) && rDocumentBinding.getLength() >= 20;
}

bool AIChatAgentFailureRecoveryBridge::IsFailedStepRecoverable(
    const AIChatAgentStepResultEntry& rStepResult,
    const AIChatAgentTaskStateEntry& rTaskState)
{
    return AIChatAgentTaskStateStore::IsStepResultAllowed(rStepResult)
           && AIChatAgentTaskStateStore::IsTaskStateShapeAllowed(rTaskState)
           && rStepResult.Status == u"failed"_ustr && rTaskState.State == u"failed"_ustr
           && rTaskState.CoworkTaskState == u"failed"_ustr
           && rStepResult.TaskId == rTaskState.TaskId
           && rStepResult.StepIndex == rTaskState.CurrentStepIndex
           && !rStepResult.FailureCode.isEmpty() && rStepResult.FailureCode != u"none"_ustr
           && rStepResult.FailureRecoverable && rStepResult.RetryAllowed
           && rStepResult.MainDocumentUnchanged && rTaskState.MainDocumentUnchangedOnFailure
           && rTaskState.UserDecisionRequired && rTaskState.SoftCancelSupported
           && rTaskState.HardCancelSupported && rTaskState.ResumeRequiresUserConfirmation
           && rTaskState.EvidenceCompleteCheckpoint && !rTaskState.CheckpointId.isEmpty()
           && !rTaskState.DocumentHashReference.isEmpty() && !rTaskState.ShadowSnapshotRef.isEmpty()
           && !rTaskState.AuditReplayRef.isEmpty()
           && ContainsString(rStepResult.RequiredEvidence, u"evidence-record"_ustr)
           && ContainsString(rStepResult.RequiredEvidence, u"audit-log-entry"_ustr)
           && ContainsString(rStepResult.RequiredEvidence, u"policy-decision"_ustr)
           && AIChatAgentTaskStateStore::IsEvidenceIdAllowed(FirstEvidenceId(rStepResult));
}

OUString AIChatAgentFailureRecoveryBridge::MakeFailureStepObjectId(const OUString& rTaskId,
                                                                   sal_Int32 nStepIndex)
{
    return u"task-step:"_ustr + rTaskId + u":failure-recovery:"_ustr
           + OUString::number(nStepIndex);
}

OUString AIChatAgentFailureRecoveryBridge::MakeFailureHashReference(
    const AIChatAgentStepResultEntry& rStepResult,
    const AIChatAgentTaskStateEntry& rTaskState)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rStepResult.TaskId + u":"_ustr + OUString::number(rStepResult.StepIndex)
                 + u":"_ustr + rStepResult.FailureCode + u":"_ustr + rStepResult.OutputRefId
                 + u":"_ustr + rTaskState.CheckpointId);
}

AIChatAgentFailureRecoveryResult
AIChatAgentFailureRecoveryBridge::PublishFailedStep(
    const AIChatAgentStepResultEntry& rStepResult,
    const AIChatAgentTaskStateEntry& rTaskState, const OUString& rDocumentBinding) const
{
    AIChatAgentFailureRecoveryResult aResult;
    if (!IsFailedStepRecoverable(rStepResult, rTaskState)
        || !IsDocumentBindingAllowed(rDocumentBinding))
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=invalid-failed-step-or-document-binding"_ustr
              + u" visible-failure=false retry=false cancel=false"_ustr
              + u" evidence-inspector=false source-links=false activity-timeline=false"_ustr
              + u" session-snapshot=false metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    aResult.TaskStepObjectId = MakeFailureStepObjectId(rStepResult.TaskId, rStepResult.StepIndex);
    aResult.FailureCode = rStepResult.FailureCode;
    aResult.EvidenceId = FirstEvidenceId(rStepResult);
    aResult.HashReference = MakeFailureHashReference(rStepResult, rTaskState);
    aResult.OpenTarget = u"task-progress"_ustr;
    aResult.PreviewMode = u"metadata-summary"_ustr;

    AIChatContentRegistryEntry aEntry;
    aEntry.ObjectId = aResult.TaskStepObjectId;
    aEntry.Type = u"task-step"_ustr;
    aEntry.SourceSurface = u"agent-failure-recovery"_ustr;
    aEntry.State = u"failed"_ustr;
    aEntry.EvidenceId = aResult.EvidenceId;
    aEntry.HashReference = aResult.HashReference;
    aEntry.OpenTarget = u"task-progress"_ustr;
    aEntry.PreviewMode = u"metadata-summary"_ustr;

    AIChatContentRegistry aRegistry;
    if (!aRegistry.RegisterObject(aEntry))
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=registry-write-failed"_ustr
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatSourceProvenanceEntry aSource;
    aSource.SourceId = AIChatSourceProvenance::MakeSourceId(aEntry.ObjectId);
    aSource.SourceType = u"task-step"_ustr;
    aSource.CitationId = AIChatSourceProvenance::MakeCitationId(aEntry.ObjectId);
    aSource.EvidenceId = aEntry.EvidenceId;
    aSource.HashReference = aEntry.HashReference;
    aSource.SourceSurface = u"agent-failure-recovery"_ustr;
    aSource.OpenTarget = u"evidence-inspector"_ustr;
    aSource.SpanReference = u"span:failed-step:"_ustr + rStepResult.TaskId + u":"_ustr
                            + OUString::number(rStepResult.StepIndex);
    AIChatSourceProvenance aProvenance;
    if (!aProvenance.RegisterSource(aSource))
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=source-provenance-write-failed"_ustr
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatEvidenceInspector aEvidenceInspector;
    const AIChatEvidenceInspectionResult aInspection = aEvidenceInspector.Inspect(aEntry);
    if (!aInspection.Success)
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=evidence-inspector-link-failed"_ustr
              + u" task-step-id="_ustr + aResult.TaskStepObjectId
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatReviewStateSyncStore aStateSync;
    const AIChatReviewStateSyncResult aFailureState = aStateSync.RecordFromRegistry(
        aEntry, u"fail"_ustr, u"failed"_ustr, u"task-progress"_ustr);
    if (!aFailureState.Success)
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=task-progress-state-sync-failed"_ustr
              + u" task-step-id="_ustr + aResult.TaskStepObjectId
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatWorkspaceActionBarStore aActionBar;
    const AIChatWorkspaceActionBarDispatchResult aRetry
        = aActionBar.DispatchCommand(u"retry"_ustr, &aEntry, false, true);
    if (!aRetry.Success)
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=retry-action-disabled"_ustr
              + u" userDecisionRequired=true auto-retry=false main-document-mutation=false"_ustr;
        return aResult;
    }
    aResult.RetryActionState = u"enabled"_ustr;

    const AIChatWorkspaceActionBarDispatchResult aCancel
        = aActionBar.DispatchCommand(u"cancel"_ustr, &aEntry, true, false);
    if (!aCancel.Success)
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=cancel-action-disabled"_ustr
              + u" userDecisionRequired=true background-cancel=false main-document-mutation=false"_ustr;
        return aResult;
    }
    aResult.CancelActionState = u"enabled"_ustr;

    AIChatWorkspaceSessionStore aSession(rDocumentBinding);
    const OUString sTimestamp = AIChatWorkspaceSessionStore::MakeTimestamp();

    AIChatWorkspaceActivityEntry aFailureActivity;
    aFailureActivity.Event = u"failure-reported"_ustr;
    aFailureActivity.Surface = u"task-progress"_ustr;
    aFailureActivity.Actor = u"agent"_ustr;
    aFailureActivity.Timestamp = sTimestamp;
    aFailureActivity.ArtifactId = aResult.TaskStepObjectId;
    aFailureActivity.ReviewId = aResult.TaskStepObjectId;
    aFailureActivity.EvidenceId = aResult.EvidenceId;
    aFailureActivity.HashReference = aResult.HashReference;
    aFailureActivity.OpenTarget = u"task-progress"_ustr;
    if (!aSession.RecordActivity(aFailureActivity))
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=failure-activity-write-failed"_ustr
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatWorkspaceActivityEntry aRetryActivity = aFailureActivity;
    aRetryActivity.Event = u"action-invoked"_ustr;
    aRetryActivity.OpenTarget = u"retry"_ustr;
    if (!aSession.RecordActivity(aRetryActivity))
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=retry-activity-write-failed"_ustr
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatWorkspaceActivityEntry aCancelActivity = aFailureActivity;
    aCancelActivity.Event = u"action-invoked"_ustr;
    aCancelActivity.OpenTarget = u"cancel"_ustr;
    if (!aSession.RecordActivity(aCancelActivity))
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=cancel-activity-write-failed"_ustr
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatSessionSnapshot aSnapshot;
    aSnapshot.DocumentBinding = rDocumentBinding;
    aSnapshot.Timestamp = sTimestamp;
    aSnapshot.ActiveTaskId = rTaskState.TaskId;
    aSnapshot.OpenArtifactId = aResult.TaskStepObjectId;
    aSnapshot.OpenReviewId = aResult.TaskStepObjectId;
    aSnapshot.ActiveEvidenceId = aResult.EvidenceId;
    aSnapshot.PreviewMode = u"metadata-summary"_ustr;
    aSnapshot.ReviewState = u"failed"_ustr;
    aSnapshot.ActivityCursor = sTimestamp;
    aSnapshot.FailureState = aResult.FailureCode;
    aSnapshot.HashReference = aResult.HashReference;
    if (!aSession.SaveSnapshot(aSnapshot))
    {
        aResult.Message
            = u"agent-failure-recovery-failed reason=session-snapshot-write-failed"_ustr
              + u" failure-state=true metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }
    aResult.ActivityCursor = sTimestamp;

    aResult.Success = true;
    aResult.Message
        = u"agent-failure-recovery-published task-step-id="_ustr + aResult.TaskStepObjectId
          + u" task-id="_ustr + rTaskState.TaskId
          + u" step-index="_ustr + OUString::number(rStepResult.StepIndex)
          + u" failure-code="_ustr + aResult.FailureCode
          + u" visible-failure=true failed-step-reason=true"_ustr
          + u" retry-action=enabled cancel-action=enabled open-evidence=true source-links=true"_ustr
          + u" evidence-inspector=true task-progress=true action-bar=true"_ustr
          + u" activity-timeline=true session-snapshot=true failure-state=true"_ustr
          + u" retryRequiresUserConfirmation=true cancelRequiresUserConfirmation=true"_ustr
          + u" auto-retry=false background-cancel=false auto-resume=false"_ustr
          + u" no-actor-observer-execution=true no-apply-plan-execution=true"_ustr
          + u" evidence-id="_ustr + aResult.EvidenceId
          + u" hash-reference="_ustr + aResult.HashReference
          + u" mainDocumentUnchanged=true main-document-mutation=false metadata-only=true"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
