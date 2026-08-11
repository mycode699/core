/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: unified turn phases).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "WorkbenchPhase.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::control
{

OUString WorkbenchPhaseMachine::phaseId(WorkbenchPhase p)
{
    switch (p)
    {
        case WorkbenchPhase::Idle:
            return u"idle"_ustr;
        case WorkbenchPhase::Planning:
            return u"planning"_ustr;
        case WorkbenchPhase::Generating:
            return u"generating"_ustr;
        case WorkbenchPhase::ToolsOpen:
            return u"tools-open"_ustr;
        case WorkbenchPhase::AwaitingApply:
            return u"awaiting-apply"_ustr;
        case WorkbenchPhase::Applying:
            return u"applying"_ustr;
        case WorkbenchPhase::Applied:
            return u"applied"_ustr;
        case WorkbenchPhase::Failed:
            return u"failed"_ustr;
        case WorkbenchPhase::Cancelled:
            return u"cancelled"_ustr;
    }
    return u"idle"_ustr;
}

OUString WorkbenchPhaseMachine::phaseLabelZh(WorkbenchPhase p)
{
    switch (p)
    {
        case WorkbenchPhase::Idle:
            return u"就绪"_ustr;
        case WorkbenchPhase::Planning:
            return u"待确认计划"_ustr;
        case WorkbenchPhase::Generating:
            return u"生成中"_ustr;
        case WorkbenchPhase::ToolsOpen:
            return u"工具进行中"_ustr;
        case WorkbenchPhase::AwaitingApply:
            return u"待批准写回"_ustr;
        case WorkbenchPhase::Applying:
            return u"写回中"_ustr;
        case WorkbenchPhase::Applied:
            return u"已写回"_ustr;
        case WorkbenchPhase::Failed:
            return u"失败"_ustr;
        case WorkbenchPhase::Cancelled:
            return u"已取消"_ustr;
    }
    return u"就绪"_ustr;
}

std::vector<WorkbenchStep> WorkbenchPhaseMachine::parseStepsFromApproach(const OUString& approachMarkdown)
{
    std::vector<WorkbenchStep> out;
    if (approachMarkdown.isEmpty())
        return out;

    const OUString text = approachMarkdown;
    sal_Int32 start = 0;
    sal_Int32 idx = 0;
    while (start <= text.getLength() && static_cast<sal_Int32>(out.size()) < 12)
    {
        sal_Int32 end = text.indexOf(u'\n', start);
        if (end < 0)
            end = text.getLength();
        OUString line = text.copy(start, end - start).trim();
        start = end + 1;

        if (line.isEmpty())
            continue;
        // Strip markdown bullets / numbering
        while (!line.isEmpty()
               && (line[0] == u'-' || line[0] == u'*' || line[0] == u'•' || line[0] == u'·'
                   || line[0] == u'#'))
            line = line.copy(1).trim();
        // 1. / 1) / （1）
        while (!line.isEmpty() && line[0] >= u'0' && line[0] <= u'9')
            line = line.copy(1);
        while (!line.isEmpty()
               && (line[0] == u'.' || line[0] == u')' || line[0] == u'、' || line[0] == u'：'
                   || line[0] == u':' || line[0] == u' '))
            line = line.copy(1).trim();
        if (line.isEmpty() || line.getLength() < 2)
            continue;
        if (line.getLength() > 48)
            line = line.copy(0, 48) + u"…"_ustr;

        WorkbenchStep s;
        s.index = ++idx;
        s.titleZh = line;
        out.push_back(s);
    }
    return out;
}

bool WorkbenchPhaseMachine::isReadyForPrimaryInput(WorkbenchPhase p, bool streamOpen, bool toolsOpen)
{
    if (streamOpen || toolsOpen)
        return false;
    switch (p)
    {
        case WorkbenchPhase::Generating:
        case WorkbenchPhase::ToolsOpen:
        case WorkbenchPhase::Applying:
            return false;
        default:
            return true;
    }
}

bool WorkbenchPhaseMachine::isTurnBusy(WorkbenchPhase p, bool streamOpen, bool toolsOpen)
{
    if (streamOpen || toolsOpen)
        return true;
    return p == WorkbenchPhase::Generating || p == WorkbenchPhase::ToolsOpen
           || p == WorkbenchPhase::Applying;
}

WorkbenchSnapshot WorkbenchPhaseMachine::makeSnapshot(WorkbenchPhase phase,
                                                      const OUString& workPlanId,
                                                      const OUString& applyPlanId,
                                                      const std::vector<WorkbenchStep>& steps,
                                                      sal_Int32 currentStepIndex, bool streamOpen,
                                                      bool toolsOpen)
{
    WorkbenchSnapshot snap;
    snap.phase = phase;
    snap.workPlanId = workPlanId;
    snap.applyPlanId = applyPlanId;
    snap.steps = steps;
    snap.stepTotal = static_cast<sal_Int32>(steps.size());
    snap.stepIndex = currentStepIndex;
    snap.streamOpen = streamOpen;
    snap.toolsOpen = toolsOpen;
    snap.readyForPrimaryInput = isReadyForPrimaryInput(phase, streamOpen, toolsOpen);
    snap.phaseLabelZh = phaseLabelZh(phase);

    // Mark current/done on steps
    for (auto& s : snap.steps)
    {
        s.current = (snap.stepIndex > 0 && s.index == snap.stepIndex);
        s.done = (snap.stepIndex > 0 && s.index < snap.stepIndex);
    }

    OUStringBuffer bar;
    bar.append(u"步骤："_ustr);
    bar.append(snap.phaseLabelZh);
    if (snap.stepTotal > 0 && snap.stepIndex > 0)
    {
        bar.append(u" · "_ustr);
        bar.append(OUString::number(snap.stepIndex));
        bar.append(u"/");
        bar.append(OUString::number(snap.stepTotal));
        for (const auto& s : snap.steps)
        {
            if (s.current)
            {
                bar.append(u" · "_ustr);
                bar.append(s.titleZh);
                break;
            }
        }
    }
    else if (snap.stepTotal > 0 && phase == WorkbenchPhase::Planning)
    {
        bar.append(u" · 共 "_ustr);
        bar.append(OUString::number(snap.stepTotal));
        bar.append(u" 步 · 主文档未改"_ustr);
    }
    if (toolsOpen)
        bar.append(u" · 工具未结束"_ustr);
    if (streamOpen)
        bar.append(u" · 流式中"_ustr);
    if (!snap.readyForPrimaryInput)
        bar.append(u" · 后续消息将排队"_ustr);
    if (phase == WorkbenchPhase::AwaitingApply)
        bar.append(u" · 请批准或拒绝写回"_ustr);
    snap.stepBarZh = bar.makeStringAndClear();
    return snap;
}

bool WorkbenchPhaseMachine::canTransition(WorkbenchPhase from, WorkbenchPhase to)
{
    if (from == to)
        return true;
    // Terminal → Idle always ok
    if (to == WorkbenchPhase::Idle)
        return true;
    if (to == WorkbenchPhase::Cancelled || to == WorkbenchPhase::Failed)
        return true;

    switch (from)
    {
        case WorkbenchPhase::Idle:
            return to == WorkbenchPhase::Planning || to == WorkbenchPhase::Generating
                   || to == WorkbenchPhase::AwaitingApply;
        case WorkbenchPhase::Planning:
            return to == WorkbenchPhase::Generating || to == WorkbenchPhase::Idle
                   || to == WorkbenchPhase::Cancelled;
        case WorkbenchPhase::Generating:
            return to == WorkbenchPhase::ToolsOpen || to == WorkbenchPhase::AwaitingApply
                   || to == WorkbenchPhase::Applied || to == WorkbenchPhase::Failed
                   || to == WorkbenchPhase::Cancelled || to == WorkbenchPhase::Idle;
        case WorkbenchPhase::ToolsOpen:
            return to == WorkbenchPhase::Generating || to == WorkbenchPhase::AwaitingApply
                   || to == WorkbenchPhase::Failed || to == WorkbenchPhase::Cancelled;
        case WorkbenchPhase::AwaitingApply:
            return to == WorkbenchPhase::Applying || to == WorkbenchPhase::Cancelled
                   || to == WorkbenchPhase::Idle || to == WorkbenchPhase::Generating;
        case WorkbenchPhase::Applying:
            return to == WorkbenchPhase::Applied || to == WorkbenchPhase::Failed
                   || to == WorkbenchPhase::Cancelled;
        case WorkbenchPhase::Applied:
        case WorkbenchPhase::Failed:
        case WorkbenchPhase::Cancelled:
            return to == WorkbenchPhase::Idle || to == WorkbenchPhase::Planning
                   || to == WorkbenchPhase::Generating;
    }
    return false;
}

WorkbenchPhase WorkbenchPhaseMachine::transition(WorkbenchPhase from, WorkbenchPhase to,
                                                 bool bStrict)
{
    if (!bStrict || canTransition(from, to))
        return to;
    return from;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
