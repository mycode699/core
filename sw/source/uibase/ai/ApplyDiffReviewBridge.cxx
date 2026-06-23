/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ApplyDiffReviewBridge.hxx"

#include <IntelligentWriterApplyEngine.hxx>
#include <doc.hxx>
#include <docsh.hxx>
#include <sal/log.hxx>
#include <svx/sidebar/DiffReviewPanel.hxx>
#include <viewsh.hxx>
#include <vcl/window.hxx>
#include <vcl/weld/weldutils.hxx>

#include <unordered_map>

namespace
{

weld::Widget* lcl_diffReviewParentWidget(SwViewShell& rShell)
{
    vcl::Window* pWin = rShell.GetWin();
    if (!pWin)
        return nullptr;
    tools::Rectangle aRect;
    if (weld::Window* pPopupParent = weld::GetPopupParent(*pWin, aRect))
        return pPopupParent;
    return pWin->GetFrameWeld();
}

std::vector<svx::sidebar::diff_review::DiffReviewPatchEntry> lcl_buildPatchEntries(
    const sw::intelligent::ApplyResult& rResult,
    const std::vector<sw::intelligent::Patch>* pPlanPatches)
{
    std::unordered_map<OUString, OUString> aKindByPatchId;
    if (pPlanPatches)
    {
        for (const sw::intelligent::Patch& rPatch : *pPlanPatches)
            aKindByPatchId.emplace(rPatch.maPatchId,
                                   OUString::fromUtf8(sw::intelligent::PatchKindToken(rPatch.meKind)));
    }

    std::vector<svx::sidebar::diff_review::DiffReviewPatchEntry> aEntries;
    aEntries.reserve(rResult.maPatchResults.size());
    for (const sw::intelligent::PatchResult& rPatchResult : rResult.maPatchResults)
    {
        svx::sidebar::diff_review::DiffReviewPatchEntry aEntry;
        aEntry.maPatchId = rPatchResult.maPatchId;
        aEntry.maStatus = rPatchResult.maStatus;
        aEntry.mbApplied = rPatchResult.maStatus == u"ok"_ustr;
        if (auto it = aKindByPatchId.find(rPatchResult.maPatchId); it != aKindByPatchId.end())
            aEntry.maKind = it->second;
        aEntries.push_back(aEntry);
    }
    return aEntries;
}

} // namespace

void ShowApplyPlanDiffReview(SwViewShell& rShell, const sw::intelligent::ApplyResult& rResult,
                             const rtl::OUString& rPlanId,
                             const std::vector<sw::intelligent::Patch>* pPlanPatches)
{
    weld::Widget* pParent = lcl_diffReviewParentWidget(rShell);
    if (!pParent)
    {
        SAL_INFO("sw.apply", "ShowApplyPlanDiffReview: no parent weld widget");
        return;
    }

    SfxObjectShell* pDocShell = rShell.GetDoc() ? rShell.GetDoc()->GetDocShell() : nullptr;
    const std::vector<svx::sidebar::diff_review::DiffReviewPatchEntry> aEntries
        = lcl_buildPatchEntries(rResult, pPlanPatches);
    const sal_uInt32 nUndoActionsApplied = static_cast<sal_uInt32>(rResult.mnAppliedCount);
    svx::sidebar::diff_review::ShowDiffReviewPanel(pParent, rPlanId, aEntries, pDocShell,
                                                   nUndoActionsApplied);

    SAL_INFO("sw.apply",
             "ShowApplyPlanDiffReview plan_id=" << rPlanId << " entries=" << aEntries.size()
                                                << " undo_actions=" << nUndoActionsApplied);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */