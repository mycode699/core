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
