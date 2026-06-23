/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent ShadowDoc bridge).
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

struct AIChatAgentShadowDocRequest
{
    OUString TaskId;
    sal_Int32 StepIndex = 0;
    OUString OwnerSurface;
    OUString ShadowBranchId;
    OUString ApplyPlanRuntimeRef;
    OUString ApplyPlanSchemaVersion;
    OUString DocumentSnapshotHash;
    OUString ShadowSnapshotRef;
    OUString DiffHashReference;
    OUString EvidenceId;
    OUString AuditReplayRef;
    bool ApplyPlanRuntimeValidated = false;
    bool MainDocumentUnchanged = true;
    bool UserApprovedMerge = false;
};

struct AIChatAgentShadowDocResult
{
    bool Success = false;
    AIChatAgentStepResultEntry StepResult;
    AIChatAgentTaskStateEntry TaskState;
    AIChatContentRegistryEntry RegistryEntry;
    OUString SourceId;
    OUString CitationId;
    OUString Message;
};

class AIChatAgentShadowDocBridge final
{
public:
    AIChatAgentShadowDocResult
    PreparePatchStep(const AIChatAgentTaskStateEntry& rCurrentState,
                     const AIChatAgentShadowDocRequest& rRequest) const;

    static bool IsApplyPlanRuntimeRefAllowed(const OUString& rApplyPlanRuntimeRef);
    static bool IsDocumentSnapshotHashAllowed(const OUString& rDocumentSnapshotHash);
    static bool IsShadowSnapshotRefAllowed(const OUString& rShadowSnapshotRef);
    static bool IsShadowDocRequestAllowed(const AIChatAgentTaskStateEntry& rCurrentState,
                                          const AIChatAgentShadowDocRequest& rRequest);
    static OUString MakeShadowDocStepObjectId(const OUString& rTaskId, sal_Int32 nStepIndex);
    static OUString MakeShadowDocHashReference(const AIChatAgentShadowDocRequest& rRequest);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
