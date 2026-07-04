/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AICanvasUI — display formatting helpers for the canvas workflow.
 */

#include "AICanvasUI.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::canvas
{

CanvasUIDisplay AICanvasUI::buildDisplay(const CanvasSession& session)
{
    CanvasUIDisplay display;

    // Step label: "Step 3/7"
    display.stepLabel = u"Step "_ustr
        + OUString::number(session.currentStep + 1) + u"/"
        + OUString::number(session.estimatedSteps);

    // Progress bar
    display.progressBar = formatProgressBar(
        static_cast<double>(session.currentStep)
        / static_cast<double>(session.estimatedSteps));

    // Status text
    display.statusText = AICanvasMode::stateLabel(session.state);

    // Content preview from the current step (first 200 chars)
    if (session.currentStep < static_cast<sal_Int32>(session.steps.size()))
    {
        const auto& step = session.steps[session.currentStep];
        OUString text = step.generatedContent;
        if (text.getLength() > 200)
            text = text.copy(0, 200) + u"..."_ustr;
        display.contentPreview = text;
    }

    // Next action hint
    switch (session.state)
    {
        case CanvasState::Describing:
            display.nextActionHint = u"请描述当前步骤的需求"_ustr;
            break;
        case CanvasState::Reviewing:
            display.nextActionHint = u"确认以继续，或描述修改内容"_ustr;
            break;
        case CanvasState::Completed:
            display.nextActionHint = u"文档创建完成!"_ustr;
            break;
        case CanvasState::Cancelled:
            display.nextActionHint = u"画布模式已取消"_ustr;
            break;
        default:
            display.nextActionHint = u"..."_ustr;
            break;
    }

    return display;
}

OUString AICanvasUI::formatStepSummary(const CanvasStep& step)
{
    return u"[Step "_ustr + OUString::number(step.stepNumber) + u"] "
        + (step.confirmed ? u"✓ "_ustr : u"→ "_ustr)
        + step.userRequirement;
}

OUString AICanvasUI::formatProgressBar(double fraction, sal_Int32 width)
{
    OUStringBuffer buf;
    buf.append(u"["_ustr);

    sal_Int32 filled = static_cast<sal_Int32>(fraction * width);
    if (filled > width) filled = width;
    if (filled < 0) filled = 0;

    for (sal_Int32 i = 0; i < filled; i++)
        buf.append(u"█"_ustr);
    for (sal_Int32 i = filled; i < width; i++)
        buf.append(u"░"_ustr);

    buf.append(u"] "_ustr);
    buf.append(OUString::number(static_cast<sal_Int32>(fraction * 100)));
    buf.append(u"%"_ustr);

    return buf.makeStringAndClear();
}

} // namespace kqoffice::ai::canvas

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
