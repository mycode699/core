/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "InlineActionSlideApply.hxx"

#include <DrawViewShell.hxx>
#include <drawview.hxx>
#include <sal/log.hxx>
#include <sdresid.hxx>
#include <strings.hrc>
#include <svx/svdmark.hxx>
#include <svx/svdobj.hxx>
#include <svx/svdotext.hxx>

namespace sd::inline_actions {
namespace {

SdrTextObj* lcl_singleMarkedTextObj(DrawViewShell& rShell)
{
    DrawView* pView = rShell.GetDrawView();
    if (!pView)
        return nullptr;
    if (pView->IsTextEdit())
        return nullptr;

    const SdrMarkList& rMarkList = pView->GetMarkedObjectList();
    if (rMarkList.GetMarkCount() != 1)
        return nullptr;

    SdrObject* pObj = rMarkList.GetMark(0)->GetMarkedSdrObj();
    if (!pObj)
        return nullptr;

    return DynCastSdrTextObj(pObj);
}

} // namespace

bool shouldApplyProviderContentToMarkedTextShape(SlideElementAction eAction)
{
    return eAction == SlideElementAction::RewriteText
           || eAction == SlideElementAction::TranslateText;
}

bool tryApplyProviderContentToMarkedTextShape(DrawViewShell& rShell, const OUString& rContent,
                                              SlideElementAction eAction)
{
    if (!shouldApplyProviderContentToMarkedTextShape(eAction))
        return false;

    if (rContent.isEmpty())
    {
        SAL_INFO("sd.inline_actions",
                 "tryApplyProviderContentToMarkedTextShape: empty provider content");
        return false;
    }

    SdrTextObj* pTextObj = lcl_singleMarkedTextObj(rShell);
    if (!pTextObj)
    {
        SAL_INFO("sd.inline_actions",
                 "tryApplyProviderContentToMarkedTextShape: no eligible marked text shape");
        return false;
    }

    DrawView* pView = rShell.GetDrawView();
    if (!pView)
        return false;

    pView->BegUndo(SdResId(STR_UNDO_REPLACE));
    pTextObj->SetText(rContent);
    pTextObj->SetEmptyPresObj(false);
    pView->EndUndo();

    SAL_INFO("sd.inline_actions",
             "tryApplyProviderContentToMarkedTextShape action=" << toToken(eAction)
             << " applied=true");
    return true;
}

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */