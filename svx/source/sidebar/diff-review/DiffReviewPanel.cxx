/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-D: Diff Review sidebar).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "DiffReviewPanel.hxx"
#include "WeldDiffReviewPanel.hxx"

#include <sal/log.hxx>
#include <sal/types.h>
#include <vcl/svapp.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Dialog.hxx>
#include <vcl/weld/DialogController.hxx>
#include <vcl/weld/weld.hxx>

namespace svx::sidebar::diff_review {

namespace {

class DiffReviewDialog final : public weld::GenericDialogController
{
public:
    DiffReviewDialog(weld::Widget* pParent, SfxObjectShell* pDocShell)
        : GenericDialogController(pParent, u"svx/ui/diff-review-dialog.ui"_ustr,
                                  u"DiffReviewDialog"_ustr)
        , m_xPanelHost(m_xBuilder->weld_container(u"panel_host"_ustr))
        , m_xPanel(std::make_unique<WeldDiffReviewPanel>(m_xPanelHost.get(), pDocShell))
    {
    }

    WeldDiffReviewPanel& panel() { return *m_xPanel; }

private:
    std::unique_ptr<weld::Container> m_xPanelHost;
    std::unique_ptr<WeldDiffReviewPanel> m_xPanel;
};

std::shared_ptr<DiffReviewDialog> g_xActiveDialog;

struct PendingDiffReviewDialog
{
    weld::Widget* pParent;
    OUString aPlanId;
    std::vector<DiffReviewPatchEntry> aEntries;
    SfxObjectShell* pDocShell;
    sal_uInt32 nUndoActionsApplied;
};

void closeActiveDialog()
{
    if (g_xActiveDialog)
        g_xActiveDialog->response(RET_CLOSE);
}

void showDiffReviewDialog(weld::Widget* pParent, const OUString& rPlanId,
                          const std::vector<DiffReviewPatchEntry>& rEntries,
                          SfxObjectShell* pDocShell, sal_uInt32 nUndoActionsApplied)
{
    closeActiveDialog();

    auto xDialog = std::make_shared<DiffReviewDialog>(pParent, pDocShell);
    xDialog->panel().setUndoActionsApplied(nUndoActionsApplied);
    xDialog->panel().populateFromPatchResults(rPlanId, rEntries);
    g_xActiveDialog = xDialog;
    weld::DialogController::runAsync(xDialog, [xDialog](sal_Int32) {
        if (g_xActiveDialog == xDialog)
            g_xActiveDialog.reset();
    });

    SAL_INFO("svx.diff_review",
             "ShowDiffReviewPanel opened visible dialog plan_id="
                 << rPlanId << " entries=" << rEntries.size()
                 << " undo_actions=" << nUndoActionsApplied);
}

void ShowDiffReviewDialogAsync(void*, void* pArg)
{
    std::unique_ptr<PendingDiffReviewDialog> pPending(
        static_cast<PendingDiffReviewDialog*>(pArg));
    if (!pPending->pParent)
    {
        SAL_WARN("svx.diff_review", "ShowDiffReviewPanel: parent disappeared before UI dispatch");
        return;
    }

    showDiffReviewDialog(pPending->pParent, pPending->aPlanId, pPending->aEntries,
                         pPending->pDocShell, pPending->nUndoActionsApplied);
}

} // namespace

std::unique_ptr<DiffReviewPanel> CreateWeldDiffReviewPanel(weld::Widget* pParent)
{
    return std::make_unique<WeldDiffReviewPanel>(pParent);
}

void ShowDiffReviewPanel(weld::Widget* pParent, const rtl::OUString& rPlanId,
                         const std::vector<DiffReviewPatchEntry>& rEntries,
                         SfxObjectShell* pDocShell, sal_uInt32 nUndoActionsApplied)
{
    if (!pParent)
    {
        SAL_INFO("svx.diff_review", "ShowDiffReviewPanel: no parent widget");
        return;
    }

    if (!Application::IsMainThread())
    {
        auto pPending = std::make_unique<PendingDiffReviewDialog>();
        pPending->pParent = pParent;
        pPending->aPlanId = rPlanId;
        pPending->aEntries = rEntries;
        pPending->pDocShell = pDocShell;
        pPending->nUndoActionsApplied = nUndoActionsApplied;
        if (Application::PostUserEvent(LINK_NONMEMBER(nullptr, ShowDiffReviewDialogAsync),
                                       pPending.get()))
        {
            pPending.release();
            return;
        }

        SAL_WARN("svx.diff_review", "ShowDiffReviewPanel: failed to post UI event");
        return;
    }

    showDiffReviewDialog(pParent, rPlanId, rEntries, pDocShell, nUndoActionsApplied);
}

void DismissDiffReviewPanel() { closeActiveDialog(); }

} // namespace svx::sidebar::diff_review

// C ABI for Document AI Fabric (kqoffice/sfx2) — no link to svx required at
// compile time; resolved via dlsym(RTLD_DEFAULT) after svx is loaded.
extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_show_diff_review(
    void* pParentWidget, const sal_Unicode* pPlanId, sal_Int32 nPlanIdLen,
    const sal_Unicode* pPatchId, sal_Int32 nPatchIdLen, const sal_Unicode* pKind,
    sal_Int32 nKindLen, const sal_Unicode* pStatus, sal_Int32 nStatusLen,
    sal_Bool bApplied)
{
    auto* pParent = static_cast<weld::Widget*>(pParentWidget);
    if (!pParent || !pPlanId || nPlanIdLen <= 0)
        return;

    const OUString aPlanId(pPlanId, nPlanIdLen);
    svx::sidebar::diff_review::DiffReviewPatchEntry aEntry;
    if (pPatchId && nPatchIdLen > 0)
        aEntry.maPatchId = OUString(pPatchId, nPatchIdLen);
    else
        aEntry.maPatchId = u"p1"_ustr;
    if (pKind && nKindLen > 0)
        aEntry.maKind = OUString(pKind, nKindLen);
    else
        aEntry.maKind = u"replace"_ustr;
    if (pStatus && nStatusLen > 0)
        aEntry.maStatus = OUString(pStatus, nStatusLen);
    else
        aEntry.maStatus = bApplied ? u"ok"_ustr : u"pending"_ustr;
    aEntry.mbApplied = bApplied;

    svx::sidebar::diff_review::ShowDiffReviewPanel(pParent, aPlanId, { aEntry }, nullptr,
                                                   bApplied ? 1 : 0);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
