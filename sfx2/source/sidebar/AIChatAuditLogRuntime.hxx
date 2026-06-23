/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W4/M6: audit log runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatPolicyEngineRuntime.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatAuditApprovalEntry
{
    OUString ApproverId;
    OUString Decision;
    OUString Timestamp;
    OUString EvidenceId;
};

struct AIChatAuditLogEntry
{
    OUString AuditId;
    OUString SchemaVersion;
    OUString Timestamp;
    OUString TenantId;
    OUString WorkspaceId;
    OUString ActorId;
    OUString ActorRole;
    OUString ActionType;
    OUString TargetRef;
    OUString ServiceMode;
    OUString DataClass;
    bool PublicEgress = false;
    OUString EvidenceId;
    OUString PolicyDecision;
    std::vector<AIChatAuditApprovalEntry> ApprovalChain;
    bool StoresDocumentContent = false;
    OUString PromptStorage;
    OUString HashAlgorithm;
    OUString PreviousHash;
    OUString EntryHash;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString HashReference;
};

struct AIChatAuditLogResult
{
    bool Success = false;
    AIChatAuditLogEntry Entry;
    OUString Message;
};

class AIChatAuditLogRuntime final
{
public:
    AIChatAuditLogRuntime();

    const OUString& GetAuditLogUrl() const { return m_sAuditLogUrl; }

    AIChatAuditLogResult AppendPolicyDecision(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatPolicyDecision& rDecision, const OUString& rTimestamp,
        const std::vector<AIChatAuditApprovalEntry>& rApprovalChain) const;
    AIChatAuditLogResult ValidateHashChain() const;
    std::vector<AIChatAuditLogEntry> LoadEntries() const;

    static bool IsAuditIdAllowed(const OUString& rAuditId);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsActionTypeAllowed(const OUString& rActionType);
    static bool IsPolicyDecisionAllowed(const OUString& rPolicyDecision);
    static bool IsApprovalDecisionAllowed(const OUString& rDecision);
    static bool IsPromptStorageAllowed(const OUString& rPromptStorage);
    static bool IsChainHashAllowed(const OUString& rHash);
    static bool IsEntryShapeAllowed(const AIChatAuditLogEntry& rEntry);
    static OUString NormalizeActionType(const OUString& rTargetType);
    static OUString NormalizePolicyDecision(const OUString& rDecision);
    static OUString MakeAuditId(const AIChatTenantActionScope& rScope,
                                const AIChatPolicyDecision& rDecision,
                                const OUString& rTimestamp);
    static OUString MakeEntryHash(const AIChatAuditLogEntry& rEntry);

private:
    OUString m_sStorageRootUrl;
    OUString m_sAuditLogUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
