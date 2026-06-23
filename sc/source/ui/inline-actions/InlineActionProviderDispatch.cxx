/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "InlineActionProviderDispatch.hxx"
#include "InlineActionCellApply.hxx"
#include "InlineActionRequest.hxx"

#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/processfactory.hxx>
#include <optional>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <sfx2/objsh.hxx>
#include <svx/sidebar/DiffReviewPanel.hxx>
#include <tabvwsh.hxx>

namespace sc::inline_actions {
namespace {

constexpr sal_Int32 kContentPreviewMaxLen = 80;

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

OUString lcl_defaultPromptForAction(CellAction eAction)
{
    switch (eAction)
    {
        case CellAction::ExplainData:
            return u"Explain the data in the selected cell range."_ustr;
        case CellAction::SuggestChart:
            return u"Suggest a chart for the selected cell range."_ustr;
        case CellAction::GenerateFormula:
            return u"Generate a formula for the selected cell range."_ustr;
        case CellAction::FormatClean:
            return u"Clean up formatting in the selected cell range."_ustr;
        case CellAction::FormatChange:
            return u"Change number/date format in the selected cell range."_ustr;
    }
    return OUString();
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

OUString offlineCapabilityForCellAction(CellAction eAction)
{
    switch (eAction)
    {
        case CellAction::ExplainData:
            return u"summarize"_ustr;
        case CellAction::SuggestChart:
        case CellAction::GenerateFormula:
            return u"intent-to-uno"_ustr;
        case CellAction::FormatClean:
        case CellAction::FormatChange:
            return u"format-fix"_ustr;
    }
    return OUString();
}

void dispatchCalcInlineAction(const OUString& rJsonRequest, CellAction eAction,
                              SfxObjectShell* pDocShell, weld::Widget* pDiffReviewParent)
{
    const OUString aCapability = offlineCapabilityForCellAction(eAction);
    if (aCapability.isEmpty())
    {
        SAL_INFO("sc.inline_actions",
                 "dispatchCalcInlineAction: no provider capability for action");
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
        SAL_INFO("sc.inline_actions",
                 "dispatchCalcInlineAction: com.sun.star.ai.Provider unavailable");
        return;
    }

    const std::optional<OUString> oSheet = lcl_extractJsonStringField(rJsonRequest, u"sheet");
    const std::optional<OUString> oRange = lcl_extractJsonStringField(rJsonRequest, u"range");
    const std::optional<OUString> oRequestId
        = lcl_extractJsonStringField(rJsonRequest, u"request_id");

    OUString aContext;
    if (oSheet && oRange)
        aContext = *oSheet + u"!"_ustr + *oRange;

    css::ai::ProviderRequest aReq;
    aReq.capability = aCapability;
    aReq.prompt = lcl_defaultPromptForAction(eAction);
    aReq.context = aContext;
    aReq.timeoutMs = 30000;

    const css::ai::ProviderResponse aRsp = xProvider->call(aReq);

    SAL_INFO("sc.inline_actions",
             "dispatchCalcInlineAction action=" << toToken(eAction)
             << " capability=" << aCapability << " status=" << aRsp.status
             << " evidenceId=" << aRsp.evidenceId << " durationMs=" << aRsp.durationMs);

    if (!actionRoutesToDiff(eAction))
        return;

    const bool bOpenDiff = aRsp.status == u"ok"_ustr
                           || (aRsp.status == u"provider-error"_ustr && !aRsp.evidenceId.isEmpty());
    if (!bOpenDiff || !pDiffReviewParent)
        return;

    const OUString sRequestId = oRequestId ? *oRequestId : u"unknown"_ustr;
    const OUString sPlanId = u"ap-inline-"_ustr + sRequestId;

    bool bApplied = false;
    if (aRsp.status == u"ok"_ustr)
    {
        if (ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell())
            bApplied = tryApplyProviderContentToMarkedCell(*pViewShell, aRsp.content, eAction);
    }

    svx::sidebar::diff_review::DiffReviewPatchEntry aEntry;
    aEntry.maPatchId = sRequestId;
    aEntry.maKind = toToken(eAction);
    aEntry.maStatus = bApplied ? u"ok"_ustr
                               : lcl_statusWithPreview(aRsp.status, aRsp.content);
    aEntry.mbApplied = bApplied;

    svx::sidebar::diff_review::ShowDiffReviewPanel(pDiffReviewParent, sPlanId, { aEntry },
                                                   pDocShell, 0);
}

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */