/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "InlineActionProviderDispatch.hxx"
#include "InlineActionRequest.hxx"
#include "InlineActionSlideApply.hxx"

#include <DrawDocShell.hxx>
#include <DrawViewShell.hxx>

#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/processfactory.hxx>
#include <optional>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <sfx2/objsh.hxx>
#include <svx/sidebar/DiffReviewPanel.hxx>

namespace sd::inline_actions {
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

OUString lcl_defaultPromptForAction(SlideElementAction eAction)
{
    switch (eAction)
    {
        case SlideElementAction::RewriteText:
            return u"Rewrite the selected slide text."_ustr;
        case SlideElementAction::AdjustColor:
            return u"Adjust colors for the selected slide element."_ustr;
        case SlideElementAction::Relayout:
            return u"Relayout the selected slide element."_ustr;
        case SlideElementAction::TranslateText:
            return u"Translate the selected slide text."_ustr;
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

OUString offlineCapabilityForSlideElementAction(SlideElementAction eAction)
{
    switch (eAction)
    {
        case SlideElementAction::RewriteText:
        case SlideElementAction::TranslateText:
            return u"rewrite"_ustr;
        case SlideElementAction::AdjustColor:
        case SlideElementAction::Relayout:
            return u"format-fix"_ustr;
    }
    return OUString();
}

void dispatchImpressInlineAction(const OUString& rJsonRequest, SlideElementAction eAction,
                                 SfxObjectShell* pDocShell, weld::Widget* pDiffReviewParent)
{
    const OUString aCapability = offlineCapabilityForSlideElementAction(eAction);
    if (aCapability.isEmpty())
    {
        SAL_INFO("sd.inline_actions",
                 "dispatchImpressInlineAction: no provider capability for action");
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
        SAL_INFO("sd.inline_actions",
                 "dispatchImpressInlineAction: com.sun.star.ai.Provider unavailable");
        return;
    }

    const std::optional<OUString> oElementId
        = lcl_extractJsonStringField(rJsonRequest, u"element_id");
    const std::optional<OUString> oRequestId
        = lcl_extractJsonStringField(rJsonRequest, u"request_id");

    css::ai::ProviderRequest aReq;
    aReq.capability = aCapability;
    aReq.prompt = lcl_defaultPromptForAction(eAction);
    aReq.context = oElementId ? *oElementId : OUString();
    aReq.timeoutMs = 30000;

    const css::ai::ProviderResponse aRsp = xProvider->call(aReq);

    SAL_INFO("sd.inline_actions",
             "dispatchImpressInlineAction action=" << toToken(eAction)
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
        if (DrawDocShell* pDrawDocShell = dynamic_cast<DrawDocShell*>(pDocShell))
        {
            if (DrawViewShell* pViewShell
                = dynamic_cast<DrawViewShell*>(pDrawDocShell->GetViewShell()))
            {
                bApplied = tryApplyProviderContentToMarkedTextShape(*pViewShell, aRsp.content,
                                                                    eAction);
            }
        }
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

} // namespace sd::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */