/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace content opener).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatContentOpener.hxx"

namespace sfx2::sidebar
{

OUString AIChatContentOpener::ResolveOpenTarget(const AIChatContentRegistryEntry& rEntry)
{
    return AIChatPreviewMatrix::ResolvePreviewTarget(rEntry);
}

bool AIChatContentOpener::IsSupportedTarget(const OUString& rTarget)
{
    return rTarget == u"main-document-window"_ustr || rTarget == u"sidebar-preview"_ustr
           || rTarget == u"diff-review"_ustr || rTarget == u"evidence-inspector"_ustr
           || rTarget == u"review-queue"_ustr;
}

AIChatContentOpenResult
AIChatContentOpener::OpenReadOnlyPreview(const AIChatContentRegistryEntry& rEntry) const
{
    AIChatPreviewMatrix aPreviewMatrix;
    const AIChatPreviewResult aPreview = aPreviewMatrix.BuildPreview(rEntry);

    AIChatContentOpenResult aResult;
    aResult.ObjectId = rEntry.ObjectId;
    aResult.Target = aPreview.Target;
    aResult.PreviewMode = aPreview.Mode;
    aResult.PreviewSummary = aPreview.Summary;

    if (rEntry.ObjectId.isEmpty())
    {
        aResult.Message = u"open-failed reason=missing-object-id"_ustr;
        return aResult;
    }

    if (!IsSupportedTarget(aResult.Target))
    {
        aResult.Message = u"open-failed reason=unsupported-target target="_ustr + aResult.Target;
        return aResult;
    }

    aResult.Success = true;
    aResult.Message = u"opened id="_ustr + rEntry.ObjectId + u" target="_ustr + aResult.Target
                      + u" preview-mode="_ustr + aResult.PreviewMode
                      + u" preview-matrix=true"_ustr
                      + u" read-only=true main-document-mutation=false"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
