/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Document AI Fabric — approved write-back gate.
 * Writer prefers native ApplyEngine via C ABI hook exported from libsw
 * (dlsym RTLD_DEFAULT — avoids gbuild MERGELIBS link cycles).
 * Calc prefers native skeleton via kqoffice_calc_apply_runtime_json (libsc)
 * for cell-replace / cell-formula; falls back to UNO DiffApplier.
 * Impress still UNO-only. Never auto-applies.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIAPPLY_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIAPPLY_HXX

#include <AgentChatDiffExtractor.hxx>

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

struct DocumentAIApplyResult
{
    bool success = false;
    // "writer-apply-engine" | "calc-apply-engine" | "uno-diff-applier" |
    // "calc-chart-dispatch" | "none"
    OUString engine;
    OUString surface;
    OUString planId;
    sal_Int32 appliedCount = 0;
    OUString error;
    OUString evidenceNote;
};

/// Optional test/override hook. Production uses dlsym of
/// kqoffice_writer_apply_runtime_json from loaded libsw.
using WriterApplyEngineHook
    = bool (*)(const OUString& rRuntimeJson, OUString& rErrorOut, sal_Int32& rAppliedCount);

/// Optional test/override hook. Production uses dlsym of
/// kqoffice_calc_apply_runtime_json from loaded libsc.
using CalcApplyEngineHook
    = bool (*)(const OUString& rRuntimeJson, OUString& rErrorOut, sal_Int32& rAppliedCount);

class SAL_DLLPUBLIC_EXPORT DocumentAIApply
{
public:
    static void registerWriterApplyEngineHook(WriterApplyEngineHook pHook);
    static bool hasWriterApplyEngineHook();

    static void registerCalcApplyEngineHook(CalcApplyEngineHook pHook);
    static bool hasCalcApplyEngineHook();

    static OUString chatPlanToWriterRuntimeJson(const ApplyPlan& rPlan);
    /// C1 schema: v1-calc-runtime-1 patches cell-replace / cell-formula.
    static OUString chatPlanToCalcRuntimeJson(const ApplyPlan& rPlan);

    static DocumentAIApplyResult applyApproved(const ApplyPlan& rPlan);
    static DocumentAIApplyResult applyApprovedWithRawFallback(const ApplyPlan& rPlan,
                                                              const OUString& rRawProviderContent);

    /// User-facing Chinese labels for engine / surface / apply errors (honest parity copy).
    static OUString userFacingEngineZh(const OUString& rEngine);
    static OUString userFacingSurfaceZh(const OUString& rSurface);
    static OUString userFacingErrorZh(const OUString& rError, const OUString& rEngine = OUString(),
                                      const OUString& rSurface = OUString());
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
