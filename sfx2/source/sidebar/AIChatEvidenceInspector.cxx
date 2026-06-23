/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: evidence inspector runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatEvidenceInspector.hxx"

#include "AIChatReviewQueueStore.hxx"
#include "AIChatReviewStateSyncStore.hxx"
#include "AIChatSourceProvenance.hxx"

#include <algorithm>

namespace sfx2::sidebar
{

bool AIChatEvidenceInspector::IsSupportedSourceType(const OUString& rSourceType)
{
    return rSourceType == u"evidence-record"_ustr || rSourceType == u"connector-result"_ustr
           || rSourceType == u"knowledge-index-result"_ustr
           || rSourceType == u"task-step"_ustr || rSourceType == u"review-item"_ustr
           || rSourceType == u"formatting-preview"_ustr;
}

AIChatEvidenceInspectionResult
AIChatEvidenceInspector::Inspect(const AIChatContentRegistryEntry& rEntry) const
{
    AIChatEvidenceInspectionResult aResult;
    aResult.SourceType = rEntry.Type;
    aResult.EvidenceId = rEntry.EvidenceId;
    aResult.HashReference = rEntry.HashReference;
    aResult.OpenTarget = u"evidence-inspector"_ustr;

    if (rEntry.ObjectId.isEmpty())
    {
        aResult.Summary = u"evidence-inspection-failed reason=missing-object-id"_ustr;
        return aResult;
    }
    if (!IsSupportedSourceType(rEntry.Type))
    {
        aResult.Summary = u"evidence-inspection-failed reason=unsupported-source-type type="_ustr
                          + rEntry.Type;
        return aResult;
    }
    if (rEntry.EvidenceId.isEmpty())
    {
        aResult.Summary = u"evidence-inspection-failed reason=missing-evidence-link source-id="_ustr
                          + rEntry.ObjectId;
        return aResult;
    }
    if (rEntry.HashReference.isEmpty())
    {
        aResult.Summary = u"evidence-inspection-failed reason=missing-hash-reference source-id="_ustr
                          + rEntry.ObjectId;
        return aResult;
    }

    AIChatSourceProvenance aProvenance;
    const std::vector<AIChatSourceProvenanceEntry> aEntries = aProvenance.LoadEntries();
    const auto it = std::find_if(aEntries.begin(), aEntries.end(),
                                 [&rEntry](const AIChatSourceProvenanceEntry& rSource) {
                                     return rSource.EvidenceId == rEntry.EvidenceId
                                            || rSource.ReviewId == rEntry.ObjectId
                                            || rSource.SourceId
                                                   == AIChatSourceProvenance::MakeSourceId(
                                                       rEntry.ObjectId);
                                 });

    if (it != aEntries.end())
    {
        aResult.SourceId = it->SourceId;
        aResult.CitationId = it->CitationId;
        aResult.SourceType = it->SourceType;
        aResult.OpenTarget = it->OpenTarget.isEmpty() ? u"evidence-inspector"_ustr
                                                      : it->OpenTarget;
    }
    else
    {
        aResult.SourceId = AIChatSourceProvenance::MakeSourceId(rEntry.ObjectId);
        aResult.CitationId = AIChatSourceProvenance::MakeCitationId(rEntry.ObjectId);
    }

    aResult.AuditTrail = u"audit-trail=metadata-only evidence-id="_ustr + rEntry.EvidenceId
                         + u" hash="_ustr + rEntry.HashReference
                         + u" redacted=true hash-only=true"_ustr;
    OUString sReviewState;
    if (AIChatReviewQueueStore::IsReviewQueueEntry(rEntry))
    {
        AIChatReviewStateSyncStore aStateSync;
        AIChatReviewStateSyncResult aSync = aStateSync.GetLatestState(rEntry.ObjectId);
        if (!aSync.Success)
        {
            const OUString sState = AIChatReviewStateSyncStore::NormalizeRegistryState(rEntry.State);
            aSync = aStateSync.RecordFromRegistry(
                rEntry, AIChatReviewStateSyncStore::TransitionForState(sState), sState,
                u"evidence-inspector"_ustr);
        }
        sReviewState = aSync.Success ? aSync.Entry.VisibleState : aSync.Message;
    }
    aResult.Success = true;
    aResult.Summary = u"evidence-inspected source-id="_ustr + aResult.SourceId
                      + u" source-type="_ustr + aResult.SourceType + u" citation-id="_ustr
                      + aResult.CitationId + u" evidence-id="_ustr + aResult.EvidenceId
                      + u" open-target=evidence-inspector"_ustr
                      + u" shows-citation-links=true shows-audit-trail=true"_ustr
                      + u" redacted=true hash-only=true read-only=true main-document-mutation=false"_ustr
                      + (sReviewState.isEmpty() ? OUString() : u" "_ustr + sReviewState);
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
