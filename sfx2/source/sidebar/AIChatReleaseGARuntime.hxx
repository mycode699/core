/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: release GA runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatTenantContextRuntime.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatReleaseGAScope
{
    OUString Product;
    OUString ReleasePhase;
    std::vector<OUString> SupportedPlatforms;
    OUString DefaultDataBoundary;
};

struct AIChatReleaseGAReadinessGate
{
    OUString GateId;
    OUString Title;
    OUString Status;
    OUString Owner;
    bool BlocksGA = true;
    std::vector<OUString> RequiredEvidence;
};

struct AIChatReleaseGAApprovals
{
    bool HumanApprovalRequired = true;
    bool AutomatedApprovalAllowed = false;
    std::vector<OUString> Approvers;
    bool SignoffEvidenceRequired = true;
};

struct AIChatReleaseGAGateState
{
    bool BlocksGA = true;
    bool CanShip = false;
    bool RequiresExplicitUserAuthorization = true;
    OUString RuntimeImplementation;
    bool ArtifactPublishingRuntimeActive = false;
    bool CodeSigningRuntimeActive = false;
    bool NotarizationSubmissionRuntimeActive = false;
    bool UpdateChannelPublicationRuntimeActive = false;
    bool ReleaseUploadRuntimeActive = false;
    bool ExternalMetricUploadRuntimeActive = false;
    bool PublicNetworkRuntimeActive = false;
};

struct AIChatReleaseGAChecklist
{
    OUString ChecklistId;
    OUString SchemaVersion;
    OUString CreatedAt;
    AIChatReleaseGAScope Scope;
    std::vector<AIChatReleaseGAReadinessGate> ReadinessGates;
    AIChatReleaseGAApprovals Approvals;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    AIChatReleaseGAGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString DistributionRecoveryRef;
    OUString PerfCrashRef;
    OUString NoEgressRef;
    OUString V2RegressionRef;
    OUString V3SelfTestRef;
    OUString V3OnlyRef;
    OUString SourceArchiveRef;
    OUString WindowsToastProofRef;
    OUString ReleasePolicyDecisionRef;
    OUString ReleaseEvidenceRef;
    OUString HashReference;
};

struct AIChatReleaseGAGateEvidenceRecord
{
    OUString RecordId;
    OUString ChecklistId;
    OUString GateId;
    OUString GateStatus;
    bool GateGreen = true;
    bool BlocksGA = true;
    std::vector<OUString> EvidenceRefs;
    OUString ArtifactRef;
    OUString SigningRef;
    OUString UpdateChannelRef;
    OUString RecoveryProofRef;
    OUString PolicyDecisionRef;
    bool PublicEgressRequired = false;
    bool StoresRawEvidence = false;
    OUString EvidenceId;
    OUString AuditLogRef;
};

struct AIChatReleaseGASignoffRecord
{
    OUString SignoffId;
    OUString ChecklistId;
    OUString ApproverRole;
    OUString ApproverId;
    OUString HumanApprovalRef;
    OUString ReleaseSignoffRef;
    OUString ArtifactBundleRef;
    OUString SigningRef;
    OUString UpdateChannelRef;
    OUString RecoveryProofRef;
    OUString SourceArchiveRef;
    OUString WindowsToastProofRef;
    OUString ReleasePolicyDecisionRef;
    OUString PolicyDecisionRef;
    bool HumanApprovalRecorded = true;
    bool AutomatedApproval = false;
    bool AllBlockingGatesGreen = true;
    bool ArtifactsSigned = true;
    bool UpdateChannelReady = true;
    bool RecoveryProofPresent = true;
    bool RequiresExplicitUserAuthorization = true;
    bool CanShip = false;
    bool ReleasePublished = false;
    OUString EvidenceId;
    OUString AuditLogRef;
};

struct AIChatReleaseGAResult
{
    bool Success = false;
    AIChatReleaseGAChecklist Checklist;
    AIChatReleaseGAGateEvidenceRecord GateEvidence;
    AIChatReleaseGASignoffRecord Signoff;
    OUString Message;
};

class AIChatReleaseGARuntime final
{
public:
    AIChatReleaseGARuntime();

    const OUString& GetReleaseGAStoreUrl() const { return m_sReleaseGAStoreUrl; }

    AIChatReleaseGAResult SaveReleaseGAChecklist(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatReleaseGAChecklist& rChecklist) const;
    AIChatReleaseGAResult RecordGateEvidence(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatReleaseGAChecklist& rChecklist,
        const AIChatReleaseGAGateEvidenceRecord& rRecord) const;
    AIChatReleaseGAResult RecordReleaseSignoff(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatReleaseGAChecklist& rChecklist,
        const AIChatReleaseGASignoffRecord& rRecord) const;

    static bool IsChecklistIdAllowed(const OUString& rChecklistId);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsReleasePlatformAllowed(const OUString& rPlatform);
    static bool IsReleaseGateIdAllowed(const OUString& rGateId);
    static bool IsReleaseGateStatusAllowed(const OUString& rGateId,
                                           const OUString& rStatus);
    static bool IsReleaseGateOwnerAllowed(const OUString& rOwner);
    static bool IsReleaseGateEvidenceAllowed(const OUString& rEvidence);
    static bool IsReleaseRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsReleaseApproverAllowed(const OUString& rApprover);
    static bool IsReleaseScopeShapeAllowed(const AIChatReleaseGAScope& rScope);
    static bool IsReleaseGateRosterAllowed(
        const std::vector<AIChatReleaseGAReadinessGate>& rGates);
    static bool IsReleaseApprovalsShapeAllowed(const AIChatReleaseGAApprovals& rApprovals);
    static bool IsReleaseChecklistShapeAllowed(const AIChatReleaseGAChecklist& rChecklist);
    static bool IsGateEvidenceRecordShapeAllowed(
        const AIChatReleaseGAChecklist& rChecklist,
        const AIChatReleaseGAGateEvidenceRecord& rRecord);
    static bool IsReleaseSignoffRecordShapeAllowed(
        const AIChatReleaseGAChecklist& rChecklist,
        const AIChatReleaseGASignoffRecord& rRecord);
    static OUString MakeReleaseGAChecklistId(const OUString& rCreatedAt);
    static OUString MakeReleaseGAChecklistHashReference(
        const AIChatReleaseGAChecklist& rChecklist);
    static OUString MakeGateEvidenceRecordId(
        const AIChatReleaseGAChecklist& rChecklist,
        const AIChatReleaseGAGateEvidenceRecord& rRecord);
    static OUString MakeReleaseSignoffId(const AIChatReleaseGAChecklist& rChecklist,
                                         const AIChatReleaseGASignoffRecord& rRecord);

private:
    OUString m_sStorageRootUrl;
    OUString m_sReleaseGAStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
