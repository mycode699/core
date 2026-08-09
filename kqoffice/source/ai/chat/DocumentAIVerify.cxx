/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "DocumentAIVerify.hxx"

#include "AgentChatSelectionCapture.hxx"
#include "DocumentAIFormulaDryRun.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::chat
{
namespace
{
OUString surfaceOr(const OUString& s, const OUString& fallback)
{
    return s.isEmpty() ? fallback : s;
}

/// Soft honesty check: does expected newText appear near current selection/paragraph?
void spotCheckNewText(const ApplyPlan& rPlan, DocumentAIVerifyResult& out)
{
    const SelectionContext cap = AgentChatSelectionCapture::captureCurrent();
    OUStringBuffer hay;
    hay.append(cap.text);
    hay.append(u"\n"_ustr);
    hay.append(cap.paraText);
    hay.append(u"\n"_ustr);
    hay.append(cap.beforeText);
    hay.append(cap.afterText);
    const OUString h = hay.makeStringAndClear();
    if (h.getLength() < 2)
        return;

    sal_Int32 samples = 0;
    sal_Int32 hits = 0;
    for (const auto& op : rPlan.operations)
    {
        if (samples >= 6)
            break;
        // Prefer replace / insert with concrete new text; skip pure format meta.
        if (op.newText.isEmpty())
            continue;
        if (op.opType == u"format"_ustr && op.newText.startsWith(u"heading:"_ustr))
            continue;
        // Skip long blobs (full-doc rewrites) — selection haystack won't hold them.
        if (op.newText.getLength() < 2 || op.newText.getLength() > 120)
            continue;
        // Skip formulas that live in cells (checked by dry-run).
        if (op.newText.startsWith(u"="_ustr))
            continue;

        ++samples;
        // Use a short anchor (first 24 chars of first line) to reduce false misses.
        OUString needle = op.newText;
        const sal_Int32 nl = needle.indexOf(u'\n');
        if (nl > 0)
            needle = needle.copy(0, nl);
        if (needle.getLength() > 40)
            needle = needle.copy(0, 40);
        needle = needle.trim();
        if (needle.getLength() < 2)
            continue;
        if (h.indexOf(needle) >= 0)
            ++hits;
    }
    out.spotChecked = samples;
    out.spotHits = hits;
}
} // namespace

DocumentAIVerifyResult DocumentAIVerify::verifyPlanBeforeApply(const ApplyPlan& rPlan,
                                                               const OUString& rSurface)
{
    DocumentAIVerifyResult out;
    if (rPlan.operations.empty())
    {
        out.status = u"soft-fail"_ustr;
        out.summaryZh = u"校验失败 · 计划无操作项"_ustr;
        out.repairHintZh = u"请重新生成含可解析写回块的结果后再批准"_ustr;
        out.cardZh = formatPostApplyCard(out);
        return out;
    }
    if (!AgentChatDiffExtractor::validate(rPlan))
    {
        out.status = u"soft-fail"_ustr;
        out.summaryZh = u"校验失败 · 计划结构不完整"_ustr;
        out.repairHintZh = u"检查 target / newText 是否齐全后重试"_ustr;
        out.checkedOps = static_cast<sal_Int32>(rPlan.operations.size());
        out.cardZh = formatPostApplyCard(out);
        return out;
    }

    out.checkedOps = static_cast<sal_Int32>(rPlan.operations.size());
    const OUString surf = surfaceOr(rSurface, OUString());
    // Prefer selection snapshot so SUM(A1:A10) can eval against live numbers.
    CellValueSnapshot snap;
    if (surf == u"calc"_ustr || surf.isEmpty())
        snap = DocumentAIFormulaDryRun::captureSelectionSnapshot(256);
    const FormulaDryRunReport dry
        = DocumentAIFormulaDryRun::checkPlan(rPlan, snap.empty() ? nullptr : &snap);
    if (dry.hasWork())
    {
        out.failedOps = dry.badCount;
        if (dry.badCount > 0)
        {
            out.status = u"soft-fail"_ustr;
            out.summaryZh = u"写回前校验 · "_ustr + dry.summaryZh;
            out.repairHintZh
                = u"修正非法公式后重新运行任务，或编辑预览后再批准（主文档未改）"_ustr;
            out.ok = false;
            out.cardZh = formatPostApplyCard(out);
            return out;
        }
        out.status = u"soft-ok"_ustr;
        out.summaryZh = u"写回前校验通过 · "_ustr + dry.summaryZh;
        out.ok = true;
        out.cardZh = formatPostApplyCard(out);
        return out;
    }

    // Non-formula plans: structural OK is enough pre-apply.
    (void)surf;
    out.status = u"soft-ok"_ustr;
    out.summaryZh = u"写回前校验通过 · 操作="_ustr + OUString::number(out.checkedOps);
    out.ok = true;
    out.cardZh = formatPostApplyCard(out);
    return out;
}

DocumentAIVerifyResult DocumentAIVerify::verifyAfterApply(const ApplyPlan& rPlan,
                                                          const DocumentAIApplyResult& rApply,
                                                          const OUString& rSurface)
{
    DocumentAIVerifyResult out;
    out.checkedOps = static_cast<sal_Int32>(rPlan.operations.size());
    out.appliedCount = rApply.appliedCount;
    if (!rApply.success)
    {
        out.status = u"soft-fail"_ustr;
        out.summaryZh = u"写回后校验跳过 · 应用未成功"_ustr;
        out.repairHintZh = u"可再次点批准重试一次，或改指令后重跑"_ustr;
        out.cardZh = formatPostApplyCard(out);
        return out;
    }

    // Chart wizard: no cell mutation expected.
    if (rApply.engine == u"calc-chart-dispatch"_ustr || rPlan.planId == u"ap-chart-insert"_ustr)
    {
        out.status = u"soft-ok"_ustr;
        out.summaryZh = u"写回后校验 · 图表向导路径（无需单元格核对）"_ustr;
        out.ok = true;
        out.cardZh = formatPostApplyCard(out);
        return out;
    }

    if (out.checkedOps > 0 && rApply.appliedCount <= 0)
    {
        out.status = u"soft-fail"_ustr;
        out.failedOps = out.checkedOps;
        out.summaryZh = u"写回后校验失败 · applied=0 但计划有操作"_ustr;
        out.repairHintZh = u"请点「撤销写回」后改选区/目标再批准，或重新生成写回块"_ustr;
        out.cardZh = formatPostApplyCard(out);
        return out;
    }

    // Partial apply: honest soft-warn (Grok-style — don't claim full success).
    const bool bPartial
        = out.checkedOps > 1 && rApply.appliedCount > 0 && rApply.appliedCount < out.checkedOps;

    CellValueSnapshot snap;
    if (rSurface == u"calc"_ustr || rApply.surface == u"calc"_ustr)
        snap = DocumentAIFormulaDryRun::captureSelectionSnapshot(256);
    const FormulaDryRunReport dry
        = DocumentAIFormulaDryRun::checkPlan(rPlan, snap.empty() ? nullptr : &snap);
    if (dry.hasWork() && dry.badCount > 0)
    {
        out.status = u"soft-fail"_ustr;
        out.failedOps = dry.badCount;
        out.summaryZh = u"写回后校验 · 公式仍异常 · "_ustr + dry.summaryZh;
        out.repairHintZh = u"建议点「撤销写回」，修正公式后再次批准"_ustr;
        out.cardZh = formatPostApplyCard(out);
        return out;
    }

    // Spot-check sample newText near selection (Writer/Impress/selection-bound replaces).
    if (rApply.surface != u"calc"_ustr && rSurface != u"calc"_ustr)
        spotCheckNewText(rPlan, out);

    bool bSpotWarn = false;
    if (out.spotChecked >= 2 && out.spotHits == 0)
    {
        bSpotWarn = true;
        out.repairHintZh
            = u"选区附近未确认到预期新文；若写回范围在文档他处可忽略；否则请撤销后重试"_ustr;
    }
    else if (out.spotChecked >= 3 && out.spotHits * 2 < out.spotChecked)
    {
        bSpotWarn = true;
        if (out.repairHintZh.isEmpty())
            out.repairHintZh = u"部分预期文本未在选区附近确认；请目视 Diff/正文，必要时撤销"_ustr;
    }

    OUStringBuffer b;
    if (bPartial || bSpotWarn)
    {
        out.status = u"soft-warn"_ustr;
        out.ok = true; // applied, but honesty warn
        b.append(u"写回后校验 · 有提示 · 引擎="_ustr);
    }
    else
    {
        out.status = u"soft-ok"_ustr;
        out.ok = true;
        b.append(u"写回后校验通过 · 引擎="_ustr);
    }
    b.append(rApply.engine);
    b.append(u" · applied="_ustr);
    b.append(OUString::number(rApply.appliedCount));
    b.append(u"/"_ustr);
    b.append(OUString::number(out.checkedOps));
    if (bPartial)
        b.append(u" · 部分应用"_ustr);
    if (dry.hasWork())
    {
        b.append(u" · "_ustr);
        b.append(dry.summaryZh);
    }
    if (out.spotChecked > 0)
    {
        b.append(u" · 抽检="_ustr);
        b.append(OUString::number(out.spotHits));
        b.append(u"/"_ustr);
        b.append(OUString::number(out.spotChecked));
    }
    if (!rSurface.isEmpty())
    {
        b.append(u" · 表面="_ustr);
        b.append(rSurface);
    }
    out.summaryZh = b.makeStringAndClear();
    if (bPartial && out.repairHintZh.isEmpty())
        out.repairHintZh = u"部分操作未应用；可撤销后缩小范围再批准，或接受现状继续"_ustr;
    out.cardZh = formatPostApplyCard(out);
    return out;
}

OUString DocumentAIVerify::formatPostApplyCard(const DocumentAIVerifyResult& rResult)
{
    OUStringBuffer b;
    if (rResult.status == u"soft-fail"_ustr)
        b.append(u"### 写回后校验 · 未通过\n\n"_ustr);
    else if (rResult.status == u"soft-warn"_ustr)
        b.append(u"### 写回后校验 · 有提示\n\n"_ustr);
    else if (rResult.status == u"soft-ok"_ustr)
        b.append(u"### 写回后校验 · 通过\n\n"_ustr);
    else
        b.append(u"### 写回后校验\n\n"_ustr);

    b.append(u"**状态：** "_ustr);
    b.append(rResult.summaryZh.isEmpty() ? rResult.status : rResult.summaryZh);
    b.append(u"\n\n"_ustr);

    if (rResult.checkedOps > 0)
    {
        b.append(u"- 计划操作："_ustr);
        b.append(OUString::number(rResult.checkedOps));
        if (rResult.appliedCount > 0 || rResult.status != u"soft-fail"_ustr)
        {
            b.append(u" · 已应用："_ustr);
            b.append(OUString::number(rResult.appliedCount));
        }
        b.append(u"\n"_ustr);
    }
    if (rResult.spotChecked > 0)
    {
        b.append(u"- 选区附近抽检："_ustr);
        b.append(OUString::number(rResult.spotHits));
        b.append(u"/"_ustr);
        b.append(OUString::number(rResult.spotChecked));
        b.append(u" 命中\n"_ustr);
    }
    if (rResult.failedOps > 0)
    {
        b.append(u"- 问题项："_ustr);
        b.append(OUString::number(rResult.failedOps));
        b.append(u"\n"_ustr);
    }

    if (!rResult.repairHintZh.isEmpty())
    {
        b.append(u"\n**建议：** "_ustr);
        b.append(rResult.repairHintZh);
        b.append(u"\n"_ustr);
    }

    if (rResult.status == u"soft-fail"_ustr || rResult.status == u"soft-warn"_ustr)
    {
        b.append(u"\n> **一键回退：** 侧栏 **「撤销写回」**，或发送 `/撤销写回` / `/undo-apply`。\n"_ustr);
        b.append(u"> 一次写回批准 ≠ 永久静默改稿。\n"_ustr);
    }
    else if (rResult.status == u"soft-ok"_ustr)
    {
        b.append(u"\n> 写回已生效；不满意可点「撤销写回」或发送 `/撤销写回` 后改指令重跑。\n"_ustr);
    }
    return b.makeStringAndClear();
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
