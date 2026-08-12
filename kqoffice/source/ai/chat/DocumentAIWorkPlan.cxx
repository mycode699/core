/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "DocumentAIWorkPlan.hxx"

#include "DocumentAIDocumentTools.hxx"
#include "DocumentAITaskBootstrap.hxx"
#include "WorkbenchPhase.hxx"
#include "FactRouter.hxx"

#include <rtl/ustrbuf.hxx>

#include <algorithm>

namespace kqoffice::ai::chat
{
namespace
{
OUString lower(const OUString& s) { return s.toAsciiLowerCase(); }

OUString clip(const OUString& s, sal_Int32 n)
{
    if (s.getLength() <= n)
        return s;
    return s.copy(0, n) + u"…"_ustr;
}

bool hasAny(const OUString& low, std::initializer_list<OUString> keys)
{
    for (const OUString& k : keys)
    {
        if (low.indexOf(k) >= 0)
            return true;
    }
    return false;
}

OUString surfaceZh(const OUString& s)
{
    if (s == u"writer"_ustr)
        return u"文字"_ustr;
    if (s == u"calc"_ustr)
        return u"表格"_ustr;
    if (s == u"impress"_ustr)
        return u"演示"_ustr;
    return u"办公"_ustr;
}

sal_Int32 nextPlanSerial()
{
    static sal_Int32 n = 0;
    ++n;
    if (n > 9999)
        n = 1;
    return n;
}
} // namespace

bool DocumentAIWorkPlan::looksLikeForcePlan(const OUString& rPrompt)
{
    const OUString t = rPrompt.trim();
    if (t.isEmpty())
        return false;
    const OUString low = lower(t);
    if (t.startsWith(u"/plan"_ustr) || t.startsWith(u"/规划"_ustr)
        || t.startsWith(u"/工作计划"_ustr))
        return true;
    if (hasAny(low, { u"先规划"_ustr, u"先出计划"_ustr, u"先做计划"_ustr, u"制定计划"_ustr,
                      u"出个计划"_ustr, u"工作计划"_ustr, u"先对齐计划"_ustr, u"plan mode"_ustr,
                      u"先 plan"_ustr }))
        return true;
    return false;
}

bool DocumentAIWorkPlan::looksLikeLargeTask(const OUString& rPrompt, const OUString& rSurface,
                                            bool bHasSelection, bool bAgentCheckbox,
                                            const OUString& rForcedCap)
{
    kqoffice::ai::control::FactRouteInput in;
    in.prompt = rPrompt;
    in.surface = rSurface;
    in.hasSelection = bHasSelection;
    in.selectionChars = 0;
    in.agentCheckbox = bAgentCheckbox
                       || DocumentAITaskBootstrap::looksLikeDevOrAgent(rPrompt, bAgentCheckbox,
                                                                       rForcedCap);
    in.forcedCapability = rForcedCap;
    in.forcePlanOnce = looksLikeForcePlan(rPrompt);
    in.knownContinue = DocumentAITaskBootstrap::looksLikeContinue(rPrompt);
    in.knownPureQa = DocumentAITaskBootstrap::looksLikePureQa(rPrompt);
    return kqoffice::ai::control::FactRouter::route(in).needsWorkPlan;
}

bool DocumentAIWorkPlan::prefersBoundedLoop(const OUString& rPrompt, const OUString& rSurface,
                                            bool bHasSelection, sal_Int32 nSelectionChars,
                                            bool bAgentCheckbox, const OUString& rForcedCap)
{
    kqoffice::ai::control::FactRouteInput in;
    in.prompt = rPrompt;
    in.surface = rSurface;
    in.hasSelection = bHasSelection;
    in.selectionChars = nSelectionChars;
    in.agentCheckbox = bAgentCheckbox;
    in.forcedCapability = rForcedCap;
    in.forcePlanOnce = looksLikeForcePlan(rPrompt);
    in.knownContinue = DocumentAITaskBootstrap::looksLikeContinue(rPrompt);
    in.knownPureQa = DocumentAITaskBootstrap::looksLikePureQa(rPrompt);
    return kqoffice::ai::control::FactRouter::route(in).preferMultiRoundTools;
}

WorkPlanAction DocumentAIWorkPlan::classifyAction(const OUString& rPrompt)
{
    const OUString t = rPrompt.trim();
    if (t.isEmpty())
        return WorkPlanAction::None;
    const OUString low = lower(t);

    if (t.startsWith(u"/view-plan"_ustr) || t.startsWith(u"/show-plan"_ustr)
        || t.startsWith(u"/查看计划"_ustr) || low == u"查看计划"_ustr || low == u"显示计划"_ustr)
        return WorkPlanAction::Show;

    if (t.startsWith(u"/cancel-plan"_ustr) || t.startsWith(u"/取消计划"_ustr)
        || low == u"取消计划"_ustr || low == u"放弃计划"_ustr || low == u"不要计划了"_ustr)
        return WorkPlanAction::Cancel;

    if (t.startsWith(u"/revise-plan"_ustr) || t.startsWith(u"/修改计划"_ustr)
        || t.startsWith(u"修改计划"_ustr) || t.startsWith(u"改计划"_ustr)
        || low.startsWith(u"计划改成"_ustr) || low.startsWith(u"计划调整"_ustr))
        return WorkPlanAction::Revise;

    if (t.startsWith(u"/approve-plan"_ustr) || t.startsWith(u"/执行计划"_ustr)
        || t.startsWith(u"/按此计划"_ustr) || low == u"按此计划执行"_ustr
        || low == u"按计划执行"_ustr || low == u"执行计划"_ustr || low == u"批准计划"_ustr
        || low == u"同意计划"_ustr || low == u"就按这个做"_ustr || low == u"开始执行"_ustr
        || low == u"可以执行"_ustr || low == u"ok plan"_ustr || low == u"approve plan"_ustr)
        return WorkPlanAction::Approve;

    return WorkPlanAction::None;
}

OUString DocumentAIWorkPlan::extractReviseNotes(const OUString& rPrompt)
{
    const OUString t = rPrompt.trim();
    auto after = [&](const OUString& key) -> OUString {
        if (t.startsWith(key))
            return t.copy(key.getLength()).trim();
        return OUString();
    };
    OUString n = after(u"/revise-plan"_ustr);
    if (n.isEmpty())
        n = after(u"/修改计划"_ustr);
    if (n.isEmpty())
        n = after(u"修改计划："_ustr);
    if (n.isEmpty())
        n = after(u"修改计划:"_ustr);
    if (n.isEmpty())
        n = after(u"修改计划"_ustr);
    if (n.isEmpty())
        n = after(u"改计划："_ustr);
    if (n.isEmpty())
        n = after(u"改计划:"_ustr);
    if (n.isEmpty())
        n = after(u"改计划"_ustr);
    if (n.isEmpty())
        n = after(u"计划改成"_ustr);
    if (n.isEmpty())
        n = after(u"计划调整"_ustr);
    // Drop leading colon/punctuation
    while (!n.isEmpty() && (n[0] == u':' || n[0] == u'：' || n[0] == u' '))
        n = n.copy(1).trim();
    return n;
}

OUString DocumentAIWorkPlan::stripPlanForcePrefix(const OUString& rPrompt)
{
    OUString t = rPrompt.trim();
    auto strip = [&](const OUString& key) {
        if (t.startsWith(key))
            t = t.copy(key.getLength()).trim();
    };
    strip(u"/plan "_ustr);
    strip(u"/plan"_ustr);
    strip(u"/规划 "_ustr);
    strip(u"/规划"_ustr);
    strip(u"/工作计划 "_ustr);
    strip(u"先规划："_ustr);
    strip(u"先规划:"_ustr);
    strip(u"先规划 "_ustr);
    strip(u"先规划"_ustr);
    strip(u"先出计划："_ustr);
    strip(u"先出计划 "_ustr);
    strip(u"制定计划："_ustr);
    strip(u"制定计划 "_ustr);
    return t;
}

OUString DocumentAIWorkPlan::formatMarkdown(const WorkPlan& rPlan)
{
    OUStringBuffer b;
    b.append(u"# 工作计划（待确认）\n\n"_ustr);
    b.append(u"**计划 ID：** `"_ustr);
    b.append(rPlan.planId);
    b.append(u"` · 表面："_ustr);
    b.append(surfaceZh(rPlan.surface));
    b.append(u" · v"_ustr);
    b.append(OUString::number(rPlan.version));
    b.append(u"\n\n"_ustr);

    b.append(u"## 目标\n"_ustr);
    b.append(rPlan.objective.isEmpty() ? u"（未命名）"_ustr : rPlan.objective);
    b.append(u"\n\n"_ustr);

    b.append(u"## 推荐做法\n"_ustr);
    b.append(rPlan.approach.isEmpty() ? u"1. 读上下文 → 2. 生成草案 → 3. 待批写回\n"_ustr
                                      : rPlan.approach);
    if (!rPlan.approach.endsWith(u"\n"_ustr))
        b.append(u"\n"_ustr);
    b.append(u"\n"_ustr);

    b.append(u"## 范围\n"_ustr);
    b.append(u"- **做：** "_ustr);
    b.append(rPlan.scopeIn.isEmpty() ? u"当前文档/选区"_ustr : rPlan.scopeIn);
    b.append(u"\n- **不做：** "_ustr);
    b.append(rPlan.scopeOut.isEmpty() ? u"未经批准不改主文档；不静默上传"_ustr
                                      : rPlan.scopeOut);
    b.append(u"\n\n"_ustr);

    b.append(u"## 风险\n"_ustr);
    b.append(rPlan.risks.isEmpty() ? u"- 范围过大时可能改动超出预期 — 批准写回前请看 Diff\n"_ustr
                                   : rPlan.risks);
    if (!rPlan.risks.endsWith(u"\n"_ustr))
        b.append(u"\n"_ustr);
    b.append(u"\n"_ustr);

    b.append(u"## 验收\n"_ustr);
    b.append(rPlan.acceptance.isEmpty() ? u"- 用户批准写回后主文档变化可感知；可撤销\n"_ustr
                                        : rPlan.acceptance);
    if (!rPlan.acceptance.endsWith(u"\n"_ustr))
        b.append(u"\n"_ustr);
    b.append(u"\n"_ustr);

    if (!rPlan.skillTitleZh.isEmpty() || !rPlan.skillId.isEmpty())
    {
        b.append(u"## 拟用技能\n"_ustr);
        b.append(rPlan.skillTitleZh.isEmpty() ? rPlan.skillId : rPlan.skillTitleZh);
        if (!rPlan.skillId.isEmpty())
        {
            b.append(u" (`"_ustr);
            b.append(rPlan.skillId);
            b.append(u"`)"_ustr);
        }
        b.append(u"\n\n"_ustr);
    }

    if (!rPlan.reviseNotes.isEmpty())
    {
        b.append(u"## 你的修订意见\n"_ustr);
        b.append(rPlan.reviseNotes);
        b.append(u"\n\n"_ustr);
    }

    b.append(u"---\n"_ustr);
    b.append(u"**下一步：** 回复 **按此计划执行** 或 `/approve-plan` 开始生成写回**草案**"_ustr);
    b.append(u"（仍须「批准写回」才改主文档）。\n"_ustr);
    b.append(u"改计划：`修改计划：你的补充` · 取消：`/cancel-plan` · 再看：`/view-plan`\n"_ustr);
    b.append(u"\n> 一次批准工作计划 ≠ 永久静默改稿；写回仍是独立批准。\n"_ustr);
    return b.makeStringAndClear();
}

WorkPlan DocumentAIWorkPlan::build(const WorkPlanInput& rIn)
{
    WorkPlan p;
    if (rIn.prior)
    {
        p = *rIn.prior;
        p.version = rIn.prior->version + 1;
        if (!rIn.reviseNotes.isEmpty())
            p.reviseNotes = rIn.reviseNotes;
    }
    else
    {
        p.planId = u"wp-"_ustr + OUString::number(nextPlanSerial());
        p.version = 1;
    }

    const OUString raw = stripPlanForcePrefix(
        rIn.prior && !rIn.prior->originalPrompt.isEmpty() ? rIn.prior->originalPrompt
                                                          : rIn.userPrompt);
    p.originalPrompt = rIn.prior && !rIn.prior->originalPrompt.isEmpty()
                           ? rIn.prior->originalPrompt
                           : rIn.userPrompt.trim();
    p.surface = rIn.surface.isEmpty() ? u"any"_ustr : rIn.surface;
    p.skillId = rIn.skillId.isEmpty() && rIn.prior ? rIn.prior->skillId : rIn.skillId;
    p.skillTitleZh
        = rIn.skillTitleZh.isEmpty() && rIn.prior ? rIn.prior->skillTitleZh : rIn.skillTitleZh;
    if (!rIn.reviseNotes.isEmpty())
        p.reviseNotes = rIn.reviseNotes;

    // Objective
    if (!rIn.bootstrapObjective.isEmpty())
        p.objective = rIn.bootstrapObjective;
    else
        p.objective = clip(raw.isEmpty() ? u"文档质量改稿"_ustr : raw, 80);

    // Scope
    OUStringBuffer scope;
    scope.append(surfaceZh(p.surface));
    if (rIn.hasSelection)
    {
        scope.append(u" · 选区 "_ustr);
        scope.append(OUString::number(rIn.selectionChars));
        scope.append(u" 字"_ustr);
    }
    else
        scope.append(u" · 当前文档/上下文"_ustr);
    if (!p.skillTitleZh.isEmpty())
    {
        scope.append(u" · 技能 "_ustr);
        scope.append(p.skillTitleZh);
    }
    p.scopeIn = scope.makeStringAndClear();
    p.scopeOut = u"未经「批准写回」不改主文档；不编造事实；不静默上传；"
                 u"不扩大到未声明的整库/外链任务"_ustr;

    const OUString low = lower(raw);
    const OUString surf = p.surface;

    // Approach by surface / intent
    OUStringBuffer ap;
    if (surf == u"calc"_ustr
        || hasAny(low, { u"公式"_ustr, u"清洗"_ustr, u"表格"_ustr, u"汇总"_ustr, u"图表"_ustr }))
    {
        ap.append(u"1. 探查选区/表头与数据形态\n"_ustr);
        ap.append(u"2. 生成问题清单或公式草案（含单元格引用）\n"_ustr);
        ap.append(u"3. 公式 dry-run / 清洗写回块暂存\n"_ustr);
        ap.append(u"4. 你确认 Diff 后批准写回（可撤销）\n"_ustr);
    }
    else if (surf == u"impress"_ustr
             || hasAny(low, { u"幻灯"_ustr, u"演示"_ustr, u"成片"_ustr, u"多方案"_ustr }))
    {
        ap.append(u"1. 大纲/页序（禁止黑盒一次成片）\n"_ustr);
        ap.append(u"2. 多方案对比（若需要）\n"_ustr);
        ap.append(u"3. 选一生成可写回 ## 页结构\n"_ustr);
        ap.append(u"4. 批准写回 → 可选导出 PPTX 指引\n"_ustr);
    }
    else if (hasAny(low, { u"pdf"_ustr, u"扫描"_ustr }))
    {
        ap.append(u"1. 确认本地提取文本是否足够（不足则 OCR 提示）\n"_ustr);
        ap.append(u"2. 摘要/问答/大纲（只依据已提取文本）\n"_ustr);
        ap.append(u"3. 可编辑大纲可转入 Writer；不宣称 Acrobat 编辑\n"_ustr);
    }
    else if (hasAny(low, { u"排版"_ustr, u"层级"_ustr, u"大纲写回"_ustr, u"标题"_ustr }))
    {
        ap.append(u"1. 诊断标题层级与结构问题\n"_ustr);
        ap.append(u"2. 建议标题树（H1–H3）\n"_ustr);
        ap.append(u"3. 输出可圈大纲写回块（按标题软匹配）\n"_ustr);
        ap.append(u"4. Diff + 批准后设标题样式\n"_ustr);
    }
    else if (hasAny(low, { u"质检"_ustr, u"校对"_ustr, u"审阅"_ustr, u"打分"_ustr }))
    {
        ap.append(u"1. 通读与四维/严重度诊断\n"_ustr);
        ap.append(u"2. Top 问题 + 最小改动建议\n"_ustr);
        ap.append(u"3. 可选 FIX| 写回块；无写回块则咨询收口\n"_ustr);
        ap.append(u"4. 有 FIX 时经 Diff 批准再写回\n"_ustr);
    }
    else
    {
        ap.append(u"1. 对齐目标与范围（本计划）\n"_ustr);
        ap.append(u"2. 读选区/文档上下文，生成改稿草案\n"_ustr);
        ap.append(u"3. 结构化写回块或可粘贴稿\n"_ustr);
        ap.append(u"4. Diff 预览 → 你批准写回（可撤销）\n"_ustr);
    }
    if (!p.reviseNotes.isEmpty())
    {
        ap.append(u"5. **已纳入你的修订：** "_ustr);
        ap.append(clip(p.reviseNotes, 120));
        ap.append(u"\n"_ustr);
    }
    p.approach = ap.makeStringAndClear();

    // Risks
    OUStringBuffer risks;
    risks.append(u"- 写回范围可能大于预期 — 务必看 Diff 再批准\n"_ustr);
    if (!rIn.hasSelection && surf == u"writer"_ustr)
        risks.append(u"- 未选区时模型可能覆盖面偏大；建议先选关键段落\n"_ustr);
    if (hasAny(low, { u"整篇"_ustr, u"全文"_ustr, u"全面"_ustr, u"重构"_ustr }))
        risks.append(u"- 「整篇」任务建议分批批准，避免一次大 diff\n"_ustr);
    if (surf == u"calc"_ustr)
        risks.append(u"- 公式写回前 dry-run 失败时会二次确认\n"_ustr);
    p.risks = risks.makeStringAndClear();

    // Acceptance
    OUStringBuffer acc;
    acc.append(u"- 输出含可检查的结构（要点/FIX/大纲/公式行）\n"_ustr);
    acc.append(u"- 主文档仅在「批准写回」后变化；可 ⌘Z/撤销写回\n"_ustr);
    if (!p.skillId.isEmpty())
    {
        acc.append(u"- 技能「"_ustr);
        acc.append(p.skillTitleZh.isEmpty() ? p.skillId : p.skillTitleZh);
        acc.append(u"」的输出约定被遵守\n"_ustr);
    }
    if (!p.reviseNotes.isEmpty())
        acc.append(u"- 修订意见已体现在草案中\n"_ustr);
    p.acceptance = acc.makeStringAndClear();

    p.markdown = formatMarkdown(p);
    return p;
}

OUString DocumentAIWorkPlan::applyContractToPrompt(const OUString& rWorkPrompt,
                                                   const WorkPlan& rPlan)
{
    OUStringBuffer b;
    b.append(u"【已批准工作计划 · 严格按此执行】\n"_ustr);
    b.append(u"计划ID："_ustr);
    b.append(rPlan.planId);
    b.append(u" v"_ustr);
    b.append(OUString::number(rPlan.version));
    b.append(u"\n目标："_ustr);
    b.append(rPlan.objective);
    b.append(u"\n做法：\n"_ustr);
    b.append(rPlan.approach);
    b.append(u"\n范围：做="_ustr);
    b.append(rPlan.scopeIn);
    b.append(u" · 不做="_ustr);
    b.append(rPlan.scopeOut);
    b.append(u"\n验收：\n"_ustr);
    b.append(rPlan.acceptance);
    if (!rPlan.reviseNotes.isEmpty())
    {
        b.append(u"\n用户修订意见："_ustr);
        b.append(rPlan.reviseNotes);
    }
    b.append(u"\n硬约束：只提议不自动写主文档；不编造事实；一次计划批准≠永久静默改稿。\n"_ustr);
    b.append(u"\n---\n"_ustr);
    b.append(rWorkPrompt);
    return b.makeStringAndClear();
}

OUString DocumentAIWorkPlan::chipLabelZh(const WorkPlan& rPlan, bool bApproved)
{
    OUStringBuffer b;
    if (bApproved)
        b.append(u"计划：已确认 · "_ustr);
    else
        b.append(u"计划：待确认 · "_ustr);
    b.append(clip(rPlan.objective, 24));
    OUString s = b.makeStringAndClear();
    if (s.getLength() > 42)
        s = s.copy(0, 42) + u"…"_ustr;
    return s;
}

std::vector<OUString> DocumentAIWorkPlan::approachStepTitles(const WorkPlan& rPlan)
{
    const auto steps
        = kqoffice::ai::control::WorkbenchPhaseMachine::parseStepsFromApproach(rPlan.approach);
    std::vector<OUString> out;
    out.reserve(steps.size());
    for (const auto& s : steps)
        out.push_back(s.titleZh);
    return out;
}

OUString DocumentAIWorkPlan::stepBarZh(const WorkPlan& rPlan, bool bApproved, sal_Int32 currentStep)
{
    const auto steps
        = kqoffice::ai::control::WorkbenchPhaseMachine::parseStepsFromApproach(rPlan.approach);
    using kqoffice::ai::control::WorkbenchPhase;
    using kqoffice::ai::control::WorkbenchPhaseMachine;
    const WorkbenchPhase phase
        = bApproved ? WorkbenchPhase::Generating : WorkbenchPhase::Planning;
    const auto snap = WorkbenchPhaseMachine::makeSnapshot(
        phase, rPlan.planId, OUString(), steps, currentStep, /*streamOpen*/ false,
        /*toolsOpen*/ false);
    return snap.stepBarZh;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
