/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "DocumentAITaskBootstrap.hxx"

#include "DocumentAIDocumentTools.hxx"

#include <rtl/ustrbuf.hxx>

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

OUString surfaceZh(const OUString& s)
{
    if (s == u"writer"_ustr)
        return u"文字"_ustr;
    if (s == u"calc"_ustr)
        return u"表格"_ustr;
    if (s == u"impress"_ustr)
        return u"演示"_ustr;
    if (s == u"notebook"_ustr)
        return u"笔记"_ustr;
    return u"办公"_ustr;
}

bool hasAny(const OUString& low, std::initializer_list<const char16_t*> keys)
{
    for (const auto* k : keys)
    {
        if (low.indexOf(OUString(k)) >= 0)
            return true;
    }
    return false;
}

OUString detectActionVerbZh(const OUString& prompt, const OUString& surface, bool hasSel)
{
    const OUString low = lower(prompt);
    if (hasAny(low, { u"公式", u"formula", u"sum(", u"汇总", u"aggregate" }))
        return u"生成/校验公式"_ustr;
    if (hasAny(low, { u"清洗", u"clean" }))
        return u"数据清洗"_ustr;
    if (hasAny(low, { u"表格美化", u"表头", u"冻结", u"table format", u"排版表格" }))
        return u"表格排版美化"_ustr;
    if (hasAny(low, { u"图表", u"chart" }))
        return u"图表建议"_ustr;
    if (hasAny(low, { u"排版", u"版式", u"层级", u"layout", u"typography" }))
        return u"排版/版式优化"_ustr;
    if (hasAny(low, { u"质检", u"质量", u"打分", u"quality" }))
        return u"内容质检"_ustr;
    if (hasAny(low, { u"pdf", u"PDF", u"扫描件" }))
        return u"PDF/材料处理"_ustr;
    if (hasAny(low, { u"大纲", u"outline", u"结构" }))
        return u"产出结构大纲"_ustr;
    if (hasAny(low, { u"审阅", u"校对", u"proofread", u"review" }))
        return u"审阅校对"_ustr;
    if (hasAny(low, { u"改写", u"润色", u"rewrite", u"polish", u"正式" }))
        return u"改写润色"_ustr;
    if (hasAny(low, { u"精简", u"缩短", u"shorten" }))
        return u"精简"_ustr;
    if (hasAny(low, { u"扩写", u"加长", u"expand" }))
        return u"扩写"_ustr;
    if (hasAny(low, { u"翻译", u"translate" }))
        return u"翻译"_ustr;
    if (hasAny(low, { u"续写", u"continue" }))
        return u"续写"_ustr;
    if (hasAny(low, { u"总结", u"概括", u"summar" }))
        return u"总结"_ustr;
    if (hasAny(low, { u"讲稿", u"备注", u"notes" }) && surface == u"impress"_ustr)
        return u"写讲稿"_ustr;
    if (hasAny(low, { u"多步", u"agent", u"协作", u"子代理", u"plan-act" }))
        return u"多步任务协作"_ustr;
    if (hasAny(low, { u"规划", u"计划", u"plan" }))
        return u"制定计划"_ustr;
    if (hasSel)
        return u"处理选区"_ustr;
    return u"协助处理"_ustr;
}
} // namespace

bool DocumentAITaskBootstrap::looksLikeContinue(const OUString& rPrompt)
{
    const OUString t = rPrompt.trim();
    if (t.isEmpty())
        return false;
    const OUString low = lower(t);
    if (low == u"继续"_ustr || low == u"继续。"_ustr || low == u"继续。。"_ustr
        || low == u"继续…"_ustr || low == u"continue"_ustr || low == u"go on"_ustr
        || low == u"jixu"_ustr || low == u"接着"_ustr || low == u"接着做"_ustr
        || low == u"往下"_ustr || low == u"下一步"_ustr)
        return true;
    if (t.getLength() <= 6
        && (low.startsWith(u"继续"_ustr) || low.startsWith(u"continue"_ustr)))
        return true;
    return false;
}

bool DocumentAITaskBootstrap::looksLikePureQa(const OUString& rPrompt)
{
    const OUString low = lower(rPrompt);
    // Explicit write verbs → not pure QA
    if (hasAny(low, { u"改写", u"润色", u"扩写", u"精简", u"翻译", u"替换", u"删掉",
                      u"删除", u"改成", u"写入", u"写回", u"生成公式", u"清洗",
                      u"rewrite", u"replace", u"delete", u"insert" }))
        return false;
    if (hasAny(low, { u"多步", u"agent", u"协作", u"子代理", u"plan-act" }))
        return false;
    // Question / explain style
    if (hasAny(low, { u"什么", u"为什么", u"怎么", u"如何", u"解释", u"说明", u"有哪些",
                      u"是否", u"?", u"？", u"what", u"why", u"how", u"explain",
                      u"tell me", u"是什么" }))
        return true;
    if (low.startsWith(u"/问"_ustr) || low.indexOf(u"问本文档"_ustr) >= 0)
        return true;
    return false;
}

bool DocumentAITaskBootstrap::looksLikeDevOrAgent(const OUString& rPrompt, bool bAgentCheckbox,
                                                  const OUString& rForcedCap)
{
    if (bAgentCheckbox)
        return true;
    const OUString cap = lower(rForcedCap);
    if (cap == u"agent"_ustr || cap == u"plan"_ustr)
        return true;
    const OUString low = lower(rPrompt);
    if (hasAny(low, { u"多步", u"子代理", u"协作", u"agent", u"cowork", u"plan-act",
                      u"/agent", u"分步", u"先规划" }))
        return true;
    if (hasAny(low, { u"全面升级", u"实现", u"开发", u"修复", u"落地", u"重构" }))
        return true;
    return false;
}

TaskBootstrapMode DocumentAITaskBootstrap::routeMode(const TaskBootstrapInput& rIn)
{
    const OUString p = rIn.userPrompt.trim();
    if (p.isEmpty())
        return TaskBootstrapMode::Skip;

    // Slash with full-ish contract: /rewrite already states action — thin card ok as skip
    if (p.startsWith(u"/"_ustr) && p.getLength() < 24
        && !looksLikeDevOrAgent(p, rIn.agentCheckbox, rIn.forcedCapability))
        return TaskBootstrapMode::Skip;

    if (looksLikeContinue(p) && (!rIn.lastPrompt.isEmpty() || !rIn.lastRestatement.isEmpty()))
        return TaskBootstrapMode::Thin;

    if (looksLikeDevOrAgent(p, rIn.agentCheckbox, rIn.forcedCapability))
        return TaskBootstrapMode::Required;

    // Edit-shaped freeform with selection → required short restatement
    const OUString intent
        = DocumentAIDocumentTools::classifyIntent(p, rIn.forcedCapability, rIn.hasSelection);
    if (intent == u"edit"_ustr)
        return TaskBootstrapMode::Required;

    if (looksLikePureQa(p) || intent == u"consult"_ustr || intent == u"read"_ustr)
        return TaskBootstrapMode::Skip; // still can thin-show optional — skip card noise

    if (rIn.hasSelection)
        return TaskBootstrapMode::Thin;

    return TaskBootstrapMode::Thin;
}

TaskRestatement DocumentAITaskBootstrap::buildRestatement(const TaskBootstrapInput& rIn,
                                                          TaskBootstrapMode eMode)
{
    TaskRestatement r;
    const OUString prompt = rIn.userPrompt.trim();
    const OUString surf = surfaceZh(rIn.surface);

    if (eMode == TaskBootstrapMode::Thin && looksLikeContinue(prompt))
    {
        const OUString anchor
            = !rIn.lastRestatement.isEmpty() ? rIn.lastRestatement : clip(rIn.lastPrompt, 48);
        r.objective = u"继续完成："_ustr
                      + (anchor.isEmpty() ? u"上一任务"_ustr : anchor);
        r.scopeIn = surf + u" · 承接上文"_ustr;
        r.scopeOut = u"不另起无关新任务"_ustr;
        r.nextStep = u"沿用上下文推进"_ustr;
    }
    else
    {
        const OUString verb = detectActionVerbZh(prompt, rIn.surface, rIn.hasSelection);
        r.objective = verb + u"：「"_ustr + clip(prompt, 56) + u"」"_ustr;

        OUStringBuffer scope;
        scope.append(surf);
        if (rIn.hasSelection)
        {
            scope.append(u" · 选区 "_ustr);
            scope.append(OUString::number(rIn.selectionChars));
            scope.append(u" 字"_ustr);
        }
        else
            scope.append(u" · 当前文档/上下文"_ustr);
        r.scopeIn = scope.makeStringAndClear();
        r.scopeOut = u"主文档未经你批准不写回"_ustr;

        if (rIn.surface == u"calc"_ustr
            && hasAny(lower(prompt), { u"公式", u"清洗", u"汇总", u"图表", u"多步" }))
            r.nextStep = u"探查表格 → 公式/清洗草案 → 沙箱校验"_ustr;
        else if (looksLikeDevOrAgent(prompt, rIn.agentCheckbox, rIn.forcedCapability))
            r.nextStep = u"绑定文档 → 规划 →（继续）执行"_ustr;
        else if (DocumentAIDocumentTools::classifyIntent(prompt, rIn.forcedCapability,
                                                         rIn.hasSelection)
                 == u"edit"_ustr)
            r.nextStep = u"生成预览 → 待批准写回"_ustr;
        else
            r.nextStep = u"检索/生成回答"_ustr;
    }

    r.source = (eMode == TaskBootstrapMode::Thin) ? u"thin"_ustr : u"rule"_ustr;
    r.confidence = confidenceScore(rIn, eMode);
    finalizeRestatementStrings(r);
    return r;
}

void DocumentAITaskBootstrap::finalizeRestatementStrings(TaskRestatement& rRest)
{
    OUStringBuffer vis;
    vis.append(u"理解："_ustr);
    vis.append(rRest.objective);
    vis.append(u" · 范围："_ustr);
    vis.append(rRest.scopeIn);
    if (!rRest.scopeOut.isEmpty())
    {
        vis.append(u" · 不做："_ustr);
        vis.append(rRest.scopeOut);
    }
    vis.append(u" · 下一步："_ustr);
    vis.append(rRest.nextStep);
    rRest.userVisibleZh = vis.makeStringAndClear();

    OUStringBuffer tok;
    tok.append(u"task_restatement: obj="_ustr);
    tok.append(clip(rRest.objective, 80));
    tok.append(u" | next="_ustr);
    tok.append(clip(rRest.nextStep, 40));
    tok.append(u" | src="_ustr);
    tok.append(rRest.source.isEmpty() ? u"rule"_ustr : rRest.source);
    tok.append(u" | conf="_ustr);
    tok.append(OUString::number(rRest.confidence));
    rRest.journalToken = tok.makeStringAndClear();
}

sal_Int32 DocumentAITaskBootstrap::confidenceScore(const TaskBootstrapInput& rIn,
                                                   TaskBootstrapMode eMode)
{
    if (eMode == TaskBootstrapMode::Skip)
        return 100;
    if (eMode == TaskBootstrapMode::Thin && looksLikeContinue(rIn.userPrompt)
        && (!rIn.lastPrompt.isEmpty() || !rIn.lastRestatement.isEmpty()))
        return 85;

    sal_Int32 conf = 40;
    const OUString p = rIn.userPrompt.trim();
    const OUString low = lower(p);

    // Clear action verb → higher confidence
    if (hasAny(low, { u"改写", u"润色", u"扩写", u"精简", u"翻译", u"公式", u"清洗",
                      u"大纲", u"审阅", u"续写", u"总结", u"rewrite", u"formula" }))
        conf += 25;
    if (rIn.hasSelection)
        conf += 15;
    if (!rIn.forcedCapability.isEmpty())
        conf += 15;
    if (p.getLength() >= 8 && p.getLength() <= 120)
        conf += 10;
    // Vague / very long freeform → lower
    if (p.getLength() > 200)
        conf -= 15;
    if (p.getLength() < 4)
        conf -= 20;
    if (hasAny(low, { u"弄一下", u"搞一下", u"处理下", u"帮我看看", u"随便", u"优化一下" }))
        conf -= 25;
    if (looksLikeDevOrAgent(p, rIn.agentCheckbox, rIn.forcedCapability) && p.getLength() > 40
        && !hasAny(low, { u"改写", u"公式", u"大纲" }))
        conf -= 10; // open-ended dev → prefer model refine

    if (conf < 0)
        conf = 0;
    if (conf > 100)
        conf = 100;
    return conf;
}

bool DocumentAITaskBootstrap::shouldRefineWithModel(const TaskBootstrapResult& rBoot,
                                                    sal_Int32 nConfidenceThreshold)
{
    if (rBoot.mode != TaskBootstrapMode::Required)
        return false;
    if (rBoot.skipRestatementCard)
        return false;
    return rBoot.restatement.confidence < nConfidenceThreshold;
}

OUString DocumentAITaskBootstrap::buildModelRefinePrompt(const TaskBootstrapInput& rIn)
{
    OUStringBuffer b;
    b.append(u"你是可圈办公任务启动器。只用中文输出 3 行，禁止散文、禁止工具：\n"_ustr);
    b.append(u"理解：<一句话核心交付>\n"_ustr);
    b.append(u"范围：<表面/选区/文档>\n"_ustr);
    b.append(u"下一步：<立刻要做的第一步>\n"_ustr);
    b.append(u"约束：主文档未经用户批准不写回。\n"_ustr);
    b.append(u"表面："_ustr);
    b.append(rIn.surface.isEmpty() ? u"none"_ustr : rIn.surface);
    if (rIn.hasSelection)
    {
        b.append(u" · 选区字数="_ustr);
        b.append(OUString::number(rIn.selectionChars));
    }
    b.append(u"\n用户原话：\n"_ustr);
    b.append(clip(rIn.userPrompt, 400));
    if (!rIn.lastRestatement.isEmpty())
    {
        b.append(u"\n上一目标："_ustr);
        b.append(clip(rIn.lastRestatement, 80));
    }
    return b.makeStringAndClear();
}

TaskRestatement DocumentAITaskBootstrap::parseModelRestatement(const OUString& rModelBody,
                                                               const TaskRestatement& rFallback)
{
    TaskRestatement r = rFallback;
    if (rModelBody.trim().isEmpty())
        return r;

    auto lineAfter = [&](const OUString& key) -> OUString {
        sal_Int32 p = rModelBody.indexOf(key);
        if (p < 0)
            return OUString();
        sal_Int32 start = p + key.getLength();
        sal_Int32 nl = rModelBody.indexOf(u'\n', start);
        if (nl < 0)
            nl = rModelBody.getLength();
        return rModelBody.copy(start, nl - start).trim();
    };

    const OUString o1 = lineAfter(u"理解："_ustr);
    const OUString o2 = lineAfter(u"理解:"_ustr);
    const OUString s1 = lineAfter(u"范围："_ustr);
    const OUString s2 = lineAfter(u"范围:"_ustr);
    const OUString n1 = lineAfter(u"下一步："_ustr);
    const OUString n2 = lineAfter(u"下一步:"_ustr);
    if (!o1.isEmpty() || !o2.isEmpty())
        r.objective = !o1.isEmpty() ? o1 : o2;
    if (!s1.isEmpty() || !s2.isEmpty())
        r.scopeIn = !s1.isEmpty() ? s1 : s2;
    if (!n1.isEmpty() || !n2.isEmpty())
        r.nextStep = !n1.isEmpty() ? n1 : n2;

    // Fallback: first non-empty line as objective if model ignored format
    if (r.objective == rFallback.objective || r.objective.isEmpty())
    {
        sal_Int32 pos = 0;
        while (pos < rModelBody.getLength())
        {
            sal_Int32 nl = rModelBody.indexOf(u'\n', pos);
            if (nl < 0)
                nl = rModelBody.getLength();
            OUString line = rModelBody.copy(pos, nl - pos).trim();
            pos = nl + 1;
            if (line.isEmpty() || line.startsWith(u"#"_ustr))
                continue;
            if (line.startsWith(u"理解"_ustr) || line.startsWith(u"范围"_ustr)
                || line.startsWith(u"下一步"_ustr))
                continue;
            r.objective = clip(line, 80);
            break;
        }
    }
    if (r.scopeOut.isEmpty())
        r.scopeOut = u"主文档未经你批准不写回"_ustr;
    r.source = u"model"_ustr;
    r.confidence = 88;
    finalizeRestatementStrings(r);
    return r;
}

TaskBootstrapResult DocumentAITaskBootstrap::refineWithModelOutput(
    const TaskBootstrapResult& rBoot, const TaskBootstrapInput& rIn, const OUString& rModelBody)
{
    TaskBootstrapResult out = rBoot;
    out.restatement = parseModelRestatement(rModelBody, rBoot.restatement);
    const CapabilitySchedule primary = buildSchedule(rIn, out.restatement, out.mode);
    out.schedule = refineScheduleSecondary(rIn, out.restatement, primary);
    out.wantsModelRefine = false;
    out.modelRefinePrompt.clear();
    // Prefer agent if model next-step implies multi-step
    const OUString ns = lower(out.restatement.nextStep);
    if (ns.indexOf(u"规划"_ustr) >= 0 || ns.indexOf(u"多步"_ustr) >= 0
        || ns.indexOf(u"plan"_ustr) >= 0 || ns.indexOf(u"探查"_ustr) >= 0)
        out.schedule.preferAgentPipeline = true;
    return out;
}

