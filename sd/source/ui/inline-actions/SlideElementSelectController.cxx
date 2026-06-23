/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SlideElementSelectController.hxx"
#include "SlideElementPopover.hxx"

#include <DrawViewShell.hxx>
#include <o3tl/test_info.hxx>
#include <sdpage.hxx>
#include <Window.hxx>
#include <drawview.hxx>
#include <optional>
#include <rtl/ustring.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <svx/svdobj.hxx>
#include <svx/svdotext.hxx>
#include <tools/gen.hxx>
#include <vcl/svapp.hxx>

namespace sd::inline_actions {
namespace {

bool lcl_isEligibleSlideElementSelection(const DrawViewShell& rShell)
{
    const DrawView* pView = rShell.GetDrawView();
    if (!pView)
        return false;
    if (pView->IsTextEdit())
        return false;

    const SdrMarkList& rMarkList = pView->GetMarkedObjectList();
    if (rMarkList.GetMarkCount() != 1)
        return false;

    const SdrObject* pObj = rMarkList.GetMark(0)->GetMarkedSdrObj();
    if (!pObj || !DynCastSdrTextObj(pObj))
        return false;
    return true;
}

OUString lcl_sdObjElementId(const SdrObject& rObj)
{
    OUStringBuffer aBuf(u"sdobj-"_ustr);
    aBuf.append(OUString::number(reinterpret_cast<sal_uIntPtr>(&rObj), 16));
    return aBuf.makeStringAndClear();
}

std::optional<OUString> lcl_elementIdForSelection(const DrawViewShell& rShell)
{
    const DrawView* pView = rShell.GetDrawView();
    if (!pView)
        return std::nullopt;

    const SdrMarkList& rMarkList = pView->GetMarkedObjectList();
    if (rMarkList.GetMarkCount() != 1)
        return std::nullopt;

    const SdrObject* pObj = rMarkList.GetMark(0)->GetMarkedSdrObj();
    if (!pObj)
        return std::nullopt;

    return lcl_sdObjElementId(*pObj);
}

sal_Int32 lcl_slideIndexForShell(DrawViewShell& rShell)
{
    const SdPage* pPage = rShell.GetActualPage();
    if (!pPage)
        return 0;
    return static_cast<sal_Int32>((pPage->GetPageNum() - 1) / 2);
}

tools::Rectangle lcl_selectionAnchorRect(DrawViewShell& rShell, const SdrObject& rObj)
{
    ::sd::Window* pWin = rShell.GetActiveWindow();
    if (!pWin)
        return tools::Rectangle();

    const ::tools::Rectangle aLogicPix = pWin->LogicToPixel(rObj.GetLogicRect());
    const Point aBR = pWin->OutputToScreenPixel(aLogicPix.BottomRight());
    return tools::Rectangle(aBR, Size(1, 1));
}

void lcl_dismissPopover() { DismissSlideElementPopover(); }

bool lcl_isInteractivePopoverAllowed()
{
    return !Application::IsHeadlessModeEnabled() && !o3tl::IsRunningUnitTest()
           && !o3tl::IsRunningUITest();
}

} // namespace

void OnImpressSlideElementSelectionChanged(DrawViewShell& rShell)
{
    if (!lcl_isInteractivePopoverAllowed())
        return;

    if (!lcl_isEligibleSlideElementSelection(rShell))
    {
        lcl_dismissPopover();
        return;
    }

    const std::optional<OUString> oElementId = lcl_elementIdForSelection(rShell);
    if (!oElementId)
    {
        SAL_INFO("sd.inline_actions",
                 "OnImpressSlideElementSelectionChanged: no element id for selection");
        lcl_dismissPopover();
        return;
    }

    const SdrObject* pObj = rShell.GetDrawView()->GetMarkedObjectList().GetMark(0)->GetMarkedSdrObj();
    const tools::Rectangle aAnchor = lcl_selectionAnchorRect(rShell, *pObj);
    if (aAnchor.IsEmpty())
    {
        SAL_INFO("sd.inline_actions",
                 "OnImpressSlideElementSelectionChanged: no anchor rect for selection");
        lcl_dismissPopover();
        return;
    }

    ShowSlideElementPopover(rShell, aAnchor, lcl_slideIndexForShell(rShell), *oElementId);
}

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
