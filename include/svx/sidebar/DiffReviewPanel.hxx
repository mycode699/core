/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-D: Diff Review sidebar).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <svx/svxdllapi.h>
#include <rtl/ustring.hxx>

#include <memory>
#include <vector>

class SfxObjectShell;

namespace weld
{
class Widget;
}

namespace svx::sidebar::diff_review
{

/** One applied patch row for the diff-review tree (Writer-agnostic). */
struct SVX_DLLPUBLIC DiffReviewPatchEntry
{
    OUString maPatchId;
    OUString maKind;
    OUString maStatus;
    /** True when the patch was applied to the document (status "ok" at open). */
    bool mbApplied = false;
};

class SVX_DLLPUBLIC DiffReviewPanel
{
public:
    virtual ~DiffReviewPanel() = default;

    virtual void populate(const rtl::OUString& rPlanId) = 0;

    virtual void populateFromPatchResults(const rtl::OUString& rPlanId,
                                          const std::vector<DiffReviewPatchEntry>& rEntries) = 0;

    virtual void markApplied(const rtl::OUString& rDiffId) = 0;

    virtual void handleAccept(const rtl::OUString& rDiffId) = 0;
    virtual void handleReject(const rtl::OUString& rDiffId) = 0;
};

SVX_DLLPUBLIC std::unique_ptr<DiffReviewPanel> CreateWeldDiffReviewPanel(weld::Widget* pParent);

SVX_DLLPUBLIC void ShowDiffReviewPanel(weld::Widget* pParent, const rtl::OUString& rPlanId,
                                       const std::vector<DiffReviewPatchEntry>& rEntries,
                                       SfxObjectShell* pDocShell = nullptr,
                                       sal_uInt32 nUndoActionsApplied = 0);

SVX_DLLPUBLIC void DismissDiffReviewPanel();

} // namespace svx::sidebar::diff_review

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */