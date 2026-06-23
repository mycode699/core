/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-D: Diff Review sidebar).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "WeldDiffReviewPanel.hxx"

#include <sal/log.hxx>
#include <sfx2/objsh.hxx>
#include <svl/undo.hxx>
#include <vcl/weld/Builder.hxx>

#include <algorithm>

namespace svx::sidebar::diff_review {

namespace {

const OUString& stubDiffId()
{
    static const OUString sId(u"diff-stub-0"_ustr);
    return sId;
}

OUString lcl_entrySummary(const DiffReviewPatchEntry& rEntry)
{
    OUString aSummary = rEntry.maPatchId;
    if (!rEntry.maKind.isEmpty())
        aSummary += u" ["_ustr + rEntry.maKind + u"]"_ustr;
    if (!rEntry.maStatus.isEmpty())
        aSummary += u" — "_ustr + rEntry.maStatus;
    return aSummary;
}

bool lcl_isPendingProviderEntry(const DiffReviewPatchEntry& rEntry)
{
    return !rEntry.mbApplied && rEntry.maStatus == u"pending"_ustr;
}

} // namespace

WeldDiffReviewPanel::~WeldDiffReviewPanel() = default;

WeldDiffReviewPanel::WeldDiffReviewPanel(weld::Widget* pParent, SfxObjectShell* pDocShell)
    : PanelLayout(pParent, u"DiffReviewPanel"_ustr, u"svx/ui/diff-review-panel.ui"_ustr)
    , m_xLabelPlanId(m_xBuilder->weld_label(u"label_plan_id"_ustr))
    , m_xTreeDiffs(m_xBuilder->weld_tree_view(u"tree_diff_list"_ustr))
    , m_xBtnAccept(m_xBuilder->weld_button(u"btn_accept"_ustr))
    , m_xBtnReject(m_xBuilder->weld_button(u"btn_reject"_ustr))
    , m_pDocShell(pDocShell)
{
    m_xBtnAccept->connect_clicked(LINK(this, WeldDiffReviewPanel, OnAcceptClicked));
    m_xBtnReject->connect_clicked(LINK(this, WeldDiffReviewPanel, OnRejectClicked));
    m_xTreeDiffs->connect_selection_changed(
        LINK(this, WeldDiffReviewPanel, OnSelectionChanged));
}

void WeldDiffReviewPanel::setUndoActionsApplied(sal_uInt32 nUndoActionsApplied)
{
    m_nUndoActionsApplied = nUndoActionsApplied;
    SAL_INFO("svx.diff_review",
             "setUndoActionsApplied count=" << nUndoActionsApplied
                                            << " (one SfxUndo per applied patch; Accept reverts "
                                               "the last remaining patch — undo in apply order)");
}

void WeldDiffReviewPanel::populate(const rtl::OUString& rPlanId)
{
    DiffReviewPatchEntry aStub;
    aStub.maPatchId = stubDiffId();
    aStub.maKind = u"placeholder"_ustr;
    aStub.maStatus = u"pending"_ustr;
    aStub.mbApplied = false;
    populateFromPatchResults(rPlanId, { aStub });
}

void WeldDiffReviewPanel::populateFromPatchResults(
    const rtl::OUString& rPlanId, const std::vector<DiffReviewPatchEntry>& rEntries)
{
    m_sPlanId = rPlanId;
    m_aEntries = rEntries;
    m_xLabelPlanId->set_label(u"Apply plan: "_ustr + rPlanId);

    m_xTreeDiffs->clear();
    if (m_aEntries.empty())
    {
        m_xTreeDiffs->append(stubDiffId(), u"(no patches)"_ustr);
        m_xTreeDiffs->select(0);
        m_sSelectedDiffId = stubDiffId();
    }
    else
    {
        for (const DiffReviewPatchEntry& rEntry : m_aEntries)
        {
            const OUString sId = treeIdForPatch(rEntry.maPatchId);
            m_xTreeDiffs->append(sId, lcl_entrySummary(rEntry));
        }
        m_xTreeDiffs->select(0);
        m_sSelectedDiffId = treeIdForPatch(m_aEntries.front().maPatchId);
    }

    updateActionButtons();

    SAL_INFO("svx.diff_review",
             "populateFromPatchResults plan_id=" << rPlanId << " entries=" << m_aEntries.size()
                                                 << " undo_actions=" << m_nUndoActionsApplied);
}

void WeldDiffReviewPanel::markApplied(const rtl::OUString& rDiffId)
{
    if (DiffReviewPatchEntry* pEntry = entryForPatchId(rDiffId))
    {
        pEntry->mbApplied = true;
        if (pEntry->maStatus.isEmpty() || pEntry->maStatus == u"pending"_ustr)
            pEntry->maStatus = u"ok"_ustr;
        refreshTreeRow(*pEntry);
        updateActionButtons();
    }
    SAL_INFO("svx.diff_review",
             "markApplied plan_id=" << m_sPlanId << " diff_id=" << rDiffId);
}

OUString WeldDiffReviewPanel::treeIdForPatch(const OUString& rPatchId) const
{
    return rPatchId.isEmpty() ? stubDiffId() : rPatchId;
}

DiffReviewPatchEntry* WeldDiffReviewPanel::entryForPatchId(const OUString& rPatchId)
{
    for (DiffReviewPatchEntry& rEntry : m_aEntries)
    {
        if (rEntry.maPatchId == rPatchId)
            return &rEntry;
    }
    return nullptr;
}

const DiffReviewPatchEntry* WeldDiffReviewPanel::entryForPatchId(const OUString& rPatchId) const
{
    for (const DiffReviewPatchEntry& rEntry : m_aEntries)
    {
        if (rEntry.maPatchId == rPatchId)
            return &rEntry;
    }
    return nullptr;
}

void WeldDiffReviewPanel::refreshTreeRow(const DiffReviewPatchEntry& rEntry)
{
    const OUString sTreeId = treeIdForPatch(rEntry.maPatchId);
    const int nRow = m_xTreeDiffs->find_id(sTreeId);
    if (nRow >= 0)
        m_xTreeDiffs->set_text(nRow, lcl_entrySummary(rEntry));
}

const DiffReviewPatchEntry* WeldDiffReviewPanel::lastRemainingAppliedEntry() const
{
    for (auto it = m_aEntries.crbegin(); it != m_aEntries.crend(); ++it)
    {
        if (it->mbApplied && it->maStatus == u"ok"_ustr)
            return &*it;
    }
    return nullptr;
}

void WeldDiffReviewPanel::updateActionButtons()
{
    const DiffReviewPatchEntry* pEntry = entryForPatchId(selectedDiffId());
    const DiffReviewPatchEntry* pLastApplied = lastRemainingAppliedEntry();
    // Accept maps to one SfxUndoManager::Undo() — reverts the last applied patch still on
    // the stack (apply order). Enable only when the selected row is that patch.
    const bool bCanAccept = pEntry && pLastApplied && pEntry->maPatchId == pLastApplied->maPatchId;
    m_xBtnAccept->set_sensitive(bCanAccept);
    m_xBtnReject->set_sensitive(pEntry != nullptr);
}

void WeldDiffReviewPanel::handleAccept(const rtl::OUString& rDiffId)
{
    DiffReviewPatchEntry* pEntry = entryForPatchId(rDiffId);
    if (!pEntry)
    {
        SAL_INFO("svx.diff_review",
                 "handleAccept: unknown diff_id=" << rDiffId << " plan_id=" << m_sPlanId);
        return;
    }

    if (lcl_isPendingProviderEntry(*pEntry))
    {
        SAL_INFO("svx.diff_review",
                 "handleAccept: pending provider-only patch diff_id=" << rDiffId
                                                                      << " (no document change)");
        return;
    }

    if (!pEntry->mbApplied || pEntry->maStatus != u"ok"_ustr)
    {
        SAL_INFO("svx.diff_review",
                 "handleAccept: patch not undoable diff_id=" << rDiffId
                                                             << " status=" << pEntry->maStatus);
        return;
    }

    const DiffReviewPatchEntry* pLastApplied = lastRemainingAppliedEntry();
    if (!pLastApplied || pEntry->maPatchId != pLastApplied->maPatchId)
    {
        SAL_INFO("svx.diff_review",
                 "handleAccept: diff_id=" << rDiffId
                                          << " is not the last remaining applied patch ("
                                             "undo stack is LIFO; accept "
                                          << pLastApplied->maPatchId << " first)");
        return;
    }

    if (m_pDocShell)
    {
        if (SfxUndoManager* pUndo = m_pDocShell->GetUndoManager())
        {
            pUndo->Undo();
            pEntry->mbApplied = false;
            pEntry->maStatus = u"reverted"_ustr;
            refreshTreeRow(*pEntry);
            updateActionButtons();
            SAL_INFO("svx.diff_review",
                     "handleAccept: SfxUndoManager::Undo() once for diff_id=" << rDiffId
                     << " (per-patch undo groups; " << m_nUndoActionsApplied
                     << " patches applied at open)");
            return;
        }
    }

    SAL_INFO("svx.diff_review",
             "handleAccept: no undo manager for diff_id=" << rDiffId);
}

void WeldDiffReviewPanel::handleReject(const rtl::OUString& rDiffId)
{
    const DiffReviewPatchEntry* pEntry = entryForPatchId(rDiffId);
    const bool bWasApplied = pEntry && pEntry->mbApplied;

    m_aEntries.erase(
        std::remove_if(m_aEntries.begin(), m_aEntries.end(),
                       [&rDiffId](const DiffReviewPatchEntry& rEntry) {
                           return rEntry.maPatchId == rDiffId;
                       }),
        m_aEntries.end());

    const OUString sTreeId = treeIdForPatch(rDiffId);
    if (m_xTreeDiffs->find_id(sTreeId) >= 0)
        m_xTreeDiffs->remove_id(sTreeId);

    if (m_sSelectedDiffId == rDiffId)
    {
        std::unique_ptr<weld::TreeIter> xSel = m_xTreeDiffs->get_selected();
        if (xSel)
            m_sSelectedDiffId = m_xTreeDiffs->get_id(*xSel);
        else
            m_sSelectedDiffId.clear();
    }

    updateActionButtons();

    SAL_INFO("svx.diff_review",
             "handleReject plan_id=" << m_sPlanId << " diff_id=" << rDiffId
                                     << " was_applied=" << (bWasApplied ? "true" : "false")
                                     << " (document unchanged when patch was not applied)");
}

OUString WeldDiffReviewPanel::selectedDiffId() const
{
    if (m_sSelectedDiffId.isEmpty())
        return stubDiffId();
    return m_sSelectedDiffId;
}

IMPL_LINK(WeldDiffReviewPanel, OnAcceptClicked, weld::Button&, /*rButton*/, void)
{
    handleAccept(selectedDiffId());
}

IMPL_LINK(WeldDiffReviewPanel, OnRejectClicked, weld::Button&, /*rButton*/, void)
{
    handleReject(selectedDiffId());
}

IMPL_LINK(WeldDiffReviewPanel, OnSelectionChanged, weld::TreeView&, /*rTree*/, void)
{
    std::unique_ptr<weld::TreeIter> xSel = m_xTreeDiffs->get_selected();
    if (!xSel)
    {
        m_sSelectedDiffId.clear();
        updateActionButtons();
        return;
    }
    m_sSelectedDiffId = m_xTreeDiffs->get_id(*xSel);
    updateActionButtons();
}

} // namespace svx::sidebar::diff_review

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */