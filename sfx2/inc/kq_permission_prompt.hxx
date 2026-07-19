/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (Wave D4: clarification card UI).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Modal clarify / permission card:
 *   Title: 需要确认
 *   Buttons: 拒绝 | 仅本次 | 本轮对话均允许
 *
 * Call from future apply/delete paths via ShowPermissionPrompt().
 */

#pragma once

#include <vcl/weld/DialogController.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>

#include <vector>

#include <PermissionGrant.hxx>

namespace sfx2
{

/// Weld dialog for DuMate clarification + session permission UX.
class KqPermissionPromptDialog final : public weld::GenericDialogController
{
public:
    KqPermissionPromptDialog(weld::Widget* pParent,
                             const kqoffice::ai::control::ClarificationPrompt& rPrompt);
    virtual ~KqPermissionPromptDialog() override;

    /// Run modal; maps button responses to PermissionDecision and selected options.
    kqoffice::ai::control::ClarificationResult runPrompt();

private:
    DECL_LINK(DenyHdl, weld::Button&, void);
    DECL_LINK(OnceHdl, weld::Button&, void);
    DECL_LINK(SessionHdl, weld::Button&, void);

    void collectSelection(kqoffice::ai::control::ClarificationResult& rOut) const;

    OUString m_aActionId;
    std::vector<OUString> m_aOptions;

    std::unique_ptr<weld::Label> m_xMessage;
    std::unique_ptr<weld::Label> m_xOptionsLabel;
    std::unique_ptr<weld::TreeView> m_xOptions;
    std::unique_ptr<weld::Button> m_xDenyBtn;
    std::unique_ptr<weld::Button> m_xOnceBtn;
    std::unique_ptr<weld::Button> m_xSessionBtn;
};

/**
 * Present the clarification card (or auto-allow from session cache).
 *
 * Typical call site for apply / delete:
 *   ClarificationPrompt p;
 *   p.actionId = "apply.diff";
 *   p.messageZh = "将把 3 处修改写入文档，是否继续？";
 *   p.options = { u"保留批注"_ustr, u"同步更新目录"_ustr };
 *   auto r = sfx2::ShowPermissionPrompt(getDialog(), p);
 *   if (r.decision == PermissionDecision::Deny) return;
 *   // proceed with apply; r.selectedOptions for clarify answers
 */
kqoffice::ai::control::ClarificationResult
ShowPermissionPrompt(weld::Widget* pParent,
                     const kqoffice::ai::control::ClarificationPrompt& rPrompt);

} // namespace sfx2

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
