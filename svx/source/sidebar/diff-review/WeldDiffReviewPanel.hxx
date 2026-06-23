/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-D: Diff Review sidebar).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <svx/sidebar/DiffReviewPanel.hxx>

#include <sfx2/sidebar/PanelLayout.hxx>
#include <tools/link.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>

class SfxObjectShell;

namespace svx::sidebar::diff_review {

/** Day-1 weld implementation of DiffReviewPanel (signal-only stub). */
class WeldDiffReviewPanel final : public PanelLayout, public DiffReviewPanel
{
public:
    explicit WeldDiffReviewPanel(weld::Widget* pParent, SfxObjectShell* pDocShell = nullptr);
    ~WeldDiffReviewPanel() override;

    void populate(const rtl::OUString& rPlanId) override;
    void populateFromPatchResults(const rtl::OUString& rPlanId,
                                  const std::vector<DiffReviewPatchEntry>& rEntries) override;
    void markApplied(const rtl::OUString& rDiffId) override;
    void handleAccept(const rtl::OUString& rDiffId) override;
    void handleReject(const rtl::OUString& rDiffId) override;

    void setUndoActionsApplied(sal_uInt32 nUndoActionsApplied);

private:
    DECL_LINK(OnAcceptClicked, weld::Button&, void);
    DECL_LINK(OnRejectClicked, weld::Button&, void);
    DECL_LINK(OnSelectionChanged, weld::TreeView&, void);

    OUString selectedDiffId() const;
    OUString treeIdForPatch(const OUString& rPatchId) const;
    DiffReviewPatchEntry* entryForPatchId(const OUString& rPatchId);
    const DiffReviewPatchEntry* entryForPatchId(const OUString& rPatchId) const;
    void refreshTreeRow(const DiffReviewPatchEntry& rEntry);
    void updateActionButtons();
    const DiffReviewPatchEntry* lastRemainingAppliedEntry() const;

    std::unique_ptr<weld::Label> m_xLabelPlanId;
    std::unique_ptr<weld::TreeView> m_xTreeDiffs;
    std::unique_ptr<weld::Button> m_xBtnAccept;
    std::unique_ptr<weld::Button> m_xBtnReject;

    rtl::OUString m_sPlanId;
    rtl::OUString m_sSelectedDiffId;
    SfxObjectShell* m_pDocShell;
    sal_uInt32 m_nUndoActionsApplied = 0;
    std::vector<DiffReviewPatchEntry> m_aEntries;
};

} // namespace svx::sidebar::diff_review

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */