/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Document AI Fabric — approved write-back gate.
 * Writer prefers native ApplyEngine via C ABI hook exported from libsw
 * (dlsym RTLD_DEFAULT — avoids gbuild MERGELIBS link cycles).
 * Calc/Impress fall back to UNO DiffApplier. Never auto-applies.
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
    OUString engine; // "writer-apply-engine" | "uno-diff-applier" | "none"
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

class SAL_DLLPUBLIC_EXPORT DocumentAIApply
{
public:
    static void registerWriterApplyEngineHook(WriterApplyEngineHook pHook);
    static bool hasWriterApplyEngineHook();

    static OUString chatPlanToWriterRuntimeJson(const ApplyPlan& rPlan);

    static DocumentAIApplyResult applyApproved(const ApplyPlan& rPlan);
    static DocumentAIApplyResult applyApprovedWithRawFallback(const ApplyPlan& rPlan,
                                                              const OUString& rRawProviderContent);
};

} // namespace kqoffice::ai::chat

#endif


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
