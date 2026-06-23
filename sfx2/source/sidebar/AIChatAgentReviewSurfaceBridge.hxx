/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent review surface bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatAgentShadowDocBridge.hxx"
#include "AIChatContentReviewStore.hxx"
#include "AIChatEvidenceInspector.hxx"
#include "AIChatReviewQueueStore.hxx"
#include "AIChatWorkspaceSessionStore.hxx"

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatAgentReviewSurfaceResult
{
    bool Success = false;
    OUString TaskStepObjectId;
    OUString ReviewId;
    OUString QueueState;
    OUString EvidenceId;
    OUString HashReference;
    OUString OpenTarget;
    OUString PreviewMode;
    OUString ActivityCursor;
    OUString Message;
};

class AIChatAgentReviewSurfaceBridge final
{
public:
    AIChatAgentReviewSurfaceResult
    PublishShadowDocResult(const AIChatAgentShadowDocResult& rShadowResult,
                           const OUString& rDocumentBinding) const;

    static bool IsDocumentBindingAllowed(const OUString& rDocumentBinding);
    static bool IsShadowDocResultPublishable(const AIChatAgentShadowDocResult& rShadowResult);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
