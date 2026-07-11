/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "WriterSelectToActPopover.hxx"
#include "SelectToActPopover.hxx"
#include "InlineActionRequest.hxx"

#include <com/sun/star/beans/PropertyValue.hpp>
#include <comphelper/dispatchcommand.hxx>
#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Popover.hxx>
#include <vcl/weld/weld.hxx>

namespace sw::inline_actions {

WriterSelectToActPopover::WriterSelectToActPopover(weld::Widget* pParent,
                                                   SfxObjectShell* pDocShell)
    : m_pAnchorParent(pParent)
    , m_pDocShell(pDocShell)
    , m_xBuilder(Application::CreateBuilder(
          pParent, u"modules/swriter/ui/select-to-act-popover.ui"_ustr))
    , m_xPopover(m_xBuilder->weld_popover(u"SelectToActPopover"_ustr))
    , m_xRewrite(m_xBuilder->weld_button(u"btn_rewrite"_ustr))
    , m_xExpand(m_xBuilder->weld_button(u"btn_expand"_ustr))
    , m_xShorten(m_xBuilder->weld_button(u"btn_shorten"_ustr))
    , m_xTranslateEn(m_xBuilder->weld_button(u"btn_translate_en"_ustr))
    , m_xFormatClean(m_xBuilder->weld_button(u"btn_format_clean"_ustr))
    , m_xExplain(m_xBuilder->weld_button(u"btn_explain"_ustr))
    , m_xCustom(m_xBuilder->weld_button(u"btn_custom"_ustr))
{
    m_xRewrite->connect_clicked(LINK(this, WriterSelectToActPopover, OnActionClicked));
    m_xExpand->connect_clicked(LINK(this, WriterSelectToActPopover, OnActionClicked));
    m_xShorten->connect_clicked(LINK(this, WriterSelectToActPopover, OnActionClicked));
    m_xTranslateEn->connect_clicked(LINK(this, WriterSelectToActPopover, OnActionClicked));
    m_xFormatClean->connect_clicked(LINK(this, WriterSelectToActPopover, OnActionClicked));
    m_xExplain->connect_clicked(LINK(this, WriterSelectToActPopover, OnActionClicked));
    m_xCustom->connect_clicked(LINK(this, WriterSelectToActPopover, OnActionClicked));
    m_xPopover->connect_closed(LINK(this, WriterSelectToActPopover, OnPopoverClosed));
}

void WriterSelectToActPopover::open(const tools::Rectangle& rRect,
                                    const rtl::OUString& rParagraphId)
{
    m_sParagraphId = rParagraphId;
    m_aAnchorRect = rRect;
    m_xPopover->popup_at_rect(m_pAnchorParent, m_aAnchorRect, weld::Placement::End);
    m_bOpen = true;
}

void WriterSelectToActPopover::close()
{
    if (!m_bOpen)
        return;

    m_bOpen = false;
    m_xPopover->popdown();
}

void WriterSelectToActPopover::dispatchSelected(ParagraphAction eAction,
                                                const rtl::OUString& rUserPrompt)
{
    const OUString aJson = buildWriterParagraphRequest(toToken(eAction), m_sParagraphId,
                                                       u"offline"_ustr, rUserPrompt);
    SAL_INFO("sw.inline_actions", "inline_action_request=" << aJson);
    if (actionRoutesToDiff(eAction))
        OpenDiffReviewForInlineAction(aJson, m_pAnchorParent, m_pDocShell);
}

void WriterSelectToActPopover::pickAction(ParagraphAction eAction)
{
    dispatchSelected(eAction, OUString());
    DismissSelectToActPopover();
}

IMPL_LINK(WriterSelectToActPopover, OnActionClicked, weld::Button&, rButton, void)
{
    if (&rButton == m_xRewrite.get())
        pickAction(ParagraphAction::Rewrite);
    else if (&rButton == m_xExpand.get())
        pickAction(ParagraphAction::Expand);
    else if (&rButton == m_xShorten.get())
        pickAction(ParagraphAction::Shorten);
    else if (&rButton == m_xTranslateEn.get())
        pickAction(ParagraphAction::TranslateEn);
    else if (&rButton == m_xFormatClean.get())
        pickAction(ParagraphAction::FormatClean);
    else if (&rButton == m_xExplain.get())
        pickAction(ParagraphAction::Explain);
    else if (&rButton == m_xCustom.get())
    {
        // Cursor-style freeform: hand off to Ctrl+K inline AI edit.
        DismissSelectToActPopover();
        comphelper::dispatchCommand(u".uno:KQAIInlineEdit"_ustr,
                                    css::uno::Sequence<css::beans::PropertyValue>());
    }
}

IMPL_LINK_NOARG(WriterSelectToActPopover, OnPopoverClosed, weld::Popover&, void)
{
    m_bOpen = false;
    NotifySelectToActPopoverDismissed();
}

std::unique_ptr<SelectToActPopover> CreateWriterSelectToActPopover(weld::Widget* pParent,
                                                                   SfxObjectShell* pDocShell)
{
    return std::make_unique<WriterSelectToActPopover>(pParent, pDocShell);
}

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
