/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4 Day-3: Writer popover → Provider).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "InlineActionProviderDispatch.hxx"

#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/processfactory.hxx>
#include <docsh.hxx>
#include <IntelligentWriterApplyEngine.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <sfx2/objsh.hxx>
#include <svx/sidebar/DiffReviewPanel.hxx>

namespace sw::inline_actions {
namespace {

constexpr sal_Int32 kContentPreviewMaxLen = 80;

// W4 surface tokens (inline-action-request.schema.json) vs W1 offline allowlist
// (ServiceModePolicy.cxx kOfflineCapabilities). W4 spec table lists ideal W1 names
// (expand, shorten, translate-en) that are not yet in the offline allowlist; this
// adapter maps them onto permitted capabilities without widening the policy:
//   rewrite        → rewrite
//   expand         → rewrite      (length expansion via rewrite prompt)
//   shorten        → summarize    (condense via summarize)
//   translate-en   → rewrite      (translation framed as rewrite instruction)
//   format-clean   → format-fix   (W4 local token → W1 format-fix)
//   custom         → intent-to-uno (free-form user intent)
//   explain        → (none)       popup-only, no provider dispatch

std::optional<OUString> lcl_extractJsonStringField(const OUString& rJson,
                                                   std::u16string_view rKey)
{
    OUStringBuffer aNeedle(16);
    aNeedle.append('"');
    aNeedle.append(rKey.data(), rKey.size());
    aNeedle.append("\":\"");
    const OUString sNeedle = aNeedle.makeStringAndClear();
    const sal_Int32 nStart = rJson.indexOf(sNeedle);
    if (nStart < 0)
        return std::nullopt;

    sal_Int32 i = nStart + sNeedle.getLength();
    OUStringBuffer aValue;
    for (; i < rJson.getLength(); ++i)
    {
        const sal_Unicode c = rJson[i];
        if (c == '\\' && i + 1 < rJson.getLength())
        {
            const sal_Unicode e = rJson[++i];
            switch (e)
            {
                case '"':
                    aValue.append('"');
                    break;
                case '\\':
                    aValue.append('\\');
                    break;
                case 'n':
                    aValue.append('\n');
                    break;
                case 'r':
                    aValue.append('\r');
                    break;
                case 't':
                    aValue.append('\t');
                    break;
                default:
                    aValue.append(e);
                    break;
            }
            continue;
        }
        if (c == '"')
            return aValue.makeStringAndClear();
        aValue.append(c);
    }
    return std::nullopt;
}

OUString lcl_defaultPromptForAction(const OUString& rActionToken)
{
    if (rActionToken == u"rewrite")
        return u"Rewrite the selected paragraph."_ustr;
    if (rActionToken == u"expand")
        return u"Expand the selected paragraph with more detail."_ustr;
    if (rActionToken == u"shorten")
        return u"Shorten the selected paragraph while keeping the meaning."_ustr;
    if (rActionToken == u"translate-en")
        return u"Translate the selected paragraph to English."_ustr;
    if (rActionToken == u"format-clean")
        return u"Clean up formatting in the selected paragraph."_ustr;
    return OUString();
}

OUString lcl_stableInlineIdSuffix(const OUString& rRequestId)
{
    OUStringBuffer aOut;
    for (sal_Int32 i = 0; i < rRequestId.getLength(); ++i)
    {
        const sal_Unicode c = rRequestId[i];
        if ((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-')
            aOut.append(c);
        else if (c >= u'A' && c <= u'Z')
            aOut.append(static_cast<sal_Unicode>(c - u'A' + u'a'));
    }
    OUString s = aOut.makeStringAndClear();
    if (s.getLength() < 3)
        return u"request-001"_ustr;
    if (s.getLength() > 64)
        return s.copy(0, 64);
    return s;
}

OUString lcl_truncatePreview(const OUString& rContent)
{
    if (rContent.getLength() <= kContentPreviewMaxLen)
        return rContent;
    return rContent.copy(0, kContentPreviewMaxLen) + u"…"_ustr;
}

OUString lcl_statusWithPreview(const OUString& rStatus, const OUString& rContent)
{
    if (rContent.isEmpty())
        return rStatus;
    return rStatus + u" — "_ustr + lcl_truncatePreview(rContent);
}

} // namespace

std::optional<OUString> mapWriterActionToProviderCapability(const OUString& rActionToken)
{
    if (rActionToken == u"rewrite")
        return u"rewrite"_ustr;
    if (rActionToken == u"expand")
        return u"rewrite"_ustr;
    if (rActionToken == u"shorten")
        return u"summarize"_ustr;
    if (rActionToken == u"translate-en")
        return u"rewrite"_ustr;
    if (rActionToken == u"format-clean")
        return u"format-fix"_ustr;
    if (rActionToken == u"custom")
        return u"intent-to-uno"_ustr;
    return std::nullopt;
}

OUString buildWriterRuntimeJsonPromptForProvider(const OUString& rActionToken,
                                                 const OUString& rUserPrompt,
                                                 const OUString& rParagraphId,
                                                 const OUString& rRequestId)
{
    const OUString sInstruction
        = rUserPrompt.isEmpty() ? lcl_defaultPromptForAction(rActionToken) : rUserPrompt;
    const OUString sParagraphId = rParagraphId.isEmpty() ? u"swpara-1"_ustr : rParagraphId;
    const OUString sIdSuffix = lcl_stableInlineIdSuffix(rRequestId);

    OUStringBuffer aPrompt(2200);
    aPrompt.append(
        "Return exactly one JSON object. No markdown. No explanation. The JSON object must be "
        "valid KQOffice W3 apply-plan-runtime JSON for Writer. Required top-level fields: "
        "schema_version, plan_id, source_diagnostic_id, doc_snapshot_hash, preview_only, patches. "
        "Use schema_version \"v2-w3-runtime-1\". Use plan_id \"ap-inline-");
    aPrompt.append(sIdSuffix);
    aPrompt.append("\". Use source_diagnostic_id \"diag-inline-");
    aPrompt.append(sIdSuffix);
    aPrompt.append(
        "\". Use doc_snapshot_hash "
        "\"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\". "
        "Use preview_only false. patches must be a non-empty array. Every patch must include "
        "patch_id, kind, target, severity, and rationale. target must include paragraph_id \"");
    aPrompt.append(sParagraphId);
    aPrompt.append(
        "\". Allowed kind values are exactly: paragraph-replace, paragraph-insert-after, "
        "paragraph-delete, paragraph-format, paragraph-reformat, text-range-replace, text-format. "
        "For rewrite/expand/shorten/translate/custom, prefer one paragraph-replace patch with "
        "patch_id \"p1\", severity \"minor\", before omitted unless known, and after set to "
        "the rewritten paragraph text. For format-clean, prefer paragraph-format or "
        "paragraph-reformat with format_changes. For paragraph-delete, include force true. "
        "For text-range-replace and text-format, include range {\"start\":number,\"length\":number}. "
        "Never omit kind. Never return a JSON array. User action token: ");
    aPrompt.append(rActionToken);
    aPrompt.append(". User instruction: ");
    aPrompt.append(sInstruction);
    aPrompt.append(
        ". Output shape template: {\"schema_version\":\"v2-w3-runtime-1\","
        "\"plan_id\":\"ap-inline-");
    aPrompt.append(sIdSuffix);
    aPrompt.append(
        "\",\"source_diagnostic_id\":\"diag-inline-");
    aPrompt.append(sIdSuffix);
    aPrompt.append(
        "\",\"doc_snapshot_hash\":\"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"preview_only\":false,\"patches\":[{\"patch_id\":\"p1\","
        "\"kind\":\"paragraph-replace\",\"target\":{\"paragraph_id\":\"");
    aPrompt.append(sParagraphId);
    aPrompt.append(
        "\"},\"severity\":\"minor\",\"rationale\":\"inline provider rewrite\","
        "\"after\":\"<rewritten paragraph>\"}]}. Keep target as an object and preview_only as a boolean.");
    return aPrompt.makeStringAndClear();
}

void dispatchWriterInlineAction(const OUString& rJson, weld::Widget* pParent,
                                SfxObjectShell* pDocShell)
{
    const std::optional<OUString> oAction = lcl_extractJsonStringField(rJson, u"action");
    const std::optional<OUString> oParagraphId
        = lcl_extractJsonStringField(rJson, u"paragraph_id");
    const std::optional<OUString> oUserPrompt
        = lcl_extractJsonStringField(rJson, u"user_prompt");
    const std::optional<OUString> oRequestId
        = lcl_extractJsonStringField(rJson, u"request_id");

    if (!oAction)
    {
        SAL_INFO("sw.inline_actions",
                 "dispatchWriterInlineAction: missing action in request");
        return;
    }

    const std::optional<OUString> oCapability = mapWriterActionToProviderCapability(*oAction);
    if (!oCapability)
    {
        SAL_INFO("sw.inline_actions",
                 "dispatchWriterInlineAction: no provider capability for action="
                     << *oAction);
        return;
    }

    const css::uno::Reference<css::uno::XComponentContext>& xContext
        = comphelper::getProcessComponentContext();
    css::uno::Reference<css::ai::XProvider> xProvider(
        xContext->getServiceManager()->createInstanceWithContext(
            u"com.sun.star.ai.Provider"_ustr, xContext),
        css::uno::UNO_QUERY);
    if (!xProvider.is())
    {
        SAL_INFO("sw.inline_actions",
                 "dispatchWriterInlineAction: com.sun.star.ai.Provider unavailable");
        return;
    }

    css::ai::ProviderRequest aReq;
    aReq.capability = *oCapability;
    aReq.prompt = buildWriterRuntimeJsonPromptForProvider(
        *oAction, oUserPrompt && !oUserPrompt->isEmpty() ? *oUserPrompt : OUString(),
        oParagraphId ? *oParagraphId : OUString(), oRequestId ? *oRequestId : OUString());
    aReq.context = oParagraphId ? *oParagraphId : OUString();
    aReq.timeoutMs = 30000;

    const css::ai::ProviderResponse aRsp = xProvider->call(aReq);

    SAL_INFO("sw.inline_actions",
             "dispatchWriterInlineAction action=" << *oAction << " capability=" << *oCapability
                                                  << " status=" << aRsp.status
                                                  << " evidenceId=" << aRsp.evidenceId
                                                  << " durationMs=" << aRsp.durationMs);

    const OUString sRequestId = oRequestId ? *oRequestId : u"unknown"_ustr;
    const OUString sPlanId = u"ap-inline-"_ustr + sRequestId;
    const OUString sDiagId = u"diag-inline-"_ustr + sRequestId;

    if (aRsp.status == u"ok"_ustr)
    {
        auto* pSwDocShell = dynamic_cast<SwDocShell*>(pDocShell);
        if (!pSwDocShell || !oParagraphId)
            return;

        std::optional<sw::intelligent::ApplyPlan> oPlan
            = sw::intelligent::TryParseApplyPlanRuntimeJson(aRsp.content, *pSwDocShell);
        if (!oPlan)
        {
            oPlan = sw::intelligent::BuildSingleParagraphReplacePlan(
                *pSwDocShell, sPlanId, *oParagraphId, aRsp.content, sDiagId);
        }

        OUString aShapeError;
        if (oPlan && sw::intelligent::ValidateApplyPlanShape(*oPlan, aShapeError))
            pSwDocShell->applyDiagnosticsPlan(*oPlan);
        return;
    }

    if (aRsp.status == u"provider-error"_ustr && !aRsp.evidenceId.isEmpty() && pParent)
    {
        svx::sidebar::diff_review::DiffReviewPatchEntry aEntry;
        aEntry.maPatchId = sRequestId;
        aEntry.maKind = *oAction;
        aEntry.maStatus = lcl_statusWithPreview(aRsp.status, aRsp.content);
        aEntry.mbApplied = false;

        svx::sidebar::diff_review::ShowDiffReviewPanel(pParent, sPlanId, { aEntry }, pDocShell,
                                                       0);
    }
}

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
