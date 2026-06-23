/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: workspace action bar).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatWorkspaceActionBarStore.hxx"

#include "AIChatReviewQueueStore.hxx"
#include "AIChatReviewStateSyncStore.hxx"

#include <algorithm>

namespace sfx2::sidebar
{
namespace
{
bool IsTaskCommand(const OUString& rCommand)
{
    return rCommand == u"retry"_ustr || rCommand == u"cancel"_ustr;
}

bool IsReviewCommand(const OUString& rCommand)
{
    return rCommand == u"open-diff-review"_ustr || rCommand == u"approve-selected"_ustr
           || rCommand == u"reject-selected"_ustr;
}

bool IsTaskStepEntry(const AIChatContentRegistryEntry& rEntry)
{
    return rEntry.Type == u"task-step"_ustr;
}

OUString BuildBaseMessage(const AIChatWorkspaceActionBarDispatchResult& rResult)
{
    OUString sMessage = u"workspace-action-dispatched command="_ustr + rResult.Command
                        + u" target-id="_ustr + rResult.TargetId + u" target-type="_ustr
                        + rResult.TargetType + u" open-target="_ustr + rResult.OpenTarget
                        + u" preview-mode="_ustr + rResult.PreviewMode;

    if (!rResult.ReviewState.isEmpty())
        sMessage += u" review-state="_ustr + rResult.ReviewState;
    if (!rResult.Reference.isEmpty())
        sMessage += u" reference="_ustr + rResult.Reference;

    return sMessage
           + u" visible-state=true keyboard-accessible=true uses-native-controls=true"_ustr
           + u" evidence-linked=true explicit-human-approval=true"_ustr
           + u" bulk-apply=false auto-apply=false hidden-action=false mouse-only=false"_ustr
           + u" read-only=true main-document-mutation=false"_ustr;
}
}

std::vector<OUString> AIChatWorkspaceActionBarStore::GetCommandRoster()
{
    return {
        u"open-preview"_ustr,    u"open-diff-review"_ustr, u"approve-selected"_ustr,
        u"reject-selected"_ustr, u"copy-reference"_ustr,   u"export-evidence"_ustr,
        u"filter"_ustr,          u"sort"_ustr,             u"retry"_ustr,
        u"cancel"_ustr,
    };
}

bool AIChatWorkspaceActionBarStore::IsSupportedCommand(const OUString& rCommand)
{
    const std::vector<OUString> aRoster = GetCommandRoster();
    return std::find(aRoster.begin(), aRoster.end(), rCommand) != aRoster.end();
}

bool AIChatWorkspaceActionBarStore::IsSupportedTargetType(const OUString& rTargetType)
{
    return rTargetType == u"task-step"_ustr || rTargetType == u"review-item"_ustr
           || rTargetType == u"artifact"_ustr || rTargetType == u"evidence-record"_ustr
           || rTargetType == u"preview"_ustr || rTargetType == u"formatting-preview"_ustr
           || rTargetType == u"connector-result"_ustr
           || rTargetType == u"knowledge-index-result"_ustr;
}

bool AIChatWorkspaceActionBarStore::RequiresSelectedTarget(const OUString& rCommand)
{
    return rCommand == u"open-preview"_ustr || rCommand == u"open-diff-review"_ustr
           || rCommand == u"approve-selected"_ustr || rCommand == u"reject-selected"_ustr
           || rCommand == u"copy-reference"_ustr || rCommand == u"export-evidence"_ustr;
}

bool AIChatWorkspaceActionBarStore::RequiresEvidenceLink(const OUString& rCommand)
{
    return RequiresSelectedTarget(rCommand);
}

OUString AIChatWorkspaceActionBarStore::MakeReference(const AIChatContentRegistryEntry& rEntry)
{
    if (AIChatReviewQueueStore::IsReviewQueueEntry(rEntry))
        return u"@review:"_ustr + rEntry.ObjectId;
    if (rEntry.Type == u"evidence-record"_ustr)
        return u"@evidence:"_ustr + rEntry.EvidenceId;
    return u"@artifact:"_ustr + rEntry.ObjectId;
}

bool AIChatWorkspaceActionBarStore::IsCommandEnabled(const OUString& rCommand,
                                                     const AIChatContentRegistryEntry* pEntry,
                                                     bool bRequestBusy,
                                                     bool bHasRetryPrompt)
{
    if (!IsSupportedCommand(rCommand))
        return false;

    if (rCommand == u"retry"_ustr)
        return !bRequestBusy && bHasRetryPrompt
               && (!pEntry || (IsTaskStepEntry(*pEntry) && pEntry->State == u"failed"_ustr));
    if (rCommand == u"cancel"_ustr)
        return bRequestBusy || (pEntry && IsTaskStepEntry(*pEntry)
                                && (pEntry->State == u"failed"_ustr
                                    || pEntry->State == u"running"_ustr));
    if (rCommand == u"filter"_ustr || rCommand == u"sort"_ustr)
        return !bRequestBusy;

    if (!pEntry)
        return false;
    if (RequiresEvidenceLink(rCommand)
        && (pEntry->EvidenceId.isEmpty() || pEntry->HashReference.isEmpty()))
        return false;

    if (rCommand == u"open-diff-review"_ustr)
        return pEntry->OpenTarget == u"diff-review"_ustr
               || AIChatReviewQueueStore::IsReviewQueueEntry(*pEntry);
    if (rCommand == u"approve-selected"_ustr || rCommand == u"reject-selected"_ustr)
        return AIChatReviewQueueStore::IsReviewQueueEntry(*pEntry);

    return true;
}

AIChatWorkspaceActionBarDispatchResult
AIChatWorkspaceActionBarStore::DispatchCommand(const OUString& rCommand,
                                               const AIChatContentRegistryEntry* pEntry,
                                               bool bRequestBusy,
                                               bool bHasRetryPrompt) const
{
    AIChatWorkspaceActionBarDispatchResult aResult;
    aResult.Command = rCommand;

    if (!IsSupportedCommand(rCommand))
    {
        aResult.Message = u"workspace-action-failed reason=unsupported-command command="_ustr
                          + rCommand;
        return aResult;
    }

    if (RequiresSelectedTarget(rCommand) && !pEntry)
    {
        aResult.Message = u"workspace-action-failed reason=missing-selected-target command="_ustr
                          + rCommand;
        return aResult;
    }

    if (pEntry)
    {
        aResult.TargetId = pEntry->ObjectId;
        aResult.TargetType = AIChatReviewQueueStore::IsReviewQueueEntry(*pEntry)
                                 ? u"review-item"_ustr
                                 : (IsTaskStepEntry(*pEntry)
                                        ? u"task-step"_ustr
                                        : (pEntry->Type == u"evidence-record"_ustr
                                               ? u"evidence-record"_ustr
                                               : u"artifact"_ustr));
        aResult.EvidenceId = pEntry->EvidenceId;
        aResult.HashReference = pEntry->HashReference;
        aResult.OpenTarget = pEntry->OpenTarget;
        aResult.PreviewMode = pEntry->PreviewMode;
    }
    else
    {
        aResult.TargetType = IsTaskCommand(rCommand) ? u"task-step"_ustr : u"artifact"_ustr;
        aResult.OpenTarget = IsTaskCommand(rCommand) ? u"task-progress"_ustr
                                                     : u"sidebar-workbench-header"_ustr;
        aResult.PreviewMode = u"metadata-summary"_ustr;
    }

    if (!IsSupportedTargetType(aResult.TargetType))
    {
        aResult.Message = u"workspace-action-failed reason=unsupported-target-type command="_ustr
                          + rCommand + u" target-type="_ustr + aResult.TargetType;
        return aResult;
    }

    if (RequiresEvidenceLink(rCommand)
        && (aResult.EvidenceId.isEmpty() || aResult.HashReference.isEmpty()))
    {
        aResult.Message = u"workspace-action-failed reason=missing-evidence-link command="_ustr
                          + rCommand + u" target-id="_ustr + aResult.TargetId;
        return aResult;
    }

    if (!IsCommandEnabled(rCommand, pEntry, bRequestBusy, bHasRetryPrompt))
    {
        aResult.Message = u"workspace-action-failed reason=disabled-state command="_ustr
                          + rCommand;
        return aResult;
    }

    if (rCommand == u"open-preview"_ustr)
    {
        aResult.OpenTarget = u"sidebar-preview"_ustr;
        if (aResult.PreviewMode.isEmpty())
            aResult.PreviewMode = u"metadata-summary"_ustr;
        if (pEntry && AIChatReviewQueueStore::IsReviewQueueEntry(*pEntry))
            aResult.ReviewState = AIChatReviewStateSyncStore::NormalizeRegistryState(pEntry->State);
    }
    else if (rCommand == u"open-diff-review"_ustr)
    {
        aResult.OpenTarget = u"diff-review"_ustr;
        aResult.PreviewMode = u"diff-preview"_ustr;
        if (pEntry)
            aResult.ReviewState = u"open"_ustr;
    }
    else if (rCommand == u"approve-selected"_ustr)
    {
        aResult.OpenTarget = u"review-queue"_ustr;
        aResult.PreviewMode = u"diff-preview"_ustr;
        aResult.ReviewState = u"approved"_ustr;
    }
    else if (rCommand == u"reject-selected"_ustr)
    {
        aResult.OpenTarget = u"review-queue"_ustr;
        aResult.PreviewMode = u"diff-preview"_ustr;
        aResult.ReviewState = u"rejected"_ustr;
    }
    else if (rCommand == u"copy-reference"_ustr && pEntry)
    {
        aResult.Reference = MakeReference(*pEntry);
    }
    else if (rCommand == u"export-evidence"_ustr)
    {
        aResult.OpenTarget = u"evidence-inspector"_ustr;
        aResult.PreviewMode = u"evidence-summary"_ustr;
    }
    else if (rCommand == u"filter"_ustr || rCommand == u"sort"_ustr)
    {
        aResult.OpenTarget = u"sidebar-workbench-header"_ustr;
        aResult.PreviewMode = u"metadata-summary"_ustr;
    }
    else if (rCommand == u"retry"_ustr)
    {
        aResult.OpenTarget = u"task-progress"_ustr;
        aResult.PreviewMode = u"metadata-summary"_ustr;
        aResult.ReviewState = u"failed"_ustr;
    }
    else if (rCommand == u"cancel"_ustr)
    {
        aResult.OpenTarget = u"task-progress"_ustr;
        aResult.PreviewMode = u"metadata-summary"_ustr;
        aResult.ReviewState = u"failed"_ustr;
    }

    aResult.Success = true;
    aResult.Message = BuildBaseMessage(aResult);
    if (IsReviewCommand(rCommand))
        aResult.Message += u" uses-diff-review=true"_ustr;
    if (rCommand == u"export-evidence"_ustr)
        aResult.Message += u" export-evidence=metadata-only redacted=true hash-only=true"_ustr;
    if (IsTaskCommand(rCommand))
        aResult.Message += u" task-control=true user-decision-required=true"_ustr
                           + u" auto-retry=false background-cancel=false"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
