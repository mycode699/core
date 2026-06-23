/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: distribution/recovery runtime).
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

struct AIChatDistributionChannel
{
    OUString Platform;
    OUString Artifact;
    bool Primary = true;
    bool InstallerSmokeRequired = true;
    sal_Int32 DownloadToFirstPatchMaxMinutes = 5;
};

struct AIChatDistributionPolicy
{
    std::vector<AIChatDistributionChannel> FirstLaunchChannels;
    bool ArtifactSigningRequired = true;
    bool ChecksumRequired = true;
    bool NotarizationRequired = true;
    bool OfflineInstallSupported = true;
    bool NoPublicCloudRequired = true;
};

struct AIChatUpdatePolicy
{
    OUString Mode;
    bool PromptRequired = true;
    bool OneClick = true;
    bool Deferrable = true;
    bool ForceUpdateAllowed = false;
    OUString SelfHostServer;
    bool LanSupported = true;
    bool PublicInternetRequired = false;
    bool RollbackRequired = true;
};

struct AIChatDistributionOnboardingPolicy
{
    sal_Int32 DownloadToFirstPatchMaxMinutes = 5;
    bool RequiresStarterPack = true;
    bool RequiresOnboardingFlow = true;
};

struct AIChatDistributionUpdateGateState
{
    bool BlocksGA = true;
    bool RequiresV2RegressionGreen = true;
    OUString RuntimeImplementation;
    bool InstallerPackagingRuntimeActive = false;
    bool UpdateServerRuntimeActive = false;
    bool NetworkDownloadRuntimeActive = false;
    bool UpdaterDaemonActive = false;
};

struct AIChatDistributionUpdateManifest
{
    OUString ManifestId;
    OUString SchemaVersion;
    OUString CreatedAt;
    AIChatDistributionPolicy Distribution;
    AIChatUpdatePolicy Update;
    AIChatDistributionOnboardingPolicy Onboarding;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    AIChatDistributionUpdateGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString EditionPolicyRef;
    OUString I18nLocalePolicyRef;
    OUString ManualDocsRef;
    OUString NoEgressRef;
    OUString ReleaseEvidenceRef;
    OUString HashReference;
};

struct AIChatUpdateRollbackSmokeRecord
{
    OUString RecordId;
    OUString ManifestId;
    OUString Platform;
    OUString Artifact;
    OUString SmokeKind;
    OUString ArtifactSignatureRef;
    OUString ChecksumRef;
    OUString NotarizationRef;
    OUString RollbackProofRef;
    sal_Int32 DownloadToFirstPatchMinutes = 5;
    bool PromptShown = true;
    bool OneClickAvailable = true;
    bool Deferrable = true;
    bool Forced = false;
    bool PublicInternetRequired = false;
    bool LanSupported = true;
    OUString EvidenceId;
    OUString AuditLogRef;
};

struct AIChatErrorRecoveryPolicy
{
    OUString Presentation;
    bool NextStepRequired = true;
    bool EvidenceOpenable = true;
    bool DeadEndAllowed = false;
    bool MainDocumentUnchangedUntilApply = true;
    bool HumanReadableCauseRequired = true;
};

struct AIChatErrorRecoveryScenario
{
    OUString Kind;
    OUString Surface;
    OUString CauseToken;
    std::vector<OUString> NextSteps;
    bool EvidenceOpenable = true;
    OUString EvidenceId;
    bool DiagnosticsExportable = true;
    bool MainDocumentUnchanged = true;
    bool CanRetry = true;
    bool CanRollback = true;
    bool RequiresUserChoice = true;
};

struct AIChatErrorRecoveryGateState
{
    bool BlocksGA = true;
    bool RequiresV2RegressionGreen = true;
    OUString RuntimeImplementation;
    bool InlineGuidanceUiRuntimeActive = false;
    bool DiagnosticsExporterRuntimeActive = false;
    bool RemoteRecoveryServiceActive = false;
    bool CrashReportRuntimeActive = false;
    bool OsNotificationBridgeActive = false;
};

struct AIChatErrorRecoveryUxManifest
{
    OUString ManifestId;
    OUString SchemaVersion;
    OUString CreatedAt;
    AIChatErrorRecoveryPolicy Policy;
    std::vector<AIChatErrorRecoveryScenario> Scenarios;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    AIChatErrorRecoveryGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString EditionPolicyRef;
    OUString I18nLocalePolicyRef;
    OUString ManualDocsRef;
    OUString NoEgressRef;
    OUString ReleaseEvidenceRef;
    OUString HashReference;
};

struct AIChatRecoveryActionRecord
{
    OUString ActionId;
    OUString ManifestId;
    OUString ScenarioKind;
    OUString Surface;
    OUString ChosenStep;
    bool MainDocumentUnchanged = true;
    bool RetryAvailable = true;
    bool RollbackAvailable = true;
    bool DiagnosticsExportable = true;
    bool EvidenceOpenable = true;
    bool RequiresUserChoice = true;
    bool DocumentMutationApplied = false;
    OUString EvidenceId;
    OUString AuditLogRef;
};

struct AIChatDistributionRecoveryResult
{
    bool Success = false;
    AIChatDistributionUpdateManifest DistributionManifest;
    AIChatUpdateRollbackSmokeRecord SmokeRecord;
    AIChatErrorRecoveryUxManifest RecoveryManifest;
    AIChatRecoveryActionRecord RecoveryAction;
    OUString Message;
};

class AIChatDistributionRecoveryRuntime final
{
public:
    AIChatDistributionRecoveryRuntime();

    const OUString& GetDistributionRecoveryStoreUrl() const { return m_sDistributionRecoveryStoreUrl; }

    AIChatDistributionRecoveryResult SaveDistributionUpdatePolicy(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatDistributionUpdateManifest& rManifest) const;
    AIChatDistributionRecoveryResult RecordUpdateRollbackSmoke(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatDistributionUpdateManifest& rManifest,
        const AIChatUpdateRollbackSmokeRecord& rRecord) const;
    AIChatDistributionRecoveryResult SaveErrorRecoveryUxPolicy(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatErrorRecoveryUxManifest& rManifest) const;
    AIChatDistributionRecoveryResult RecordRecoveryAction(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatErrorRecoveryUxManifest& rManifest,
        const AIChatRecoveryActionRecord& rRecord) const;

    static bool IsDistributionManifestIdAllowed(const OUString& rManifestId);
    static bool IsErrorRecoveryManifestIdAllowed(const OUString& rManifestId);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsPlatformArtifactAllowed(const OUString& rPlatform, const OUString& rArtifact);
    static bool IsDistributionChannelOrderAllowed(
        const std::vector<AIChatDistributionChannel>& rChannels);
    static bool IsDistributionRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsErrorRecoveryRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsRecoveryKindAllowed(const OUString& rKind);
    static bool IsRecoverySurfaceAllowed(const OUString& rSurface);
    static bool IsNextStepAllowed(const OUString& rStep);
    static bool IsSmokeKindAllowed(const OUString& rSmokeKind);
    static bool IsDistributionPolicyShapeAllowed(const AIChatDistributionPolicy& rPolicy);
    static bool IsUpdatePolicyShapeAllowed(const AIChatUpdatePolicy& rPolicy);
    static bool IsDistributionManifestShapeAllowed(
        const AIChatDistributionUpdateManifest& rManifest);
    static bool IsUpdateRollbackSmokeRecordShapeAllowed(
        const AIChatDistributionUpdateManifest& rManifest,
        const AIChatUpdateRollbackSmokeRecord& rRecord);
    static bool IsErrorRecoveryPolicyShapeAllowed(const AIChatErrorRecoveryPolicy& rPolicy);
    static bool IsErrorRecoveryScenarioOrderAllowed(
        const std::vector<AIChatErrorRecoveryScenario>& rScenarios);
    static bool IsErrorRecoveryUxManifestShapeAllowed(
        const AIChatErrorRecoveryUxManifest& rManifest);
    static bool IsRecoveryActionRecordShapeAllowed(
        const AIChatErrorRecoveryUxManifest& rManifest,
        const AIChatRecoveryActionRecord& rRecord);
    static OUString MakeDistributionManifestId(const OUString& rCreatedAt);
    static OUString MakeErrorRecoveryManifestId(const OUString& rCreatedAt);
    static OUString MakeDistributionHashReference(
        const AIChatDistributionUpdateManifest& rManifest);
    static OUString MakeErrorRecoveryHashReference(
        const AIChatErrorRecoveryUxManifest& rManifest);
    static OUString MakeSmokeRecordId(const AIChatDistributionUpdateManifest& rManifest,
                                      const AIChatUpdateRollbackSmokeRecord& rRecord);
    static OUString MakeRecoveryActionId(const AIChatErrorRecoveryUxManifest& rManifest,
                                         const AIChatRecoveryActionRecord& rRecord);

private:
    OUString m_sStorageRootUrl;
    OUString m_sDistributionRecoveryStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
