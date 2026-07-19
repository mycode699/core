/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include <DocumentAIApply.hxx>
#include <AgentChatDiffApplier.hxx>
#include <AgentChatSelectionCapture.hxx>

#include <comphelper/dispatchcommand.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/uno/Sequence.hxx>

#include <algorithm>

#if defined(MACOSX) || defined(LINUX) || defined(FREEBSD) || defined(NETBSD) || defined(OPENBSD) \
    || defined(DRAGONFLY) || defined(ANDROID) || defined(EMSCRIPTEN)
#include <dlfcn.h>
#define KQOFFICE_HAVE_DLSYM 1
#endif

namespace kqoffice::ai::chat
{
namespace
{
WriterApplyEngineHook g_writerHook = nullptr;

void appendJsonEscaped(OUStringBuffer& b, const OUString& s)
{
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        switch (c)
        {
            case u'"':
                b.append(u"\\\"");
                break;
            case u'\\':
                b.append(u"\\\\");
                break;
            case u'\n':
                b.append(u"\\n");
                break;
            case u'\r':
                b.append(u"\\r");
                break;
            case u'\t':
                b.append(u"\\t");
                break;
            default:
                b.append(c);
                break;
        }
    }
}

OUString toSwParagraphId(const OUString& rTarget)
{
    if (rTarget.startsWith(u"swpara-"_ustr))
        return rTarget;
    if (rTarget.startsWith(u"para:"_ustr))
    {
        const OUString n = rTarget.copy(5).trim();
        if (!n.isEmpty())
            return u"swpara-"_ustr + n;
    }
    bool digits = !rTarget.isEmpty();
    for (sal_Int32 i = 0; i < rTarget.getLength() && digits; ++i)
        digits = rTarget[i] >= u'0' && rTarget[i] <= u'9';
    if (digits)
        return u"swpara-"_ustr + rTarget;
    return OUString();
}

OUString patchKindForOp(const DiffOperation& op)
{
    if (op.opType == u"insert"_ustr)
        return u"paragraph-insert-after"_ustr;
    if (op.opType == u"delete"_ustr)
        return u"paragraph-delete"_ustr;
    if (op.opType == u"format"_ustr)
        return u"paragraph-format"_ustr;
    return u"paragraph-replace"_ustr;
}

bool looksLikeWriterRuntimeJson(const OUString& s)
{
    return s.indexOf(u"v2-w3-runtime-1"_ustr) >= 0
           && (s.indexOf(u"\"patches\""_ustr) >= 0 || s.indexOf(u"paragraph-replace"_ustr) >= 0);
}

using CWriterApplyFn = sal_Bool (*)(const sal_Unicode*, sal_Int32, sal_Unicode*, sal_Int32,
                                    sal_Int32*);

bool tryWriterApplyViaDlsym(const OUString& rRuntimeJson, OUString& rErrorOut,
                            sal_Int32& rAppliedCount)
{
#if KQOFFICE_HAVE_DLSYM
    // libsw exports this when Writer has been loaded in-process.
    void* pSym = dlsym(RTLD_DEFAULT, "kqoffice_writer_apply_runtime_json");
    if (!pSym)
    {
        rErrorOut = u"writer-apply-symbol-not-loaded"_ustr;
        return false;
    }
    auto pFn = reinterpret_cast<CWriterApplyFn>(pSym);
    sal_Unicode aErr[512] = {};
    sal_Int32 nApplied = 0;
    const sal_Bool ok
        = pFn(rRuntimeJson.getStr(), rRuntimeJson.getLength(), aErr, 511, &nApplied);
    rAppliedCount = nApplied;
    if (!ok)
    {
        rErrorOut = OUString(aErr);
        if (rErrorOut.isEmpty())
            rErrorOut = u"writer-apply-failed"_ustr;
        return false;
    }
    return true;
#else
    (void)rRuntimeJson;
    rErrorOut = u"writer-apply-dlsym-unavailable"_ustr;
    rAppliedCount = 0;
    return false;
#endif
}

bool callWriterApply(const OUString& rRuntimeJson, OUString& rErrorOut, sal_Int32& rAppliedCount)
{
    if (g_writerHook)
        return g_writerHook(rRuntimeJson, rErrorOut, rAppliedCount);
    return tryWriterApplyViaDlsym(rRuntimeJson, rErrorOut, rAppliedCount);
}
} // namespace

void DocumentAIApply::registerWriterApplyEngineHook(WriterApplyEngineHook pHook)
{
    g_writerHook = pHook;
}

bool DocumentAIApply::hasWriterApplyEngineHook()
{
    if (g_writerHook)
        return true;
#if KQOFFICE_HAVE_DLSYM
    return dlsym(RTLD_DEFAULT, "kqoffice_writer_apply_runtime_json") != nullptr;
#else
    return false;
#endif
}

OUString DocumentAIApply::chatPlanToWriterRuntimeJson(const ApplyPlan& rPlan)
{
    if (rPlan.operations.empty())
        return OUString();

    const OUString planId
        = rPlan.planId.isEmpty() ? u"ap-chat-apply"_ustr : rPlan.planId;

    OUStringBuffer b;
    b.append(u"{\n");
    b.append(u"  \"schema_version\": \"v2-w3-runtime-1\",\n");
    b.append(u"  \"plan_id\": \"");
    appendJsonEscaped(b, planId);
    b.append(u"\",\n");
    b.append(u"  \"source_diagnostic_id\": \"diag-chat-");
    appendJsonEscaped(b, planId);
    b.append(u"\",\n");
    b.append(
        u"  \"doc_snapshot_hash\": "
        u"\"sha256:0000000000000000000000000000000000000000000000000000000000000000\",\n");
    b.append(u"  \"preview_only\": false,\n");
    b.append(u"  \"patches\": [\n");

    sal_Int32 nPatch = 0;
    for (sal_Int32 i = 0; i < static_cast<sal_Int32>(rPlan.operations.size()); ++i)
    {
        const DiffOperation& op = rPlan.operations[static_cast<size_t>(i)];
        OUString paraId = toSwParagraphId(op.target);
        if (paraId.isEmpty())
            paraId = u"swpara-1"_ustr;

        const OUString kind = patchKindForOp(op);
        if (kind != u"paragraph-replace"_ustr && kind != u"paragraph-delete"_ustr
            && kind != u"paragraph-insert-after"_ustr && kind != u"paragraph-format"_ustr)
            continue;

        if (nPatch > 0)
            b.append(u",\n");

        b.append(u"    {\n");
        b.append(u"      \"patch_id\": \"p");
        b.append(nPatch + 1);
        b.append(u"\",\n");
        b.append(u"      \"kind\": \"");
        b.append(kind);
        b.append(u"\",\n");
        b.append(u"      \"target\": {\"paragraph_id\": \"");
        appendJsonEscaped(b, paraId);
        b.append(u"\"},\n");
        b.append(u"      \"severity\": \"minor\",\n");
        b.append(u"      \"rationale\": \"chat approved apply\",\n");

        if (kind == u"paragraph-replace"_ustr || kind == u"paragraph-insert-after"_ustr)
        {
            if (!op.oldText.isEmpty())
            {
                b.append(u"      \"before\": \"");
                appendJsonEscaped(b, op.oldText);
                b.append(u"\",\n");
            }
            b.append(u"      \"after\": \"");
            appendJsonEscaped(b, op.newText);
            b.append(u"\"\n");
        }
        else if (kind == u"paragraph-delete"_ustr)
        {
            b.append(u"      \"force\": true\n");
        }
        else
        {
            b.append(u"      \"format_changes\": {}\n");
        }

        b.append(u"    }");
        ++nPatch;
    }

    if (nPatch == 0)
        return OUString();

    b.append(u"\n  ]\n}\n");
    return b.makeStringAndClear();
}

OUString DocumentAIApply::userFacingEngineZh(const OUString& rEngine)
{
    if (rEngine == u"writer-apply-engine"_ustr)
        return u"Writer 原生写回"_ustr;
    if (rEngine == u"uno-diff-applier"_ustr)
        return u"UNO 轻量写回"_ustr;
    if (rEngine == u"calc-chart-dispatch"_ustr)
        return u"图表向导"_ustr;
    if (rEngine == u"none"_ustr || rEngine.isEmpty())
        return u"无写回引擎"_ustr;
    return rEngine;
}

OUString DocumentAIApply::userFacingSurfaceZh(const OUString& rSurface)
{
    if (rSurface == u"writer"_ustr)
        return u"文字"_ustr;
    if (rSurface == u"calc"_ustr)
        return u"表格"_ustr;
    if (rSurface == u"impress"_ustr)
        return u"演示"_ustr;
    if (rSurface == u"none"_ustr || rSurface.isEmpty() || rSurface == u"unknown"_ustr)
        return u"无文档"_ustr;
    return rSurface;
}

OUString DocumentAIApply::userFacingErrorZh(const OUString& rError, const OUString& rEngine,
                                            const OUString& rSurface)
{
    // Already Chinese — pass through (keep machine tokens only when pure ASCII/kebab).
    for (sal_Int32 i = 0; i < rError.getLength(); ++i)
    {
        const sal_Unicode c = rError[i];
        if (c >= 0x4E00 && c <= 0x9FFF)
            return rError;
    }

    const OUString s = rError;
    if (s.isEmpty())
    {
        if (rSurface == u"calc"_ustr || rSurface == u"impress"_ustr)
            return u"写回失败 · 当前应用仅支持 UNO 轻量写回（无原生 ApplyEngine）"_ustr;
        if (rEngine == u"uno-diff-applier"_ustr)
            return u"UNO 写回失败 · 主文档未改"_ustr;
        return u"写回失败 · 主文档未改"_ustr;
    }

    if (s.indexOf(u"writer-apply-symbol-not-loaded"_ustr) >= 0)
        return u"Writer 写回引擎未加载 · 将尝试 UNO 回退"_ustr;
    if (s.indexOf(u"writer-apply-dlsym-unavailable"_ustr) >= 0)
        return u"当前平台无法加载 Writer 写回引擎"_ustr;
    if (s.indexOf(u"writer-apply-failed"_ustr) >= 0
        || s.indexOf(u"writer-apply-status="_ustr) >= 0)
        return u"Writer 原生写回失败 · 已尝试或将尝试其他路径"_ustr;
    if (s.indexOf(u"no-active-writer-docshell"_ustr) >= 0)
        return u"当前不是 Writer 文档 · 无法使用原生写回引擎"_ustr;
    if (s.indexOf(u"writer-runtime-json-parse-failed"_ustr) >= 0
        || s.indexOf(u"empty-runtime-json"_ustr) >= 0)
        return u"写回计划解析失败"_ustr;
    if (s.indexOf(u"preview-only-plan-blocked"_ustr) >= 0)
        return u"预览计划不可写回 · 请生成可批准的正式计划"_ustr;
    if (s.indexOf(u"InsertObjectChart"_ustr) >= 0 || s.indexOf(u"chart-insert"_ustr) >= 0)
        return u"打开图表向导失败 · 请确认当前为表格选区"_ustr;
    if (s.indexOf(u"No current document"_ustr) >= 0
        || s.indexOf(u"getCurrentComponent returned null"_ustr) >= 0)
        return u"没有活动文档 · 请先打开文字/表格/演示"_ustr;
    if (s.indexOf(u"Unrecognized document type"_ustr) >= 0
        || s.indexOf(u"Unsupported document type"_ustr) >= 0)
        return u"当前文档类型不支持 AI 写回"_ustr;
    if (s.indexOf(u"Unsupported operation"_ustr) >= 0)
        return u"当前应用不支持该写回操作类型"_ustr;
    if (s.indexOf(u"Empty opType"_ustr) >= 0 || s.indexOf(u"Empty target"_ustr) >= 0)
        return u"写回计划不完整（缺少操作或目标）"_ustr;
    if (s.indexOf(u"expected cell:"_ustr) >= 0 || s.indexOf(u"Calc "_ustr) >= 0)
        return u"表格写回失败 · 目标须为单元格（如 cell:A1）；暂无原生 Calc ApplyEngine"_ustr;
    if (s.indexOf(u"expected slide:"_ustr) >= 0 || s.indexOf(u"Impress "_ustr) >= 0)
        return u"演示写回失败 · 目标须为幻灯/形状；暂无原生 Impress ApplyEngine"_ustr;
    if (s.indexOf(u"Writer "_ustr) >= 0 || s.indexOf(u"para:"_ustr) >= 0)
        return u"文字写回失败 · 请检查段落目标后重试"_ustr;
    if (s.indexOf(u"No operation to undo"_ustr) >= 0)
        return u"没有可撤销的 AI 写回"_ustr;

    // Keep short English tokens readable for diagnostics without flooding the status bar.
    if (s.getLength() <= 48)
        return u"写回失败（"_ustr + s + u"）"_ustr;
    return u"写回失败 · 详见系统记录"_ustr;
}

DocumentAIApplyResult DocumentAIApply::applyApproved(const ApplyPlan& rPlan)
{
    return applyApprovedWithRawFallback(rPlan, rPlan.rawOutput);
}

DocumentAIApplyResult DocumentAIApply::applyApprovedWithRawFallback(
    const ApplyPlan& rPlan, const OUString& rRawProviderContent)
{
    DocumentAIApplyResult out;
    out.planId = rPlan.planId;

    const SelectionContext sel = AgentChatSelectionCapture::captureCurrent();
    out.surface = sel.surface;

    // Chart insert: open Calc chart wizard on current selection (explicit approval only).
    const bool bChartPlan = rPlan.planId == u"ap-chart-insert"_ustr
                            || (!rPlan.operations.empty()
                                && rPlan.operations.front().opType == u"chart_insert"_ustr);
    if (bChartPlan)
    {
        out.surface = sel.surface.isEmpty() ? u"calc"_ustr : sel.surface;
        try
        {
            const bool ok = comphelper::dispatchCommand(
                u".uno:InsertObjectChart"_ustr,
                css::uno::Sequence<css::beans::PropertyValue>{});
            if (ok)
            {
                out.success = true;
                out.engine = u"calc-chart-dispatch"_ustr;
                out.appliedCount = 1;
                out.evidenceNote = u"chart-insert-dispatched plan="_ustr + out.planId
                                   + u" target="_ustr
                                   + (rPlan.operations.empty() ? u"selection"_ustr
                                                               : rPlan.operations.front().target)
                                   + u" explicit-human-approval=true"_ustr;
                SAL_INFO("kqoffice.ai.chat", out.evidenceNote);
                return out;
            }
            out.error = u"InsertObjectChart dispatch returned false"_ustr;
        }
        catch (const css::uno::Exception& e)
        {
            out.error = u"chart-insert-failed: "_ustr + e.Message;
        }
        out.engine = u"calc-chart-dispatch"_ustr;
        out.evidenceNote = u"chart-insert-failed plan="_ustr + out.planId + u" error="_ustr
                           + out.error;
        return out;
    }

    if (out.surface == u"writer"_ustr || out.surface.isEmpty() || out.surface == u"unknown"_ustr)
    {
        OUString runtimeJson;
        if (looksLikeWriterRuntimeJson(rRawProviderContent))
            runtimeJson = rRawProviderContent;
        else if (looksLikeWriterRuntimeJson(rPlan.rawOutput))
            runtimeJson = rPlan.rawOutput;
        else
            runtimeJson = chatPlanToWriterRuntimeJson(rPlan);

        if (!runtimeJson.isEmpty())
        {
            OUString err;
            sal_Int32 applied = 0;
            if (callWriterApply(runtimeJson, err, applied))
            {
                out.success = true;
                out.engine = u"writer-apply-engine"_ustr;
                out.appliedCount = applied;
                out.evidenceNote = u"writer-apply-engine plan="_ustr + out.planId
                                   + u" applied="_ustr + OUString::number(applied);
                SAL_INFO("kqoffice.ai.chat", out.evidenceNote);
                return out;
            }
            SAL_WARN("kqoffice.ai.chat",
                     "DocumentAIApply: writer engine failed: " << err
                     << " — falling back to UNO DiffApplier");
            out.error = err;
        }
    }

    const ApplyResult ar = AgentChatDiffApplier::apply(rPlan);
    out.success = ar.success;
    out.engine = u"uno-diff-applier"_ustr;
    out.appliedCount = static_cast<sal_Int32>(ar.appliedOps.size());
    if (ar.success)
    {
        out.error.clear();
        out.evidenceNote = u"uno-diff-applier plan="_ustr + out.planId + u" applied="_ustr
                           + OUString::number(out.appliedCount) + u" surface="_ustr
                           + out.surface;
    }
    else
    {
        if (out.error.isEmpty())
            out.error = ar.error;
        else if (!ar.error.isEmpty())
            out.error = out.error + u" | uno="_ustr + ar.error;
        out.evidenceNote = u"apply-failed plan="_ustr + out.planId + u" error="_ustr + out.error;
    }
    return out;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
