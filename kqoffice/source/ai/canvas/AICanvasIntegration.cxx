/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AICanvasIntegration — bridge between canvas mode and AIChatPanel.
 */

#include "AICanvasIntegration.hxx"
#include "AICanvasUI.hxx"

#include <sal/log.hxx>

namespace kqoffice::ai::canvas
{

AICanvasMode AICanvasIntegration::s_canvasMode;
bool AICanvasIntegration::s_isActive = false;

bool AICanvasIntegration::isCanvasActive()
{
    return s_isActive;
}

bool AICanvasIntegration::startCanvasViaChat(CanvasDocType docType,
                                              const OUString& goal)
{
    try
    {
        s_canvasMode.startSession(docType, goal);
        s_isActive = true;

        SAL_INFO("kqoffice.ai.canvas",
                 "startCanvasViaChat: type=" << static_cast<int>(docType)
                     << " goal=\"" << goal << "\"");
        return true;
    }
    catch (...)
    {
        SAL_WARN("kqoffice.ai.canvas", "startCanvasViaChat failed");
        s_isActive = false;
        return false;
    }
}

OUString AICanvasIntegration::processCanvasMessage(const OUString& message)
{
    if (!s_isActive)
        return u"[画布模式未激活]"_ustr;

    auto result = s_canvasMode.submitRequirement(message);
    if (!result.success)
        return u"[错误] "_ustr + result.error;

    auto display = AICanvasUI::buildDisplay(s_canvasMode.getSession());

    OUString response;
    response += display.progressBar + u"\n"_ustr
        + display.stepLabel + u" | " + display.statusText + u"\n\n"_ustr
        + result.generatedContent + u"\n\n"_ustr
        + u"---\n"_ustr
        + display.nextActionHint;

    return response;
}

OUString AICanvasIntegration::processConfirm()
{
    if (!s_isActive)
        return u"[画布模式未激活]"_ustr;

    auto result = s_canvasMode.confirmStep();
    if (!result.success)
        return u"[错误] "_ustr + result.error;

    if (result.isComplete)
    {
        s_canvasMode.applyAllSteps();
        s_isActive = false;

        auto display = AICanvasUI::buildDisplay(s_canvasMode.getSession());
        return display.progressBar + u"\n"
            + u"✓ 文档创建完成! 所有步骤已应用。\n"_ustr;
    }

    auto display = AICanvasUI::buildDisplay(s_canvasMode.getSession());
    return display.progressBar + u"\n"
        + u"✓ 已确认。请描述下一步需求:\n"_ustr;
}

OUString AICanvasIntegration::processRevise(const OUString& feedback)
{
    if (!s_isActive)
        return u"[画布模式未激活]"_ustr;

    auto result = s_canvasMode.reviseStep(feedback);
    if (!result.success)
        return u"[错误] "_ustr + result.error;

    auto display = AICanvasUI::buildDisplay(s_canvasMode.getSession());

    OUString response;
    response += display.progressBar + u"\n"_ustr
        + u"已修改。请确认:\n\n"_ustr
        + result.generatedContent + u"\n\n"_ustr
        + u"---\n"_ustr
        + display.nextActionHint;

    return response;
}

const CanvasSession& AICanvasIntegration::getCurrentSession()
{
    return s_canvasMode.getSession();
}

void AICanvasIntegration::endCanvasSession()
{
    if (s_isActive)
    {
        s_canvasMode.applyAllSteps();
        s_isActive = false;
        SAL_INFO("kqoffice.ai.canvas", "endCanvasSession");
    }
}

} // namespace kqoffice::ai::canvas

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
