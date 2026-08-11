/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: status dashboard).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "WorkbenchStatus.hxx"

#include "AiFirstRunGate.hxx"
#include "AiResourceEnvelope.hxx"
#include "WritebackPermission.hxx"
#include "ProviderSlotManifest.hxx"
#include "PolicyEvolutionGuard.hxx"
#include "FactRouter.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::control
{

OUString WorkbenchStatus::staticPoliciesMarkdownZh()
{
    OUStringBuffer b;
    b.append(u"### 策略\n"_ustr);
    b.append(u"- 写回："_ustr);
    b.append(WritebackPermission::policyHintZh());
    b.append(u"\n"_ustr);
    b.append(u"- YOLO 环境：`KQOFFICE_AI_WRITEBACK_YOLO`="_ustr);
    b.append(WritebackPermission::yoloEnabled() ? u"开"_ustr : u"关"_ustr);
    b.append(u"\n"_ustr);
    b.append(u"- 引导："_ustr);
    b.append(AiFirstRunGate::stateLabelZh(AiFirstRunGate::state()));
    b.append(u"\n"_ustr);
    b.append(u"- "_ustr);
    b.append(AiResourceEnvelope::summaryLineZh());
    b.append(u"\n"_ustr);
    b.append(u"- "_ustr);
    b.append(ProviderSlotRegistry::summaryLineZh(ProviderSlotRegistry::loadAll()));
    b.append(u"\n"_ustr);
    b.append(u"- "_ustr);
    b.append(PolicyEvolutionGuard::policyHintZh());
    b.append(u"\n"_ustr);
    b.append(u"- 路由：事实优先（FactRouter）· Direct / BoundedLoop / PlanGate\n"_ustr);
    return b.makeStringAndClear();
}

WorkbenchStatusReport WorkbenchStatus::build(const WorkbenchStatusInput& in)
{
    WorkbenchStatusReport out;
    out.readyForPrimaryInput = WorkbenchPhaseMachine::isReadyForPrimaryInput(
        in.phase, in.streamOpen, in.toolsOpen);

    OUStringBuffer chip;
    chip.append(WorkbenchPhaseMachine::phaseLabelZh(in.phase));
    if (in.queueSize > 0)
    {
        chip.append(u" · 排队"_ustr);
        chip.append(OUString::number(in.queueSize));
    }
    if (!out.readyForPrimaryInput)
        chip.append(u" · 忙"_ustr);
    if (!in.contextChipZh.isEmpty())
    {
        chip.append(u" · "_ustr);
        chip.append(in.contextChipZh);
    }
    if (!in.membershipChipZh.isEmpty())
    {
        chip.append(u" · "_ustr);
        chip.append(in.membershipChipZh);
    }
    out.chipZh = chip.makeStringAndClear();
    if (out.chipZh.getLength() > 64)
        out.chipZh = out.chipZh.copy(0, 64) + u"…"_ustr;

    OUStringBuffer md;
    md.append(u"## 工作台状态\n\n"_ustr);
    md.append(u"**阶段：** "_ustr);
    md.append(WorkbenchPhaseMachine::phaseLabelZh(in.phase));
    md.append(u" (`"_ustr);
    md.append(WorkbenchPhaseMachine::phaseId(in.phase));
    md.append(u"`)\n"_ustr);
    md.append(u"**主输入：** "_ustr);
    md.append(out.readyForPrimaryInput ? u"可立即发送"_ustr : u"忙碌 · 后续消息将排队"_ustr);
    md.append(u"\n"_ustr);
    if (!in.surface.isEmpty())
    {
        md.append(u"**表面：** "_ustr);
        md.append(in.surface);
        md.append(u"\n"_ustr);
    }
    if (!in.workPlanId.isEmpty())
    {
        md.append(u"**工作计划：** `"_ustr);
        md.append(in.workPlanId);
        md.append(u"`\n"_ustr);
    }
    if (!in.applyPlanId.isEmpty())
    {
        md.append(u"**写回计划：** `"_ustr);
        md.append(in.applyPlanId);
        md.append(u"`\n"_ustr);
    }
    md.append(u"**队列：** "_ustr);
    if (in.queueSize <= 0)
        md.append(u"空"_ustr);
    else
    {
        md.append(OUString::number(in.queueSize));
        md.append(u" 条"_ustr);
        if (!in.queueStatusZh.isEmpty())
        {
            md.append(u" · "_ustr);
            md.append(in.queueStatusZh);
        }
    }
    md.append(u"\n"_ustr);
    md.append(u"**流式：** "_ustr);
    md.append(in.streamOpen ? u"进行中"_ustr : u"否"_ustr);
    md.append(u" · **工具：** "_ustr);
    md.append(in.toolsOpen ? u"未结束"_ustr : u"无"_ustr);
    md.append(u"\n\n"_ustr);

    if (!in.contextChipZh.isEmpty())
    {
        md.append(u"### 上下文\n"_ustr);
        md.append(in.contextChipZh);
        md.append(u"\n\n"_ustr);
    }
    if (!in.membershipChipZh.isEmpty())
    {
        md.append(u"### 会员\n"_ustr);
        md.append(in.membershipChipZh);
        md.append(u"\n\n"_ustr);
    }

    md.append(staticPoliciesMarkdownZh());
    md.append(u"\n### 快捷\n"_ustr);
    md.append(u"- `/队列` · `/上下文用量` · `/写回策略` · `/诊断导出` · `/资源预算`\n"_ustr);
    md.append(u"- `/引擎` · `/策略护栏`\n"_ustr);
    md.append(u"- 写回仍须批准；主文档默认不改\n"_ustr);
    out.markdownZh = md.makeStringAndClear();
    return out;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
