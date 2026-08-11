/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: fact-based route).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "FactRouter.hxx"

#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>
#include <initializer_list>

namespace kqoffice::ai::control
{

namespace
{
OUString lower(const OUString& s) { return s.toAsciiLowerCase(); }

bool hasAny(const OUString& hay, std::initializer_list<OUString> needles)
{
    for (const OUString& n : needles)
    {
        if (hay.indexOf(n) >= 0)
            return true;
    }
    return false;
}

sal_Int32 countApproxWords(const OUString& t)
{
    if (t.isEmpty())
        return 0;
    sal_Int32 spaces = 0;
    for (sal_Int32 i = 0; i < t.getLength(); ++i)
    {
        const sal_Unicode c = t[i];
        if (c == ' ' || c == '\n' || c == '\t' || c == u'、' || c == u'，' || c == u'。')
            ++spaces;
    }
    // CJK-heavy: mix space splits with char/2
    const sal_Int32 bySpace = spaces + 1;
    const sal_Int32 byCjk = (t.getLength() + 1) / 2;
    return bySpace > byCjk ? bySpace : byCjk;
}

sal_Int32 countEditVerbs(const OUString& low)
{
    sal_Int32 verbs = 0;
    if (hasAny(low, { u"润色"_ustr, u"改写"_ustr, u"rewrite"_ustr, u"polish"_ustr }))
        ++verbs;
    if (hasAny(low, { u"排版"_ustr, u"层级"_ustr, u"大纲"_ustr, u"layout"_ustr }))
        ++verbs;
    if (hasAny(low, { u"校对"_ustr, u"质检"_ustr, u"审阅"_ustr, u"proof"_ustr }))
        ++verbs;
    if (hasAny(low, { u"清洗"_ustr, u"公式"_ustr, u"汇总"_ustr, u"图表"_ustr }))
        ++verbs;
    if (hasAny(low, { u"多方案"_ustr, u"成片"_ustr, u"讲稿"_ustr }))
        ++verbs;
    return verbs;
}

bool envInt(const char* name, sal_Int32 def)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return def;
    return OString(v).toInt32();
}
} // namespace

sal_Int32 FactRouter::directMaxChars()
{
    const sal_Int32 n = envInt("KQOFFICE_AI_ROUTE_DIRECT_MAX_CHARS", 80);
    return n > 16 ? n : 80;
}

sal_Int32 FactRouter::planMinChars()
{
    const sal_Int32 n = envInt("KQOFFICE_AI_ROUTE_PLAN_MIN_CHARS", 36);
    return n > 8 ? n : 36;
}

OUString FactRouter::shapeId(RouteShape s)
{
    switch (s)
    {
        case RouteShape::Direct:
            return u"direct"_ustr;
        case RouteShape::BoundedLoop:
            return u"bounded-loop"_ustr;
        case RouteShape::PlanGate:
            return u"plan-gate"_ustr;
    }
    return u"direct"_ustr;
}

OUString FactRouter::shapeLabelZh(RouteShape s)
{
    switch (s)
    {
        case RouteShape::Direct:
            return u"直接生成"_ustr;
        case RouteShape::BoundedLoop:
            return u"受控多轮"_ustr;
        case RouteShape::PlanGate:
            return u"先出计划"_ustr;
    }
    return u"直接生成"_ustr;
}

TaskFacts FactRouter::measure(const FactRouteInput& in)
{
    TaskFacts f;
    const OUString t = in.prompt.trim();
    f.promptChars = t.getLength();
    f.approxWords = countApproxWords(t);
    f.selectionChars = in.selectionChars;
    f.hasSelection = in.hasSelection;
    f.surface = in.surface;
    f.forcedCapability = in.forcedCapability;
    f.agentCheckbox = in.agentCheckbox;
    f.forcePlan = in.forcePlanOnce;

    if (t.isEmpty())
        return f;

    const OUString low = lower(t);
    const OUString cap = lower(in.forcedCapability);

    f.pureQa = in.knownPureQa
               || (hasAny(low, { u"什么是"_ustr, u"是什么意思"_ustr, u"解释一下"_ustr,
                                 u"为什么"_ustr, u"what is"_ustr, u"explain"_ustr })
                   && !hasAny(low, { u"改写"_ustr, u"润色"_ustr, u"写回"_ustr, u"替换"_ustr }));
    f.continueTask
        = in.knownContinue
          || (hasAny(low, { u"继续"_ustr, u"接着"_ustr, u"go on"_ustr, u"continue"_ustr })
              && t.getLength() < 24);

    f.forcePlan = f.forcePlan || t.startsWith(u"/plan"_ustr) || t.startsWith(u"/规划"_ustr)
                  || t.startsWith(u"/工作计划"_ustr)
                  || hasAny(low, { u"先规划"_ustr, u"先出计划"_ustr, u"先做计划"_ustr,
                                   u"制定计划"_ustr, u"出个计划"_ustr, u"先对齐计划"_ustr });

    f.hasExplicitCheck = hasAny(low, { u"验收"_ustr, u"校验"_ustr, u"dry-run"_ustr, u"dryrun"_ustr,
                                       u"检查结果"_ustr, u"verify"_ustr });

    f.multiStepLanguage
        = hasAny(low, { u"分步"_ustr, u"多步"_ustr, u"然后再"_ustr, u"然后在"_ustr, u"先…再"_ustr,
                        u"第一步"_ustr, u"第二步"_ustr, u"plan-act"_ustr });

    f.fullDocLanguage
        = hasAny(low, { u"整篇"_ustr, u"全文"_ustr, u"整表"_ustr, u"整本"_ustr, u"全部页"_ustr,
                        u"所有页"_ustr, u"全面"_ustr, u"重构"_ustr, u"升级"_ustr, u"批量"_ustr,
                        u"从头到尾"_ustr, u"系统整理"_ustr, u"整体优化"_ustr });

    f.agentOrPlanCapability = cap == u"agent"_ustr || cap == u"plan"_ustr
                              || hasAny(cap, { u"agent"_ustr, u"plan"_ustr });

    f.editVerbCount = countEditVerbs(low);
    f.vagueLongEdit = f.promptChars >= 48
                      && hasAny(low, { u"优化"_ustr, u"处理"_ustr, u"整理"_ustr, u"改进"_ustr,
                                       u"完善"_ustr, u"改好"_ustr, u"弄好"_ustr });

    f.selectionEditLong = f.hasSelection && f.promptChars >= planMinChars()
                          && hasAny(low, { u"改写"_ustr, u"润色"_ustr, u"替换"_ustr, u"重写"_ustr,
                                           u"正式"_ustr, u"精简"_ustr, u"扩写"_ustr })
                          && !t.startsWith(u"/"_ustr);

    return f;
}