CapabilitySchedule DocumentAITaskBootstrap::buildSchedule(const TaskBootstrapInput& rIn,
                                                          const TaskRestatement& rRest,
                                                          TaskBootstrapMode eMode)
{
    CapabilitySchedule s;
    const OUString prompt = rIn.userPrompt.trim();
    const OUString low = lower(prompt);
    const OUString intent
        = DocumentAIDocumentTools::classifyIntent(prompt, rIn.forcedCapability, rIn.hasSelection);

    // Primary capability
    if (!rIn.forcedCapability.isEmpty())
        s.primaryCapability = rIn.forcedCapability;
    else if (looksLikeDevOrAgent(prompt, rIn.agentCheckbox, rIn.forcedCapability))
        s.primaryCapability = u"agent"_ustr;
    else if (hasAny(low, { u"规划", u"计划", u"/plan" })
             || (intent == u"consult"_ustr && hasAny(low, { u"大纲", u"outline" })))
        s.primaryCapability = hasAny(low, { u"大纲", u"outline" }) ? u"plan"_ustr : u"chat"_ustr;
    else if (intent == u"edit"_ustr)
    {
        if (hasAny(low, { u"精简", u"缩短" }))
            s.primaryCapability = u"shorten"_ustr;
        else if (hasAny(low, { u"扩写", u"加长" }))
            s.primaryCapability = u"expand"_ustr;
        else if (hasAny(low, { u"翻译" }))
            s.primaryCapability = u"translate"_ustr;
        else if (hasAny(low, { u"续写" }))
            s.primaryCapability = u"continue"_ustr;
        else
            s.primaryCapability = u"rewrite"_ustr;
    }
    else if (hasAny(low, { u"审阅", u"校对", u"review" }))
        s.primaryCapability = u"review"_ustr;
    else
        s.primaryCapability = u"chat"_ustr;

    s.preferAgentPipeline = rIn.agentCheckbox
                            || s.primaryCapability == u"agent"_ustr
                            || looksLikeDevOrAgent(prompt, rIn.agentCheckbox, rIn.forcedCapability)
                            || (rIn.surface == u"calc"_ustr
                                && hasAny(low, { u"多步", u"探查", u"清洗", u"汇总", u"图表" }));

    s.useDocumentTools = true;
    s.useFormulaSandbox = rIn.surface == u"calc"_ustr
                          || hasAny(low, { u"公式", u"formula", u"sum(", u"清洗" });
    s.useLocalRag = looksLikePureQa(prompt) || hasAny(low, { u"问本文档", u"/问", u"全文" });
    s.useMcpTools = s.preferAgentPipeline || s.useFormulaSandbox;
    s.stageApplyOnSuccess = intent == u"edit"_ustr || s.preferAgentPipeline
                            || s.primaryCapability == u"rewrite"_ustr
                            || s.primaryCapability == u"agent"_ustr;

    // Lanes (visible schedule)
    s.lanes.push_back(u"语义启动"_ustr);
    if (s.useDocumentTools)
        s.lanes.push_back(u"document-tools"_ustr);
    if (s.useLocalRag)
        s.lanes.push_back(u"local-rag"_ustr);
    if (s.preferAgentPipeline)
    {
        s.lanes.push_back(u"plan"_ustr);
        s.lanes.push_back(u"act"_ustr);
        s.lanes.push_back(u"verify"_ustr);
    }
    else
        s.lanes.push_back(u"model:"_ustr + s.primaryCapability);
    if (s.useFormulaSandbox)
        s.lanes.push_back(u"formula-sandbox"_ustr);
    if (s.useMcpTools)
        s.lanes.push_back(u"mcp-tools"_ustr);
    if (s.stageApplyOnSuccess)
        s.lanes.push_back(u"stage-apply"_ustr);

    // MCP tool hints (read/preview first; apply_approved only with human gate).
    s.mcpTools.clear();
    if (s.useDocumentTools)
    {
        s.mcpTools.push_back(u"read_skeleton"_ustr);
        s.mcpTools.push_back(u"snapshot_hash"_ustr);
    }
    if (s.useLocalRag || intent == u"read"_ustr)
        s.mcpTools.push_back(u"read_blocks"_ustr);
    if (s.useFormulaSandbox)
    {
        s.mcpTools.push_back(u"formula_dry_run"_ustr);
        s.mcpTools.push_back(u"verify_plan"_ustr);
    }
    if (s.stageApplyOnSuccess)
    {
        s.mcpTools.push_back(u"apply_preview"_ustr);
        // apply_approved listed last — still requires humanApproval
        s.mcpTools.push_back(u"apply_approved"_ustr);
    }
    if (s.useMcpTools && s.mcpTools.empty())
        s.mcpTools.push_back(u"list_tools"_ustr);

    OUStringBuffer sum;
    sum.append(u"调度 · 能力="_ustr);
    sum.append(s.primaryCapability);
    sum.append(u" · 多步="_ustr);
    sum.append(s.preferAgentPipeline ? u"是"_ustr : u"否"_ustr);
    sum.append(u" · 通道="_ustr);
    for (size_t i = 0; i < s.lanes.size(); ++i)
    {
        if (i)
            sum.append(u"→"_ustr);
        sum.append(s.lanes[i]);
    }
    if (!s.mcpTools.empty())
    {
        sum.append(u" · MCP="_ustr);
        for (size_t i = 0; i < s.mcpTools.size() && i < 5; ++i)
        {
            if (i)
                sum.append(u","_ustr);
            sum.append(s.mcpTools[i]);
        }
    }
    if (eMode == TaskBootstrapMode::Skip)
        sum.append(u" · bootstrap=skip"_ustr);
    else if (eMode == TaskBootstrapMode::Thin)
        sum.append(u" · bootstrap=thin"_ustr);
    else
        sum.append(u" · bootstrap=required"_ustr);
    if (s.secondaryRefined)
        sum.append(u" · refined"_ustr);
    (void)rRest;
    s.summaryZh = sum.makeStringAndClear();
    return s;
}

OUString DocumentAITaskBootstrap::buildContractBlock(const TaskRestatement& rRest,
                                                     const CapabilitySchedule& rSched)
{
    OUStringBuffer b;
    b.append(u"【任务合同 · 语义启动】\n"_ustr);
    if (!rRest.objective.isEmpty())
    {
        b.append(u"目标："_ustr);
        b.append(rRest.objective);
        b.append(u"\n"_ustr);
    }
    if (!rRest.scopeIn.isEmpty())
    {
        b.append(u"范围："_ustr);
        b.append(rRest.scopeIn);
        b.append(u"\n"_ustr);
    }
    if (!rRest.scopeOut.isEmpty())
    {
        b.append(u"不做："_ustr);
        b.append(rRest.scopeOut);
        b.append(u"\n"_ustr);
    }
    if (!rRest.nextStep.isEmpty())
    {
        b.append(u"下一步："_ustr);
        b.append(rRest.nextStep);
        b.append(u"\n"_ustr);
    }
    b.append(u"能力："_ustr);
    b.append(rSched.primaryCapability);
    b.append(u" · 多步="_ustr);
    b.append(rSched.preferAgentPipeline ? u"是"_ustr : u"否"_ustr);
    b.append(u"\n通道："_ustr);
    for (size_t i = 0; i < rSched.lanes.size(); ++i)
    {
        if (i)
            b.append(u" → "_ustr);
        b.append(rSched.lanes[i]);
    }
    b.append(u"\n"_ustr);
    if (!rSched.mcpTools.empty())
    {
        b.append(u"可用工具（只读/预览优先，写回须批准）："_ustr);
        for (size_t i = 0; i < rSched.mcpTools.size(); ++i)
        {
            if (i)
                b.append(u", "_ustr);
            b.append(rSched.mcpTools[i]);
        }
        b.append(u"\n"_ustr);
    }
    b.append(u"纪律：输出须服务上述目标；禁止声称已改主文档。\n"_ustr);
    b.append(u"【用户请求】\n"_ustr);
    return b.makeStringAndClear();
}

