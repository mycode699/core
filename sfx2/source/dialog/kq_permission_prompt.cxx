/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (Wave D4: clarification card UI).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <kq_permission_prompt.hxx>

#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Dialog.hxx>
#include <vcl/vclenum.hxx>

using kqoffice::ai::control::ClarificationPrompt;
using kqoffice::ai::control::ClarificationResult;
using kqoffice::ai::control::PermissionDecision;
using kqoffice::ai::control::PermissionGrant;

namespace sfx2
{

namespace
{
// Custom response ids for the three decision buttons.
constexpr int RESP_DENY = RET_CANCEL;
constexpr int RESP_ONCE = RET_OK;
constexpr int RESP_SESSION = RET_YES;
}

KqPermissionPromptDialog::KqPermissionPromptDialog(weld::Widget* pParent,
                                                   const ClarificationPrompt& rPrompt)
    : GenericDialogController(pParent, u"sfx/ui/kq_permission_prompt.ui"_ustr,
                              u"KqPermissionPromptDialog"_ustr)
    , m_aActionId(rPrompt.actionId)
    , m_aOptions(rPrompt.options)
    , m_xMessage(m_xBuilder->weld_label(u"message"_ustr))
    , m_xOptionsLabel(m_xBuilder->weld_label(u"options_label"_ustr))
    , m_xOptions(m_xBuilder->weld_tree_view(u"options"_ustr))
    , m_xDenyBtn(m_xBuilder->weld_button(u"deny"_ustr))
    , m_xOnceBtn(m_xBuilder->weld_button(u"once"_ustr))
    , m_xSessionBtn(m_xBuilder->weld_button(u"session"_ustr))
{
    m_xDialog->set_title(u"需要确认"_ustr);
    m_xMessage->set_label(rPrompt.messageZh.isEmpty() ? u"请确认是否继续此操作。"_ustr
                                                      : rPrompt.messageZh);

    if (rPrompt.options.empty())
    {
        m_xOptionsLabel->hide();
        m_xOptions->hide();
    }
    else
    {
        m_xOptionsLabel->set_label(u"请选择适用项（可多选）："_ustr);
        m_xOptions->enable_toggle_buttons(weld::ColumnToggleType::Check);
        m_xOptions->set_clicks_to_toggle(1);
        for (size_t i = 0; i < rPrompt.options.size(); ++i)
        {
            const OUString id = OUString::number(static_cast<sal_Int32>(i));
            m_xOptions->append(id, rPrompt.options[i]);
            const bool bDefault = i < rPrompt.optionDefaults.size() && rPrompt.optionDefaults[i];
            m_xOptions->set_toggle(static_cast<int>(i),
                                   bDefault ? TRISTATE_TRUE : TRISTATE_FALSE);
        }
    }

    m_xDenyBtn->connect_clicked(LINK(this, KqPermissionPromptDialog, DenyHdl));
    m_xOnceBtn->connect_clicked(LINK(this, KqPermissionPromptDialog, OnceHdl));
    m_xSessionBtn->connect_clicked(LINK(this, KqPermissionPromptDialog, SessionHdl));
}

KqPermissionPromptDialog::~KqPermissionPromptDialog() = default;

IMPL_LINK_NOARG(KqPermissionPromptDialog, DenyHdl, weld::Button&, void)
{
    m_xDialog->response(RESP_DENY);
}

IMPL_LINK_NOARG(KqPermissionPromptDialog, OnceHdl, weld::Button&, void)
{
    m_xDialog->response(RESP_ONCE);
}

IMPL_LINK_NOARG(KqPermissionPromptDialog, SessionHdl, weld::Button&, void)
{
    m_xDialog->response(RESP_SESSION);
}

void KqPermissionPromptDialog::collectSelection(ClarificationResult& rOut) const
{
    rOut.selectedOptionIndices.clear();
    rOut.selectedOptions.clear();
    if (!m_xOptions || !m_xOptions->get_visible())
        return;

    const int n = m_xOptions->n_children();
    for (int i = 0; i < n; ++i)
    {
        if (m_xOptions->get_toggle(i) == TRISTATE_TRUE)
        {
            rOut.selectedOptionIndices.push_back(static_cast<sal_Int32>(i));
            if (i >= 0 && static_cast<size_t>(i) < m_aOptions.size())
                rOut.selectedOptions.push_back(m_aOptions[static_cast<size_t>(i)]);
            else
                rOut.selectedOptions.push_back(m_xOptions->get_text(i));
        }
    }
}

ClarificationResult KqPermissionPromptDialog::runPrompt()
{
    ClarificationResult result;
    const int nResp = run();
    collectSelection(result);
    result.fromSessionCache = false;

    switch (nResp)
    {
        case RESP_ONCE:
            result.decision = PermissionDecision::AllowOnce;
            PermissionGrant::applyDecision(m_aActionId, result.decision);
            break;
        case RESP_SESSION:
            result.decision = PermissionDecision::AllowSession;
            PermissionGrant::applyDecision(m_aActionId, result.decision);
            break;
        case RESP_DENY:
        default:
            result.decision = PermissionDecision::Deny;
            PermissionGrant::applyDecision(m_aActionId, result.decision);
            break;
    }
    return result;
}

ClarificationResult ShowPermissionPrompt(weld::Widget* pParent, const ClarificationPrompt& rPrompt)
{
    // Session cache short-circuit: 本轮对话均允许
    if (auto autoAllow = PermissionGrant::tryAutoAllow(rPrompt.actionId))
    {
        ClarificationResult result;
        result.decision = *autoAllow;
        result.fromSessionCache = true;
        return result;
    }

    KqPermissionPromptDialog dlg(pParent, rPrompt);
    return dlg.runPrompt();
}

} // namespace sfx2

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
