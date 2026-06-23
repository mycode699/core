/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W7/M6: companion approval runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatLocalCloudSyncRuntime.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatCompanionDataBoundary
{
    bool StoresDocumentContent = false;
    bool StoresDiffSummaryOnly = true;
    bool AllowApprovalOffline = false;
};

struct AIChatCompanionPairingToken
{
    OUString PairingId;
    OUString SchemaVersion;
    OUString CreatedAt;
    OUString ExpiresAt;
    OUString TenantId;
    OUString WorkspaceId;
    OUString DesktopInstanceId;
    OUString DesktopApp;
    OUString LanEndpointHash;
    OUString ProposedDeviceId;
    OUString DevicePlatform;
    bool BindingRequired = true;
    OUString TransportMode;
    sal_Int32 Port = 17801;
    bool PublicEgress = false;
    bool CloudPushOptIn = false;
    bool MdnsRequired = true;
    sal_Int32 TtlSeconds = 600;
    sal_Int32 SessionTtlHours = 24;
    OUString TokenHash;
    bool StoresSecret = false;
    bool PinConfirmationRequired = true;
    bool BiometricEnrollmentRequired = true;
    bool MTLSRequired = true;
    bool Revocable = true;
    AIChatCompanionDataBoundary DataBoundary;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    OUString AuditLogRef;
    OUString TenantContextRef;
    OUString SyncMessageRef;
};

struct AIChatCompanionDiffSummary
{
    OUString SummaryId;
    OUString SchemaVersion;
    OUString CreatedAt;
    OUString TaskId;
    OUString StepResultId;
    OUString TenantId;
    OUString WorkspaceId;
    OUString Surface;
    OUString SummaryKind;
    OUString ApplyPlanRef;
    OUString ActionKind;
    sal_Int32 ActionCount = 0;
    OUString PreviewHash;
    OUString HumanSummaryHash;
    bool StoresDocumentContent = false;
    bool MobileParsesApplyPlan = false;
    OUString RiskLevel;
    bool RequiresApproval = true;
    OUString SnippetStorage;
    bool ContainsOriginalText = false;
    OUString CacheMode;
    bool ViewOnly = true;
    bool CanEdit = false;
    bool OfflineApproval = false;
    AIChatCompanionDataBoundary DataBoundary;
    std::vector<OUString> ChangedObjectRefs;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    OUString AuditLogRef;
    OUString TenantContextRef;
    OUString SyncMessageRef;
};

struct AIChatCompanionApprovalRequest
{
    OUString RequestId;
    OUString SchemaVersion;
    OUString CreatedAt;
    OUString ExpiresAt;
    OUString TaskId;
    OUString SummaryId;
    OUString PairingId;
    OUString TenantId;
    OUString WorkspaceId;
    OUString ActorId;
    OUString ActorRole;
    OUString Channel;
    OUString RequestState;
    std::vector<OUString> AvailableActions;
    bool RequiresOnline = true;
    OUString TransportMode;
    sal_Int32 LocalGatewayPort = 17801;
    bool PublicEgress = false;
    bool CloudPushOptIn = false;
    bool BiometricRequired = true;
    bool SecondConfirmRequired = true;
    bool MobileMayEdit = false;
    bool DecisionWritesAudit = true;
    AIChatCompanionDataBoundary DataBoundary;
    bool AuditLogRequired = true;
    bool AuditEvidenceRequired = true;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    OUString AuditLogRef;
    OUString TenantContextRef;
    OUString SyncMessageRef;
    OUString ReviewItemRef;
};

struct AIChatCompanionApprovalDecision
{
    OUString DecisionId;
    OUString RequestId;
    OUString SummaryId;
    OUString PairingId;
    OUString CreatedAt;
    OUString ActorId;
    OUString Decision;
    OUString EvidenceId;
    OUString HashReference;
    OUString AuditLogRef;
    OUString SyncMessageRef;
    bool AppliesDocumentChange = false;
    bool MobileMayEdit = false;
    bool BiometricConfirmed = true;
    bool SecondConfirmCompleted = true;
};

struct AIChatCompanionApprovalResult
{
    bool Success = false;
    OUString ObjectId;
    OUString Message;
};

class AIChatCompanionApprovalRuntime final
{
public:
    AIChatCompanionApprovalRuntime();

    const OUString& GetCompanionStoreUrl() const { return m_sCompanionStoreUrl; }

    AIChatCompanionApprovalResult PublishPairingToken(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatCompanionPairingToken& rToken) const;
    AIChatCompanionApprovalResult PublishDiffSummary(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatCompanionDiffSummary& rSummary) const;
    AIChatCompanionApprovalResult PublishApprovalRequest(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatCompanionApprovalRequest& rRequest) const;
    AIChatCompanionApprovalResult RecordApprovalDecision(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatCompanionApprovalRequest& rRequest,
        const AIChatCompanionApprovalDecision& rDecision) const;

    static bool IsCompanionIdAllowed(const OUString& rId, const OUString& rPrefix);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsDesktopAppAllowed(const OUString& rDesktopApp);
    static bool IsDevicePlatformAllowed(const OUString& rPlatform);
    static bool IsPairingTransportAllowed(const OUString& rTransport);
    static bool IsApprovalTransportAllowed(const OUString& rTransport);
    static bool IsSurfaceAllowed(const OUString& rSurface);
    static bool IsSummaryKindAllowed(const OUString& rKind);
    static bool IsActionKindAllowedForSurface(const OUString& rSurface,
                                              const OUString& rActionKind);
    static bool IsRiskLevelAllowed(const OUString& rRiskLevel);
    static bool IsSnippetStorageAllowed(const OUString& rSnippetStorage);
    static bool IsRequestStateAllowed(const OUString& rState);
    static bool IsApprovalActionAllowed(const OUString& rAction);
    static bool IsDecisionAllowed(const OUString& rDecision);
    static bool IsRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsDataBoundaryAllowed(const AIChatCompanionDataBoundary& rBoundary);
    static bool IsPairingTokenShapeAllowed(const AIChatCompanionPairingToken& rToken);
    static bool IsDiffSummaryShapeAllowed(const AIChatCompanionDiffSummary& rSummary);
    static bool IsApprovalRequestShapeAllowed(const AIChatCompanionApprovalRequest& rRequest);
    static bool IsApprovalDecisionShapeAllowed(const AIChatCompanionApprovalRequest& rRequest,
                                               const AIChatCompanionApprovalDecision& rDecision);
    static OUString MakePairingId(const AIChatTenantActionScope& rScope,
                                  const OUString& rCreatedAt);
    static OUString MakeSummaryId(const AIChatTenantActionScope& rScope,
                                  const OUString& rTaskId, const OUString& rCreatedAt);
    static OUString MakeRequestId(const AIChatTenantActionScope& rScope,
                                  const OUString& rSummaryId, const OUString& rCreatedAt);
    static OUString MakeDecisionId(const AIChatCompanionApprovalRequest& rRequest,
                                   const AIChatCompanionApprovalDecision& rDecision);

private:
    OUString m_sStorageRootUrl;
    OUString m_sCompanionStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

