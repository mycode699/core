/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Exports kqoffice_impress_apply_runtime_json so Document AI Fabric
 * (kqoffice / sfx2 AIChat) can write back through Impress ApplyEngine
 * without linking libsd from sfx2 (MERGELIBS-safe C ABI / dlsym).
 *
 * W9-B / I1 skeleton: shape-text-replace (slide-insert → UNO fallback).
 */

#include <IntelligentImpressApplyEngine.hxx>
#include <DrawDocShell.hxx>

#include <algorithm>
#include <optional>
#include <sfx2/objsh.hxx>
#include <sal/log.hxx>
#include <sal/types.h>
#include <rtl/ustring.hxx>

extern "C" SAL_DLLPUBLIC_EXPORT sal_Bool kqoffice_impress_apply_runtime_json(
    const sal_Unicode* pJson, sal_Int32 nJsonLen, sal_Unicode* pErrorBuf, sal_Int32 nErrorCap,
    sal_Int32* pAppliedCount)
{
    if (pAppliedCount)
        *pAppliedCount = 0;

    auto setError = [&](const OUString& msg) {
        if (!pErrorBuf || nErrorCap <= 0)
            return;
        const sal_Int32 n = std::min(msg.getLength(), nErrorCap - 1);
        for (sal_Int32 i = 0; i < n; ++i)
            pErrorBuf[i] = msg[i];
        pErrorBuf[n] = 0;
    };

    if (!pJson || nJsonLen <= 0)
    {
        setError(u"empty-runtime-json"_ustr);
        return false;
    }

    const OUString aJson(pJson, nJsonLen);

    SfxObjectShell* pShell = SfxObjectShell::Current();
    auto* pSd = dynamic_cast<sd::DrawDocShell*>(pShell);
    if (!pSd)
    {
        setError(u"no-active-impress-docshell"_ustr);
        return false;
    }

    // Draw documents share DrawDocShell; I1 only targets Impress presentation docs.
    if (pSd->GetDocumentType() != DocumentType::Impress)
    {
        setError(u"no-active-impress-docshell"_ustr);
        return false;
    }

    std::optional<sd::intelligent::ApplyPlan> oPlan
        = sd::intelligent::TryParseImpressApplyPlanRuntimeJson(aJson);
    if (!oPlan)
    {
        setError(u"impress-runtime-json-parse-failed"_ustr);
        return false;
    }
    if (oPlan->mbPreviewOnly)
    {
        setError(u"preview-only-plan-blocked"_ustr);
        return false;
    }

    const sd::intelligent::ApplyResult aResult
        = sd::intelligent::ApplyImpressPlan(*pSd, *oPlan);
    if (pAppliedCount)
        *pAppliedCount = aResult.mnAppliedCount;

    if (aResult.meStatus == sd::intelligent::ApplyStatus::Ok && aResult.mnAppliedCount > 0)
    {
        SAL_INFO("sd.apply",
                 "kqoffice_impress_apply_runtime_json ok plan="
                     << oPlan->maPlanId << " applied=" << aResult.mnAppliedCount);
        return true;
    }

    // Prefer Chinese / detailed engine error when present.
    if (!aResult.maError.isEmpty())
    {
        setError(aResult.maError);
        return false;
    }

    OUString err = u"impress-apply-status="_ustr
                   + OUString::fromUtf8(sd::intelligent::ApplyStatusToken(aResult.meStatus));
    if (aResult.maFailedPatchId)
        err += u" failed-patch="_ustr + *aResult.maFailedPatchId;
    setError(err);
    return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