FactRouteDecision FactRouter::route(const FactRouteInput& in)
{
    FactRouteDecision d;
    d.facts = measure(in);

    auto finish = [&](RouteShape shape, const OUString& code, const OUString& reason) {
        d.shape = shape;
        d.shapeId = shapeId(shape);
        d.reasonCode = code;
        d.reasonZh = reason;
        d.needsWorkPlan = (shape == RouteShape::PlanGate);
        d.preferMultiRoundTools = (shape == RouteShape::BoundedLoop);
        d.needsComplexStartConfirm = d.needsWorkPlan
                                     && (d.facts.agentCheckbox || d.facts.agentOrPlanCapability
                                         || d.facts.fullDocLanguage);
        return d;
    };

    if (d.facts.promptChars == 0)
        return finish(RouteShape::Direct, u"empty"_ustr, u"空输入"_ustr);

    if (d.facts.continueTask)
        return finish(RouteShape::Direct, u"continue"_ustr, u"继续类指令 · 直接生成"_ustr);

    if (d.facts.pureQa)
        return finish(RouteShape::Direct, u"pure-qa"_ustr, u"咨询/问答 · 直接生成 · 不改主文档"_ustr);

    if (d.facts.forcePlan)
        return finish(RouteShape::PlanGate, u"force-plan"_ustr, u"用户要求先规划 · 计划门闸"_ustr);

    if (d.facts.fullDocLanguage)
        return finish(RouteShape::PlanGate, u"full-doc"_ustr, u"全文/整篇类范围 · 先出计划"_ustr);

    if (d.facts.multiStepLanguage)
        return finish(RouteShape::PlanGate, u"multi-step"_ustr, u"多步语言 · 先出计划"_ustr);

    if ((d.facts.agentCheckbox || d.facts.agentOrPlanCapability) && d.facts.promptChars >= 24)
        return finish(RouteShape::PlanGate, u"agent-cap"_ustr, u"Agent/规划能力 · 先出计划"_ustr);

    if (d.facts.editVerbCount >= 2 && d.facts.promptChars >= 16)
        return finish(RouteShape::PlanGate, u"compound-edit"_ustr, u"复合改稿意图 · 先出计划"_ustr);

    if (d.facts.vagueLongEdit)
        return finish(RouteShape::PlanGate, u"vague-long"_ustr, u"长而模糊的改稿 · 先出计划"_ustr);

    // Medium selection edit: tools loop, not full plan card
    if (d.facts.selectionEditLong && d.facts.promptChars < 80)
        return finish(RouteShape::BoundedLoop, u"selection-edit"_ustr,
                      u"选区改写 · 受控多轮工具 · 写回仍须批准"_ustr);

    if (d.facts.selectionEditLong)
        return finish(RouteShape::PlanGate, u"selection-edit-large"_ustr,
                      u"较长选区改写 · 先出计划"_ustr);

    // Short atomic with selection → direct proposal
    if (d.facts.hasSelection && d.facts.promptChars <= directMaxChars())
        return finish(RouteShape::Direct, u"short-selection"_ustr,
                      u"短选区任务 · 直接生成草案"_ustr);

    // No selection, moderate edit without full-doc markers → bounded loop (read skeleton)
    if (!d.facts.hasSelection && d.facts.editVerbCount >= 1 && d.facts.promptChars >= 20
        && d.facts.promptChars < 100)
        return finish(RouteShape::BoundedLoop, u"doc-context-loop"_ustr,
                      u"无选区改稿 · 受控读上下文多轮"_ustr);

    if (d.facts.hasExplicitCheck && d.facts.promptChars <= directMaxChars())
        return finish(RouteShape::Direct, u"checked-direct"_ustr,
                      u"带验收意图的短任务 · 直接生成"_ustr);

    return finish(RouteShape::Direct, u"default-direct"_ustr, u"默认直接生成 · 写回仍须批准"_ustr);
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
