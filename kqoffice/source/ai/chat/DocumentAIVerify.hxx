/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Post-apply / pre-apply soft verification for Agent Mode write-back.
 * Never mutates the main document by itself.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIVERIFY_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIVERIFY_HXX

#include <AgentChatDiffExtractor.hxx>
#include <DocumentAIApply.hxx>

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

struct DocumentAIVerifyResult
{
    bool ok = false;
    /// soft-ok | soft-warn | soft-fail | skip
    OUString status;
    OUString summaryZh;
    /// When soft-fail/warn, a short repair hint for user / second propose (no auto apply).
    OUString repairHintZh;
    sal_Int32 checkedOps = 0;
    sal_Int32 failedOps = 0;
    sal_Int32 appliedCount = 0;
    sal_Int32 spotChecked = 0; ///< ops whose newText was sampled in doc
    sal_Int32 spotHits = 0; ///< samples that found expected new text nearby
    /// User-facing markdown card (post-apply).
    OUString cardZh;
};

/// Soft verify helpers shared by chat apply path and Agent Mode.
class SAL_DLLPUBLIC_EXPORT DocumentAIVerify
{
public:
    /// Pre-apply: structural plan + formula dry-run (if any formulas).
    /// Does not touch the document.
    static DocumentAIVerifyResult verifyPlanBeforeApply(const ApplyPlan& rPlan,
                                                        const OUString& rSurface = OUString());

    /// Post-apply soft checks after DocumentAIApply success.
    /// Confirms appliedCount vs ops; partial apply → soft-warn; formula dry-run;
    /// samples replace newText against current selection/paragraph (Grok-style honesty).
    static DocumentAIVerifyResult verifyAfterApply(const ApplyPlan& rPlan,
                                                   const DocumentAIApplyResult& rApply,
                                                   const OUString& rSurface = OUString());

    /// Markdown card for chat UI (undo CTA when not soft-ok).
    static OUString formatPostApplyCard(const DocumentAIVerifyResult& rResult);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
