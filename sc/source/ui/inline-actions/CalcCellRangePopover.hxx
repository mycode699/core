/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "CellRangePopover.hxx"

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

namespace sc::inline_actions {

class CalcCellRangePopover final : public CellRangePopover
{
public:
    explicit CalcCellRangePopover(weld::Widget* pParent);

    void open(const tools::Rectangle& rRect,
              const rtl::OUString& rCellRange) override;
    void close() override;
    void setDispatchContext(SfxObjectShell* pDocShell,
                            weld::Widget* pDiffReviewParent) override;
    void dispatchSelected(CellAction eAction,
                          const rtl::OUString& rUserPrompt) override;

private:
    DECL_LINK(OnActionClicked, weld::Button&, void);
    DECL_LINK(OnPopoverClosed, weld::Popover&, void);

    void pickAction(CellAction eAction);

    weld::Widget* m_pAnchorParent;
    weld::Widget* m_pDiffReviewParent;
    SfxObjectShell* m_pDocShell;
    tools::Rectangle m_aAnchorRect;
    rtl::OUString m_sCellRange;
    bool m_bOpen = false;

    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Popover> m_xPopover;
    std::unique_ptr<weld::Button> m_xExplainData;
    std::unique_ptr<weld::Button> m_xSuggestChart;
    std::unique_ptr<weld::Button> m_xGenerateFormula;
    std::unique_ptr<weld::Button> m_xFormatClean;
    std::unique_ptr<weld::Button> m_xFormatChange;
    std::unique_ptr<weld::Button> m_xAiInline;
};

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
