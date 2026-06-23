/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "SelectToActPopover.hxx"

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

namespace sw::inline_actions {

class WriterSelectToActPopover final : public SelectToActPopover
{
public:
    WriterSelectToActPopover(weld::Widget* pParent, SfxObjectShell* pDocShell);

    void open(const tools::Rectangle& rRect,
              const rtl::OUString& rParagraphId) override;
    void close() override;
    void dispatchSelected(ParagraphAction eAction,
                          const rtl::OUString& rUserPrompt) override;

private:
    DECL_LINK(OnActionClicked, weld::Button&, void);
    DECL_LINK(OnPopoverClosed, weld::Popover&, void);

    void pickAction(ParagraphAction eAction);

    weld::Widget* m_pAnchorParent;
    SfxObjectShell* m_pDocShell;
    tools::Rectangle m_aAnchorRect;
    rtl::OUString m_sParagraphId;
    bool m_bOpen = false;

    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Popover> m_xPopover;
    std::unique_ptr<weld::Button> m_xRewrite;
    std::unique_ptr<weld::Button> m_xExpand;
    std::unique_ptr<weld::Button> m_xShorten;
    std::unique_ptr<weld::Button> m_xTranslateEn;
    std::unique_ptr<weld::Button> m_xFormatClean;
    std::unique_ptr<weld::Button> m_xExplain;
    std::unique_ptr<weld::Button> m_xCustom;
};

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
