/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace preview matrix).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatPreviewMatrix.hxx"

#include "AIChatReviewQueueStore.hxx"
#include "AIChatReviewStateSyncStore.hxx"
#include "AIChatSourceProvenance.hxx"

namespace sfx2::sidebar
{

OUString AIChatPreviewMatrix::ResolvePreviewTarget(const AIChatContentRegistryEntry& rEntry)
{
    if (!rEntry.OpenTarget.isEmpty())
        return rEntry.OpenTarget;

    if (rEntry.Type == u"document"_ustr)
        return u"main-document-window"_ustr;
    if (rEntry.Type == u"task-step"_ustr || rEntry.Type == u"review-item"_ustr
        || rEntry.Type == u"formatting-preview"_ustr)
        return u"diff-review"_ustr;
    if (rEntry.Type == u"evidence-record"_ustr)
        return u"sidebar-preview"_ustr;
    if (rEntry.Type == u"selection"_ustr || rEntry.Type == u"connector-result"_ustr
        || rEntry.Type == u"knowledge-index-result"_ustr)
        return u"sidebar-preview"_ustr;

    return u"sidebar-preview"_ustr;
}

OUString AIChatPreviewMatrix::ResolvePreviewMode(const AIChatContentRegistryEntry& rEntry)
{
    if (!rEntry.PreviewMode.isEmpty())
        return rEntry.PreviewMode;

    if (rEntry.Type == u"task-step"_ustr || rEntry.Type == u"review-item"_ustr
        || rEntry.Type == u"formatting-preview"_ustr)
        return u"diff-preview"_ustr;
    if (rEntry.Type == u"evidence-record"_ustr)
        return u"evidence-summary"_ustr;
    return u"metadata-summary"_ustr;
}

bool AIChatPreviewMatrix::IsSupportedPreviewTarget(const OUString& rTarget)
{
    return rTarget == u"main-document-window"_ustr || rTarget == u"sidebar-preview"_ustr
           || rTarget == u"diff-review"_ustr || rTarget == u"evidence-inspector"_ustr
           || rTarget == u"review-queue"_ustr;
}

AIChatPreviewResult AIChatPreviewMatrix::BuildPreview(const AIChatContentRegistryEntry& rEntry) const
{
    AIChatPreviewResult aResult;
    aResult.ObjectId = rEntry.ObjectId;
    aResult.ContentType = rEntry.Type;
    aResult.Target = ResolvePreviewTarget(rEntry);
    aResult.Mode = ResolvePreviewMode(rEntry);
    aResult.EvidenceBadge = rEntry.EvidenceId.isEmpty() ? u"evidence=missing"_ustr
                                                        : u"evidence=linked"_ustr;
    aResult.SourceMetadata = u"source-id="_ustr
                             + AIChatSourceProvenance::MakeSourceId(rEntry.ObjectId)
                             + u" citation-id="_ustr
                             + AIChatSourceProvenance::MakeCitationId(rEntry.ObjectId)
                             + u" evidence-id="_ustr + rEntry.EvidenceId + u" source="_ustr
                             + rEntry.SourceSurface + u" hash="_ustr + rEntry.HashReference;
    if (AIChatReviewQueueStore::IsReviewQueueEntry(rEntry) && !rEntry.EvidenceId.isEmpty()
        && !rEntry.HashReference.isEmpty())
    {
        AIChatReviewStateSyncStore aStateSync;
        AIChatReviewStateSyncResult aSync = aStateSync.GetLatestState(rEntry.ObjectId);
        if (!aSync.Success)
        {
            const OUString sState = AIChatReviewStateSyncStore::NormalizeRegistryState(rEntry.State);
            aSync = aStateSync.RecordFromRegistry(
                rEntry, AIChatReviewStateSyncStore::TransitionForState(sState), sState,
                u"preview-matrix"_ustr);
        }
        if (aSync.Success)
            aResult.SourceMetadata += u" "_ustr + aSync.Entry.VisibleState;
        else
            aResult.SourceMetadata += u" review-state-sync-failed="_ustr + aSync.Message;
    }

    if (rEntry.ObjectId.isEmpty())
    {
        aResult.Summary = u"preview-failed reason=missing-object-id"_ustr;
        return aResult;
    }

    if (!IsSupportedPreviewTarget(aResult.Target))
    {
        aResult.Summary = u"preview-failed reason=unsupported-target target="_ustr
                          + aResult.Target;
        return aResult;
    }

    aResult.Success = true;
    aResult.Summary = u"preview id="_ustr + rEntry.ObjectId + u" type="_ustr + rEntry.Type
                      + u" target="_ustr + aResult.Target + u" mode="_ustr + aResult.Mode
                      + u" "_ustr + aResult.EvidenceBadge
                      + u" redacted=true hash-only=true read-only=true "_ustr
                      + aResult.SourceMetadata;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
