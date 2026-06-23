/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W8/M6: local cloud sync runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatAuditLogRuntime.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatLocalCloudSyncPayloadRef
{
    OUString RefType;
    OUString RefId;
    OUString HashReference;
    bool StoresDocumentContent = false;
    bool ContainsRawPayload = false;
};

struct AIChatLocalCloudSyncTransport
{
    OUString Mode;
    OUString EndpointClass;
    sal_Int32 Port = 17802;
    bool PublicEgress = false;
    bool MTLSRequired = true;
};

struct AIChatLocalCloudSyncOrdering
{
    sal_Int32 Sequence = 0;
    OUString IdempotencyKey;
    bool AckRequired = true;
};

struct AIChatLocalCloudSyncBoundary
{
    OUString ServiceMode;
    bool StoresDocumentContent = false;
    bool HashOnly = true;
    bool DefaultNoPublicEgress = true;
};

struct AIChatLocalCloudSyncMessage
{
    OUString SyncMessageId;
    OUString SchemaVersion;
    OUString CreatedAt;
    OUString TenantId;
    OUString WorkspaceId;
    OUString Channel;
    OUString Direction;
    OUString Kind;
    AIChatLocalCloudSyncPayloadRef Payload;
    AIChatLocalCloudSyncTransport Transport;
    AIChatLocalCloudSyncOrdering Ordering;
    AIChatLocalCloudSyncBoundary Boundary;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    OUString AuditLogRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString TenantContextRef;
};

struct AIChatLocalCloudSyncResult
{
    bool Success = false;
    AIChatLocalCloudSyncMessage MessageRecord;
    OUString Message;
};

class AIChatLocalCloudSyncRuntime final
{
public:
    AIChatLocalCloudSyncRuntime();

    const OUString& GetSyncMessageUrl() const { return m_sSyncMessageUrl; }

    AIChatLocalCloudSyncResult AppendMessage(const AIChatTenantContext& rContext,
                                             const AIChatTenantActionScope& rScope,
                                             const AIChatLocalCloudSyncMessage& rMessage) const;
    AIChatLocalCloudSyncResult AppendAcknowledgement(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatLocalCloudSyncMessage& rSourceMessage, const OUString& rCreatedAt,
        sal_Int32 nSequence, const OUString& rEvidenceId) const;
    std::vector<AIChatLocalCloudSyncMessage> LoadMessages() const;

    static bool IsSyncMessageIdAllowed(const OUString& rSyncMessageId);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsChannelAllowed(const OUString& rChannel);
    static bool IsDirectionAllowed(const OUString& rDirection);
    static bool IsKindAllowed(const OUString& rKind);
    static bool IsPayloadRefTypeAllowed(const OUString& rRefType);
    static bool IsPayloadRefIdAllowed(const OUString& rRefId);
    static bool IsPayloadShapeAllowed(const AIChatLocalCloudSyncPayloadRef& rPayload);
    static bool IsTransportModeAllowed(const OUString& rMode);
    static bool IsEndpointClassAllowed(const OUString& rEndpointClass);
    static bool IsTransportShapeAllowed(const AIChatLocalCloudSyncTransport& rTransport);
    static bool IsOrderingShapeAllowed(const AIChatLocalCloudSyncOrdering& rOrdering);
    static bool IsBoundaryShapeAllowed(const AIChatLocalCloudSyncBoundary& rBoundary);
    static bool IsRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsMessageShapeAllowed(const AIChatLocalCloudSyncMessage& rMessage);
    static OUString MakeSyncMessageId(const AIChatTenantActionScope& rScope,
                                      const OUString& rKind, sal_Int32 nSequence,
                                      const OUString& rCreatedAt);
    static OUString MakeIdempotencyKey(const AIChatLocalCloudSyncMessage& rMessage);
    static OUString MakeAuditLogRef(const AIChatAuditLogEntry& rAuditEntry);
    static OUString MakeTenantContextRef(const AIChatTenantContext& rContext);

private:
    OUString m_sStorageRootUrl;
    OUString m_sSyncMessageUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