OUString DocumentAITaskBootstrap::applyContractToPrompt(const OUString& rWorkPrompt,
                                                        const TaskBootstrapResult& rBoot)
{
    // Skip mode: no heavy contract prefix (noise for pure QA).
    if (rBoot.mode == TaskBootstrapMode::Skip && rBoot.skipRestatementCard)
        return rWorkPrompt;
    if (rBoot.restatement.objective.isEmpty() && rBoot.schedule.lanes.empty())
        return rWorkPrompt;
    return buildContractBlock(rBoot.restatement, rBoot.schedule) + rWorkPrompt;
}

CapabilitySchedule DocumentAITaskBootstrap::refineScheduleSecondary(
    const TaskBootstrapInput& rIn, const TaskRestatement& rRest, const CapabilitySchedule& rPrimary)
{
    // Rebuild from restatement text (nextStep/objective) not only raw user keywords.
    TaskBootstrapInput fake = rIn;
    OUStringBuffer blended;
    blended.append(rIn.userPrompt);
    blended.append(u"\n"_ustr);
    blended.append(rRest.objective);
    blended.append(u"\n"_ustr);
    blended.append(rRest.nextStep);
    fake.userPrompt = blended.makeStringAndClear();

    CapabilitySchedule s = buildSchedule(fake, rRest, TaskBootstrapMode::Required);
    if (!rIn.forcedCapability.isEmpty())
        s.primaryCapability = rIn.forcedCapability;
    if (rPrimary.preferAgentPipeline)
        s.preferAgentPipeline = true;

    // Union MCP tools
    for (const auto& t : rPrimary.mcpTools)
    {
        bool found = false;
        for (const auto& x : s.mcpTools)
        {
            if (x == t)
            {
                found = true;
                break;
            }
        }
        if (!found)
            s.mcpTools.push_back(t);
    }

    // Prefer more lanes if secondary discovered new ones
    if (s.lanes.size() < rPrimary.lanes.size())
        s.lanes = rPrimary.lanes;

    s.secondaryRefined = true;
    if (s.summaryZh.indexOf(u"refined"_ustr) < 0)
        s.summaryZh += u" · refined"_ustr;
    return s;
}

TaskBootstrapResult DocumentAITaskBootstrap::bootstrap(const TaskBootstrapInput& rIn)
{
    TaskBootstrapResult out;
    out.normalizedPrompt = rIn.userPrompt.trim();
    out.mode = routeMode(rIn);

    if (out.mode == TaskBootstrapMode::Skip)
    {
        out.skipRestatementCard = true;
        // Still build a minimal restatement for schedule (not shown)
        out.restatement.objective = clip(out.normalizedPrompt, 64);
        out.restatement.userVisibleZh.clear();
        out.restatement.source = u"skip"_ustr;
        out.restatement.confidence = 100;
        out.restatement.journalToken = u"task_restatement:skip"_ustr;
    }
    else
    {
        out.restatement = buildRestatement(rIn, out.mode);
        out.skipRestatementCard = false;
    }

    {
        const CapabilitySchedule primary = buildSchedule(rIn, out.restatement, out.mode);
        // Secondary pass even on rule path: blend objective into schedule keywords.
        out.schedule = (out.mode == TaskBootstrapMode::Skip)
                           ? primary
                           : refineScheduleSecondary(rIn, out.restatement, primary);
    }
    out.wantsModelRefine = shouldRefineWithModel(out);
    if (out.wantsModelRefine)
        out.modelRefinePrompt = buildModelRefinePrompt(rIn);

    // Continue: expand prompt with last goal for model
    if (looksLikeContinue(out.normalizedPrompt)
        && (!rIn.lastPrompt.isEmpty() || !rIn.lastRestatement.isEmpty()))
    {
        OUStringBuffer p;
        p.append(u"【继续上一任务】"_ustr);
        if (!rIn.lastRestatement.isEmpty())
        {
            p.append(u"目标："_ustr);
            p.append(rIn.lastRestatement);
            p.append(u"\n"_ustr);
        }
        if (!rIn.lastPrompt.isEmpty())
        {
            p.append(u"上一轮用户："_ustr);
            p.append(clip(rIn.lastPrompt, 200));
            p.append(u"\n"_ustr);
        }
        p.append(u"请承接上文继续推进，不要另起无关任务。"_ustr);
        out.normalizedPrompt = p.makeStringAndClear();
    }

    // Inject task contract so Provider/Agent stay aligned with restatement.
    out.normalizedPrompt = applyContractToPrompt(out.normalizedPrompt, out);

    return out;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
