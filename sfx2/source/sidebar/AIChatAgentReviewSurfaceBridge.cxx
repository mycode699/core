/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent review surface bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatAgentReviewSurfaceBridge.hxx"

#include "AIChatReviewStateSyncStore.hxx"

namespace sfx2::sidebar
{

bool AIChatAgentReviewSurfaceBridge::IsDocumentBindingAllowed(
    const OUString& rDocumentBinding)
{
    return rDocumentBinding.startsWith(u"doc-"_ustr) && rDocumentBinding.getLength() >= 20;
}

bool AIChatAgentReviewSurfaceBridge::IsShadowDocResultPublishable(
    const AIChatAgentShadowDocResult& rShadowResult)
{
    return rShadowResult.Success && !rShadowResult.RegistryEntry.ObjectId.isEmpty()
           && rShadowResult.RegistryEntry.Type == u"task-step"_ustr
           && rShadowResult.RegistryEntry.SourceSurface == u"agent-shadow-doc"_ustr
           && rShadowResult.RegistryEntry.State == u"awaiting-review"_ustr
           && !rShadowResult.RegistryEntry.EvidenceId.isEmpty()
           && !rShadowResult.RegistryEntry.HashReference.isEmpty()
           && rShadowResult.RegistryEntry.OpenTarget == u"diff-review"_ustr
           && rShadowResult.RegistryEntry.PreviewMode == u"diff-preview"_ustr
           && rShadowResult.StepResult.SandboxMode == u"shadow-doc"_ustr
           && rShadowResult.StepResult.MainDocumentUnchanged
           && !rShadowResult.StepResult.StoresDocumentContent
           && rShadowResult.StepResult.ApplyPlanRuntimeValidated
           && rShadowResult.TaskState.State == u"awaiting-review"_ustr
           && rShadowResult.TaskState.EvidenceCompleteCheckpoint;
}

AIChatAgentReviewSurfaceResult
AIChatAgentReviewSurfaceBridge::PublishShadowDocResult(
    const AIChatAgentShadowDocResult& rShadowResult, const OUString& rDocumentBinding) const
{
    AIChatAgentReviewSurfaceResult aResult;
    if (!IsShadowDocResultPublishable(rShadowResult)
        || !IsDocumentBindingAllowed(rDocumentBinding))
    {
        aResult.Message
            = u"agent-review-surface-failed reason=invalid-shadow-result-or-document-binding"_ustr
              + u" task-step=false review-queue=false diff-review=false"_ustr
              + u" evidence-inspector=false activity-timeline=false session-snapshot=false"_ustr
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    aResult.TaskStepObjectId = rShadowResult.RegistryEntry.ObjectId;
    aResult.EvidenceId = rShadowResult.RegistryEntry.EvidenceId;
    aResult.HashReference = rShadowResult.RegistryEntry.HashReference;
    aResult.OpenTarget = u"diff-review"_ustr;
    aResult.PreviewMode = u"diff-preview"_ustr;

    AIChatReviewQueueStore aQueue;
    if (!aQueue.EnqueueFromRegistry(rShadowResult.RegistryEntry))
    {
        aResult.Message
            = u"agent-review-surface-failed reason=review-queue-write-failed"_ustr
              + u" task-step=true main-document-mutation=false"_ustr;
        return aResult;
    }
    aResult.QueueState = u"queued"_ustr;

    AIChatContentReviewStore aContentReview;
    const AIChatContentReviewCreateResult aReview
        = aContentReview.CreateReviewFromSource(rShadowResult.RegistryEntry);
    if (!aReview.Success)
    {
        aResult.Message
            = u"agent-review-surface-failed reason=content-review-write-failed"_ustr
              + u" review-queue=true main-document-mutation=false"_ustr;
        return aResult;
    }
    aResult.ReviewId = aReview.Review.ReviewId;

    AIChatEvidenceInspector aEvidenceInspector;
    const AIChatEvidenceInspectionResult aInspection
        = aEvidenceInspector.Inspect(rShadowResult.RegistryEntry);
    if (!aInspection.Success)
    {
        aResult.Message
            = u"agent-review-surface-failed reason=evidence-inspector-link-failed"_ustr
              + u" review-id="_ustr + aResult.ReviewId
              + u" main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatReviewStateSyncStore aStateSync;
    const AIChatReviewStateSyncResult aTaskStepState = aStateSync.RecordFromRegistry(
        rShadowResult.RegistryEntry, u"open"_ustr, u"queued"_ustr, u"task-progress"_ustr);
    if (!aTaskStepState.Success)
    {
        aResult.Message
            = u"agent-review-surface-failed reason=task-progress-state-sync-failed"_ustr
              + u" review-id="_ustr + aResult.ReviewId
              + u" main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatWorkspaceSessionStore aSession(rDocumentBinding);
    const OUString sTimestamp = AIChatWorkspaceSessionStore::MakeTimestamp();

    AIChatWorkspaceActivityEntry aReadyActivity;
    aReadyActivity.Event = u"review-opened"_ustr;
    aReadyActivity.Surface = u"reviews"_ustr;
    aReadyActivity.Actor = u"agent"_ustr;
    aReadyActivity.Timestamp = sTimestamp;
    aReadyActivity.ArtifactId = aResult.TaskStepObjectId;
    aReadyActivity.ReviewId = aResult.ReviewId;
    aReadyActivity.EvidenceId = aResult.EvidenceId;
    aReadyActivity.HashReference = aResult.HashReference;
    aReadyActivity.OpenTarget = u"diff-review"_ustr;
    if (!aSession.RecordActivity(aReadyActivity))
    {
        aResult.Message
            = u"agent-review-surface-failed reason=activity-timeline-write-failed"_ustr
              + u" review-id="_ustr + aResult.ReviewId
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatWorkspaceActivityEntry aStateActivity = aReadyActivity;
    aStateActivity.Event = u"review-state-changed"_ustr;
    if (!aSession.RecordActivity(aStateActivity))
    {
        aResult.Message
            = u"agent-review-surface-failed reason=activity-state-write-failed"_ustr
              + u" review-id="_ustr + aResult.ReviewId
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatSessionSnapshot aSnapshot;
    aSnapshot.DocumentBinding = rDocumentBinding;
    aSnapshot.Timestamp = sTimestamp;
    aSnapshot.ActiveTaskId = rShadowResult.TaskState.TaskId;
    aSnapshot.OpenArtifactId = aResult.TaskStepObjectId;
    aSnapshot.OpenReviewId = aResult.ReviewId;
    aSnapshot.ActiveEvidenceId = aResult.EvidenceId;
    aSnapshot.PreviewMode = u"diff-preview"_ustr;
    aSnapshot.ReviewState = u"queued"_ustr;
    aSnapshot.ActivityCursor = sTimestamp;
    aSnapshot.FailureState = u"none"_ustr;
    aSnapshot.HashReference = aResult.HashReference;
    if (!aSession.SaveSnapshot(aSnapshot))
    {
        aResult.Message
            = u"agent-review-surface-failed reason=session-snapshot-write-failed"_ustr
              + u" review-id="_ustr + aResult.ReviewId
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aResult;
    }
    aResult.ActivityCursor = sTimestamp;

    aResult.Success = true;
    aResult.Message
        = u"agent-review-surface-published task-step-id="_ustr + aResult.TaskStepObjectId
          + u" review-id="_ustr + aResult.ReviewId
          + u" queue-state=queued review-queue=true content-review=true"_ustr
          + u" diff-review=true evidence-inspector=true activity-timeline=true"_ustr
          + u" session-snapshot=true task-progress=true"_ustr
          + u" event=review-opened event=review-state-changed actor=agent"_ustr
          + u" open-target=diff-review preview-mode=diff-preview"_ustr
          + u" evidence-id="_ustr + aResult.EvidenceId
          + u" hash-reference="_ustr + aResult.HashReference
          + u" active-task-id="_ustr + rShadowResult.TaskState.TaskId
          + u" mainDocumentUnchanged=true requires-human-approval=true"_ustr
          + u" main-document-mutation=false auto-apply=false metadata-only=true"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
