/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "SlideElementPopover.hxx"

#include <memory>
#include <tools/gen.hxx>
#include <tools/link.hxx>

class SfxObjectShell;

namespace weld
{
class Widget;
class Builder;
class Popover;
class Button;
}

namespace sd::inline_actions {

class ImpressSlideElementPopover final : public SlideElementPopover
{
public:
    explicit ImpressSlideElementPopover(weld::Widget* pParent);

    void open(const tools::Rectangle& rRect, sal_Int32 nSlideIndex,
              const rtl::OUString& rElementId) override;
    void close() override;
    void setDispatchContext(SfxObjectShell* pDocShell,
                            weld::Widget* pDiffReviewParent) override;
    void dispatchSelected(SlideElementAction eAction,
                          const rtl::OUString& rUserPrompt) override;

private:
    DECL_LINK(OnActionClicked, weld::Button&, void);
    DECL_LINK(OnPopoverClosed, weld::Popover&, void);

    void pickAction(SlideElementAction eAction);

    weld::Widget* m_pAnchorParent;
    weld::Widget* m_pDiffReviewParent;
    SfxObjectShell* m_pDocShell;
    tools::Rectangle m_aAnchorRect;
    rtl::OUString m_sElementId;
    sal_Int32 m_nSlideIndex;
    bool m_bOpen = false;

    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Popover> m_xPopover;
    std::unique_ptr<weld::Button> m_xRewriteText;
    std::unique_ptr<weld::Button> m_xAdjustColor;
    std::unique_ptr<weld::Button> m_xRelayout;
    std::unique_ptr<weld::Button> m_xTranslateText;
};

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
