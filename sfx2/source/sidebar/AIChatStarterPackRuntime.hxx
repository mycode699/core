/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: starter pack runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatOnboardingRuntime.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatStarterPackSamplePatch
{
    bool Required = true;
    OUString ActionKind;
    bool MustSucceed = true;
    bool RequiresUndo = true;
    bool EvidenceRequired = true;
    OUString ApplyPlanRef;
    OUString DiffReviewRef;
    OUString ApprovalRef;
    OUString EvidenceId;
};

struct AIChatStarterPackDataBoundary
{
    bool StoresDocumentContent = false;
    bool PublicEgress = false;
    bool LocalFirst = true;
    bool HashOnly = true;
};

struct AIChatStarterPackTemplate
{
    OUString TemplateId;
    OUString Surface;
    OUString Scenario;
    OUString Path;
    OUString TitleKey;
    std::vector<OUString> LocaleReady;
    OUString HashReference;
    AIChatStarterPackSamplePatch SamplePatch;
    AIChatStarterPackDataBoundary DataBoundary;
};

struct AIChatStarterPackCoverage
{
    sal_Int32 TemplateCount = 30;
    sal_Int32 BusinessScenarioCount = 10;
    sal_Int32 WriterCount = 10;
    sal_Int32 CalcCount = 10;
    sal_Int32 ImpressCount = 10;
    bool PatchSmokeRequired = true;
};

struct AIChatStarterPackInstallation
{
    bool Installable = true;
    bool RequiresNetwork = false;
    bool W8SelfHostedCompatible = true;
    OUString Distribution;
    bool EmbeddedDefault = true;
    OUString TemplateRoot;
    OUString LocaleStrategy;
    OUString BundleLocationRef;
};

struct AIChatStarterPackGateState
{
    bool BlocksGA = true;
    bool RequiresV2RegressionGreen = true;
    OUString RuntimeImplementation;
    bool TemplateAssetsInstalled = false;
    bool InstallerWiringComplete = false;
    bool SampleOpenSmokeRequired = true;
};

struct AIChatStarterPackManifest
{
    OUString ManifestId;
    OUString SchemaVersion;
    OUString CreatedAt;
    OUString PackName;
    AIChatStarterPackCoverage Coverage;
    AIChatStarterPackInstallation Installation;
    std::vector<AIChatStarterPackTemplate> Templates;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    AIChatStarterPackGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString OnboardingDemoRef;
    OUString ManifestHashReference;
};

struct AIChatStarterPackSmoke
{
    OUString SmokeId;
    OUString ManifestId;
    OUString TemplateId;
    OUString Surface;
    OUString Scenario;
    OUString OpenTargetRef;
    bool SampleOpenSucceeded = false;
    bool PatchSmokeSucceeded = false;
    bool UndoSucceeded = false;
    bool EvidenceRequired = true;
    OUString EvidenceId;
    OUString AuditLogRef;
    bool StoresDocumentContent = false;
    bool PublicEgress = false;
};

struct AIChatStarterPackResult
{
    bool Success = false;
    AIChatStarterPackManifest Manifest;
    AIChatStarterPackSmoke Smoke;
    OUString Message;
};

class AIChatStarterPackRuntime final
{
public:
    AIChatStarterPackRuntime();

    const OUString& GetStarterPackStoreUrl() const { return m_sStarterPackStoreUrl; }

    AIChatStarterPackResult RegisterManifest(const AIChatTenantContext& rContext,
                                             const AIChatTenantActionScope& rScope,
                                             const AIChatStarterPackManifest& rManifest) const;
    AIChatStarterPackResult RecordTemplateInstall(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatStarterPackManifest& rManifest,
        const AIChatStarterPackTemplate& rTemplate, const OUString& rEvidenceId) const;
    AIChatStarterPackResult RecordSampleOpenSmoke(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatStarterPackManifest& rManifest, const AIChatStarterPackSmoke& rSmoke) const;

    static bool IsManifestIdAllowed(const OUString& rManifestId);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsPackNameAllowed(const OUString& rPackName);
    static bool IsDistributionAllowed(const OUString& rDistribution);
    static bool IsTemplateRootAllowed(const OUString& rTemplateRoot);
    static bool IsLocaleStrategyAllowed(const OUString& rLocaleStrategy);
    static bool IsSurfaceAllowed(const OUString& rSurface);
    static bool IsScenarioAllowed(const OUString& rScenario);
    static bool IsTemplateIdAllowed(const OUString& rTemplateId, const OUString& rSurface);
    static bool IsTemplatePathAllowed(const OUString& rPath, const OUString& rSurface);
    static bool IsTitleKeyAllowed(const OUString& rTitleKey);
    static bool IsLocaleAllowed(const OUString& rLocale);
    static bool IsActionKindAllowedForSurface(const OUString& rSurface,
                                              const OUString& rActionKind);
    static bool IsRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsOpenTargetRefAllowed(const OUString& rOpenTargetRef);
    static bool IsSamplePatchShapeAllowed(const OUString& rSurface,
                                          const AIChatStarterPackSamplePatch& rSamplePatch);
    static bool IsDataBoundaryAllowed(const AIChatStarterPackDataBoundary& rBoundary);
    static bool IsTemplateShapeAllowed(const AIChatStarterPackTemplate& rTemplate);
    static bool IsCoverageShapeAllowed(const AIChatStarterPackCoverage& rCoverage,
                                       const std::vector<AIChatStarterPackTemplate>& rTemplates);
    static bool IsInstallationShapeAllowed(const AIChatStarterPackInstallation& rInstallation);
    static bool IsGateShapeAllowed(const AIChatStarterPackGateState& rGates);
    static bool IsManifestShapeAllowed(const AIChatStarterPackManifest& rManifest);
    static bool IsSmokeShapeAllowed(const AIChatStarterPackSmoke& rSmoke);
    static OUString MakeManifestId(const OUString& rCreatedAt);
    static OUString MakeTemplateHashReference(const AIChatStarterPackTemplate& rTemplate);
    static OUString MakeManifestHashReference(const AIChatStarterPackManifest& rManifest);
    static OUString MakeSmokeId(const AIChatStarterPackManifest& rManifest,
                                const AIChatStarterPackTemplate& rTemplate,
                                const OUString& rEvidenceId);

private:
    OUString m_sStorageRootUrl;
    OUString m_sStarterPackStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
