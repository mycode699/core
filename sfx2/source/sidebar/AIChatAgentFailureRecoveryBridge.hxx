/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent failure recovery bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatAgentTaskStateStore.hxx"
#include "AIChatContentRegistry.hxx"

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatAgentFailureRecoveryResult
{
    bool Success = false;
    OUString TaskStepObjectId;
    OUString FailureCode;
    OUString EvidenceId;
    OUString HashReference;
    OUString RetryActionState;
    OUString CancelActionState;
    OUString OpenTarget;
    OUString PreviewMode;
    OUString ActivityCursor;
    OUString Message;
};

class AIChatAgentFailureRecoveryBridge final
{
public:
    AIChatAgentFailureRecoveryResult
    PublishFailedStep(const AIChatAgentStepResultEntry& rStepResult,
                      const AIChatAgentTaskStateEntry& rTaskState,
                      const OUString& rDocumentBinding) const;

    static bool IsDocumentBindingAllowed(const OUString& rDocumentBinding);
    static bool IsFailedStepRecoverable(const AIChatAgentStepResultEntry& rStepResult,
                                        const AIChatAgentTaskStateEntry& rTaskState);
    static OUString MakeFailureStepObjectId(const OUString& rTaskId, sal_Int32 nStepIndex);
    static OUString MakeFailureHashReference(const AIChatAgentStepResultEntry& rStepResult,
                                             const AIChatAgentTaskStateEntry& rTaskState);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
