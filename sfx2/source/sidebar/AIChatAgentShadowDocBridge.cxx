/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent ShadowDoc bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatAgentShadowDocBridge.hxx"

#include "AIChatKnowledgeIndexStore.hxx"
#include "AIChatSourceProvenance.hxx"

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

AIChatAgentTaskStateEntry MakeAwaitingReviewState(
    const AIChatAgentTaskStateEntry& rCurrentState, const AIChatAgentShadowDocRequest& rRequest)
{
    AIChatAgentTaskStateEntry aNext = rCurrentState;
    aNext.State = u"awaiting-review"_ustr;
    aNext.CoworkTaskState = u"awaiting-review"_ustr;
    aNext.CurrentStepIndex = rRequest.StepIndex;
    aNext.EvidenceCompleteCheckpoint = true;
    aNext.CheckpointId = AIChatAgentTaskStateStore::MakeCheckpointId(rRequest.TaskId,
                                                                     rRequest.StepIndex);
    aNext.DocumentHashReference = rRequest.DocumentSnapshotHash;
    aNext.ShadowSnapshotRef = rRequest.ShadowSnapshotRef;
    aNext.AuditReplayRef = rRequest.AuditReplayRef;
    aNext.ResumeRequiresUserConfirmation = true;
    if (!ContainsString(aNext.TaskEvidenceIds, rRequest.EvidenceId))
        aNext.TaskEvidenceIds.push_back(rRequest.EvidenceId);
    return aNext;
}
}

bool AIChatAgentShadowDocBridge::IsApplyPlanRuntimeRefAllowed(
    const OUString& rApplyPlanRuntimeRef)
{
    return rApplyPlanRuntimeRef.startsWith(u"aprt-"_ustr)
           && IsLowerHex(rApplyPlanRuntimeRef.copy(5), 16);
}

bool AIChatAgentShadowDocBridge::IsDocumentSnapshotHashAllowed(
    const OUString& rDocumentSnapshotHash)
{
    return rDocumentSnapshotHash.startsWith(u"sha256:"_ustr)
           && IsLowerHex(rDocumentSnapshotHash.copy(7), 64);
}

bool AIChatAgentShadowDocBridge::IsShadowSnapshotRefAllowed(
    const OUString& rShadowSnapshotRef)
{
    return rShadowSnapshotRef.startsWith(u"shadow-snapshot:"_ustr)
           && rShadowSnapshotRef.getLength() > 32;
}

bool AIChatAgentShadowDocBridge::IsShadowDocRequestAllowed(
    const AIChatAgentTaskStateEntry& rCurrentState,
    const AIChatAgentShadowDocRequest& rRequest)
{
    return AIChatAgentTaskStateStore::IsTaskStateShapeAllowed(rCurrentState)
           && AIChatAgentTaskStateStore::IsTaskIdAllowed(rRequest.TaskId)
           && rCurrentState.TaskId == rRequest.TaskId
           && (rCurrentState.State == u"running"_ustr
               || rCurrentState.State == u"awaiting-review"_ustr)
           && rRequest.StepIndex >= 0 && rRequest.StepIndex <= 24
           && rRequest.StepIndex == rCurrentState.CurrentStepIndex
           && (rRequest.OwnerSurface == u"writer"_ustr || rRequest.OwnerSurface == u"calc"_ustr
               || rRequest.OwnerSurface == u"impress"_ustr)
           && AIChatAgentTaskStateStore::IsShadowBranchIdAllowed(rRequest.ShadowBranchId)
           && IsApplyPlanRuntimeRefAllowed(rRequest.ApplyPlanRuntimeRef)
           && rRequest.ApplyPlanSchemaVersion == u"v2-w3-runtime-1"_ustr
           && IsDocumentSnapshotHashAllowed(rRequest.DocumentSnapshotHash)
           && IsShadowSnapshotRefAllowed(rRequest.ShadowSnapshotRef)
           && IsDocumentSnapshotHashAllowed(rRequest.DiffHashReference)
           && AIChatAgentTaskStateStore::IsEvidenceIdAllowed(rRequest.EvidenceId)
           && rRequest.AuditReplayRef.startsWith(u"audit-replay:"_ustr)
           && rRequest.ApplyPlanRuntimeValidated && rRequest.MainDocumentUnchanged
           && !rRequest.UserApprovedMerge;
}

OUString AIChatAgentShadowDocBridge::MakeShadowDocStepObjectId(const OUString& rTaskId,
                                                               sal_Int32 nStepIndex)
{
    return u"task-step:"_ustr + rTaskId + u":shadow-doc:"_ustr
           + OUString::number(nStepIndex);
}

OUString AIChatAgentShadowDocBridge::MakeShadowDocHashReference(
    const AIChatAgentShadowDocRequest& rRequest)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rRequest.TaskId + u":"_ustr + OUString::number(rRequest.StepIndex) + u":"_ustr
                 + rRequest.ShadowBranchId + u":"_ustr + rRequest.ApplyPlanRuntimeRef + u":"_ustr
                 + rRequest.DiffHashReference);
}

AIChatAgentShadowDocResult AIChatAgentShadowDocBridge::PreparePatchStep(
    const AIChatAgentTaskStateEntry& rCurrentState,
    const AIChatAgentShadowDocRequest& rRequest) const
{
    AIChatAgentShadowDocResult aResult;
    if (!IsShadowDocRequestAllowed(rCurrentState, rRequest))
    {
        aResult.Message
            = u"agent-shadow-doc-failed reason=invalid-shadow-doc-request"_ustr
              + u" shadow-doc-mode=per-step-compatible-branch"_ustr
              + u" writer-compatibility=v2-w3-swdocshell creates-new-docshell=false"_ustr
              + u" apply-plan-runtime-required=true applyPlanRuntimeValidated=false"_ustr
              + u" mainDocumentUnchanged=true main-document-mutation=false"_ustr
              + u" mergeRequiresApproval=true userApprovedMerge=false metadata-only=true"_ustr;
        return aResult;
    }

    aResult.StepResult.ResultId = AIChatAgentTaskStateStore::MakeStepResultId(
        rRequest.TaskId, rRequest.StepIndex);
    aResult.StepResult.SchemaVersion = u"v3-agent-step-result/0.1"_ustr;
    aResult.StepResult.TaskId = rRequest.TaskId;
    aResult.StepResult.StepIndex = rRequest.StepIndex;
    aResult.StepResult.Kind = u"patch"_ustr;
    aResult.StepResult.Status = u"completed"_ustr;
    aResult.StepResult.OutputKind = u"apply-plan-runtime"_ustr;
    aResult.StepResult.OutputSchemaRef = u"apply-plan-runtime"_ustr;
    aResult.StepResult.OutputRefId = rRequest.ApplyPlanRuntimeRef;
    aResult.StepResult.StoresDocumentContent = false;
    aResult.StepResult.ApplyPlanRuntimeValidated = true;
    aResult.StepResult.SandboxMode = u"shadow-doc"_ustr;
    aResult.StepResult.ShadowBranchId = rRequest.ShadowBranchId;
    aResult.StepResult.MainDocumentUnchanged = true;
    aResult.StepResult.FailureIsolation = u"discard-step-branch"_ustr;
    aResult.StepResult.PolicyPreflight = true;
    aResult.StepResult.PolicyAuditLog = true;
    aResult.StepResult.PolicyDecision = u"require-approval"_ustr;
    aResult.StepResult.RequiredEvidence = { u"policy-decision"_ustr, u"audit-log-entry"_ustr,
                                            u"evidence-record"_ustr, u"shadow-doc-diff"_ustr,
                                            u"apply-plan-runtime-validated"_ustr };
    aResult.StepResult.EvidenceIds = { rRequest.EvidenceId };
    aResult.StepResult.FailureCode = u"none"_ustr;
    aResult.StepResult.FailureRecoverable = false;
    aResult.StepResult.RetryAllowed = false;

    aResult.TaskState = MakeAwaitingReviewState(rCurrentState, rRequest);
    if (!AIChatAgentTaskStateStore::IsStepResultAllowed(aResult.StepResult)
        || !AIChatAgentTaskStateStore::IsTaskStateShapeAllowed(aResult.TaskState))
    {
        aResult.Message
            = u"agent-shadow-doc-failed reason=invalid-step-or-task-state"_ustr
              + u" shadow-doc-mode=per-step-compatible-branch"_ustr
              + u" storesDocumentContent=false mainDocumentUnchanged=true"_ustr
              + u" evidence-complete-checkpoint=required main-document-mutation=false"_ustr;
        return aResult;
    }

    aResult.RegistryEntry.ObjectId = MakeShadowDocStepObjectId(rRequest.TaskId,
                                                               rRequest.StepIndex);
    aResult.RegistryEntry.Type = u"task-step"_ustr;
    aResult.RegistryEntry.SourceSurface = u"agent-shadow-doc"_ustr;
    aResult.RegistryEntry.State = u"awaiting-review"_ustr;
    aResult.RegistryEntry.EvidenceId = rRequest.EvidenceId;
    aResult.RegistryEntry.HashReference = MakeShadowDocHashReference(rRequest);
    aResult.RegistryEntry.OpenTarget = u"diff-review"_ustr;
    aResult.RegistryEntry.PreviewMode = u"diff-preview"_ustr;

    AIChatContentRegistry aRegistry;
    if (!aRegistry.RegisterObject(aResult.RegistryEntry))
    {
        aResult.Message = u"agent-shadow-doc-failed reason=registry-write-failed"_ustr;
        return aResult;
    }

    AIChatAgentTaskStateStore aTaskStateStore;
    if (!aTaskStateStore.RecordStepResult(aResult.StepResult))
    {
        aResult.Message
            = u"agent-shadow-doc-failed reason=step-result-write-failed"_ustr
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }
    AIChatAgentTaskStateResult aTaskStateResult
        = aTaskStateStore.RecordTaskState(aResult.TaskState);
    if (!aTaskStateResult.Success)
    {
        aResult.Message
            = u"agent-shadow-doc-failed reason=task-state-write-failed"_ustr
              + u" evidence-complete-checkpoint=required metadata-only=true"_ustr
              + u" main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatSourceProvenanceEntry aSource;
    aSource.SourceId = AIChatSourceProvenance::MakeSourceId(aResult.RegistryEntry.ObjectId);
    aSource.SourceType = u"task-step"_ustr;
    aSource.CitationId = AIChatSourceProvenance::MakeCitationId(aResult.RegistryEntry.ObjectId);
    aSource.EvidenceId = rRequest.EvidenceId;
    aSource.HashReference = aResult.RegistryEntry.HashReference;
    aSource.SourceSurface = u"agent-shadow-doc"_ustr;
    aSource.OpenTarget = u"diff-review"_ustr;
    aSource.SpanReference = u"span:shadow-doc-step:"_ustr + rRequest.TaskId + u":"_ustr
                            + OUString::number(rRequest.StepIndex);
    aSource.ReviewId = OUString();
    AIChatSourceProvenance aProvenance;
    aProvenance.RegisterSource(aSource);

    aResult.SourceId = aSource.SourceId;
    aResult.CitationId = aSource.CitationId;
    aResult.Success = true;
    aResult.Message
        = u"agent-shadow-doc-prepared task-id="_ustr + rRequest.TaskId + u" step-index="_ustr
          + OUString::number(rRequest.StepIndex)
          + u" shadow-doc-mode=per-step-compatible-branch"_ustr
          + u" writer-compatibility=v2-w3-swdocshell creates-new-docshell=false"_ustr
          + u" shadow-branch-id="_ustr + rRequest.ShadowBranchId
          + u" apply-plan-runtime-ref="_ustr + rRequest.ApplyPlanRuntimeRef
          + u" apply-plan-schema=v2-w3-runtime-1 applyPlanRuntimeValidated=true"_ustr
          + u" token-lock=ParagraphAction:7,CellAction:5,SlideElementAction:4"_ustr
          + u" output-kind=apply-plan-runtime output-schema=apply-plan-runtime"_ustr
          + u" storesDocumentContent=false raw-apply-plan=false raw-diff=false"_ustr
          + u" shadow-doc-diff=true apply-plan-runtime-validated=true"_ustr
          + u" mainDocumentUnchanged=true main-document-mutation=false"_ustr
          + u" merge-target=main-doc mergeRequiresApproval=true userApprovedMerge=false"_ustr
          + u" task-state=awaiting-review open-target=diff-review preview-mode=diff-preview"_ustr
          + u" registry=true provenance=true evidence-id="_ustr + rRequest.EvidenceId
          + u" hash-reference="_ustr + aResult.RegistryEntry.HashReference
          + u" step-result-recorded=true task-state-recorded=true"_ustr
          + u" metadata-only=true runtimeShadowDocImplementation=not-started"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
