/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ImpressSlideElementPopover.hxx"
#include "SlideElementPopover.hxx"
#include "InlineActionRequest.hxx"
#include "InlineActionProviderDispatch.hxx"

#include <com/sun/star/beans/PropertyValue.hpp>
#include <comphelper/dispatchcommand.hxx>
#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Popover.hxx>
#include <vcl/weld/weld.hxx>

namespace sd::inline_actions {

ImpressSlideElementPopover::ImpressSlideElementPopover(weld::Widget* pParent)
    : m_pAnchorParent(pParent)
    , m_pDiffReviewParent(pParent)
    , m_pDocShell(nullptr)
    , m_nSlideIndex(0)
    , m_xBuilder(Application::CreateBuilder(
          pParent, u"modules/simpress/ui/slide-element-popover.ui"_ustr))
    , m_xPopover(m_xBuilder->weld_popover(u"SlideElementPopover"_ustr))
    , m_xRewriteText(m_xBuilder->weld_button(u"btn_rewrite_text"_ustr))
    , m_xAdjustColor(m_xBuilder->weld_button(u"btn_adjust_color"_ustr))
    , m_xRelayout(m_xBuilder->weld_button(u"btn_relayout"_ustr))
    , m_xTranslateText(m_xBuilder->weld_button(u"btn_translate_text"_ustr))
    , m_xAiInline(m_xBuilder->weld_button(u"btn_ai_inline"_ustr))
{
    m_xRewriteText->connect_clicked(LINK(this, ImpressSlideElementPopover, OnActionClicked));
    m_xAdjustColor->connect_clicked(LINK(this, ImpressSlideElementPopover, OnActionClicked));
    m_xRelayout->connect_clicked(LINK(this, ImpressSlideElementPopover, OnActionClicked));
    m_xTranslateText->connect_clicked(LINK(this, ImpressSlideElementPopover, OnActionClicked));
    if (m_xAiInline)
        m_xAiInline->connect_clicked(LINK(this, ImpressSlideElementPopover, OnActionClicked));
    m_xPopover->connect_closed(LINK(this, ImpressSlideElementPopover, OnPopoverClosed));
}

void ImpressSlideElementPopover::open(const tools::Rectangle& rRect, sal_Int32 nSlideIndex,
                                      const rtl::OUString& rElementId)
{
    m_nSlideIndex = nSlideIndex;
    m_sElementId = rElementId;
    m_aAnchorRect = rRect;
    m_xPopover->popup_at_rect(m_pAnchorParent, m_aAnchorRect, weld::Placement::End);
    m_bOpen = true;
}

void ImpressSlideElementPopover::close()
{
    if (!m_bOpen)
        return;

    m_bOpen = false;
    m_xPopover->popdown();
}

void ImpressSlideElementPopover::setDispatchContext(SfxObjectShell* pDocShell,
                                                    weld::Widget* pDiffReviewParent)
{
    m_pDocShell = pDocShell;
    m_pDiffReviewParent = pDiffReviewParent ? pDiffReviewParent : m_pAnchorParent;
}

void ImpressSlideElementPopover::dispatchSelected(SlideElementAction eAction,
                                                  const rtl::OUString& rUserPrompt)
{
    (void)rUserPrompt;
    const OUString aJson = buildImpressSlideElementRequest(toToken(eAction), m_nSlideIndex,
                                                           m_sElementId, u"offline"_ustr);
    SAL_INFO("sd.inline_actions", "inline_action_request=" << aJson);
    dispatchImpressInlineAction(aJson, eAction, m_pDocShell, m_pDiffReviewParent);
}

void ImpressSlideElementPopover::pickAction(SlideElementAction eAction)
{
    dispatchSelected(eAction, OUString());
    DismissSlideElementPopover();
}

IMPL_LINK(ImpressSlideElementPopover, OnActionClicked, weld::Button&, rButton, void)
{
    if (&rButton == m_xRewriteText.get())
        pickAction(SlideElementAction::RewriteText);
    else if (&rButton == m_xAdjustColor.get())
        pickAction(SlideElementAction::AdjustColor);
    else if (&rButton == m_xRelayout.get())
        pickAction(SlideElementAction::Relayout);
    else if (&rButton == m_xTranslateText.get())
        pickAction(SlideElementAction::TranslateText);
    else if (m_xAiInline && &rButton == m_xAiInline.get())
    {
        DismissSlideElementPopover();
        comphelper::dispatchCommand(u".uno:KQAIInlineEdit"_ustr,
                                    css::uno::Sequence<css::beans::PropertyValue>());
    }
}

IMPL_LINK_NOARG(ImpressSlideElementPopover, OnPopoverClosed, weld::Popover&, void)
{
    m_bOpen = false;
    NotifySlideElementPopoverDismissed();
}

std::unique_ptr<SlideElementPopover> CreateImpressSlideElementPopover(weld::Widget* pParent)
{
    return std::make_unique<ImpressSlideElementPopover>(pParent);
}

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
