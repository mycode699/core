/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: first-run onboarding runtime).
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

struct AIChatOnboardingStep
{
    sal_Int32 Order = 0;
    OUString Kind;
    OUString TitleKey;
    bool Required = false;
    sal_Int32 MaxSeconds = 0;
    bool EvidenceRequired = true;
    OUString State;
    OUString EvidenceId;
};

struct AIChatOnboardingPrivacy
{
    bool NoSilentUpload = true;
    bool LocalFirst = true;
    bool ExplicitCloudOptIn = true;
    bool StoresDocumentContent = false;
    bool Acknowledged = false;
    OUString EvidenceId;
};

struct AIChatOnboardingLocalModel
{
    OUString Mode;
    OUString Provider;
    bool CanSkip = true;
    OUString DefaultModel;
    bool OfflineCapable = true;
    bool UserSkipped = false;
    bool ExplicitDownloadApproved = false;
    OUString EvidenceId;
};

struct AIChatOnboardingConnector
{
    bool Required = false;
    sal_Int32 MaxInitialConnectors = 1;
    std::vector<OUString> OptionalKinds;
    bool RequiresEvidence = true;
    OUString SelectedKind;
    bool UserOptIn = false;
    OUString EvidenceId;
};

struct AIChatOnboardingDemoPatch
{
    OUString SampleDocument;
    std::vector<OUString> Surfaces;
    bool MustSucceed = true;
    bool RequiresUndo = true;
    bool ResultEvidence = true;
    OUString ApplyPlanRef;
    OUString DiffReviewRef;
    OUString ApprovalRef;
    bool ExplicitApprovalRequired = true;
    bool PatchApplied = false;
    OUString EvidenceId;
};

struct AIChatOnboardingGateState
{
    bool BlocksGA = true;
    bool RequiresV2RegressionGreen = true;
    OUString RuntimeImplementation;
    bool RecoverySupported = true;
    bool CanSkipAndResume = true;
};

struct AIChatOnboardingFlow
{
    OUString FlowId;
    OUString SchemaVersion;
    OUString CreatedAt;
    OUString Locale;
    OUString Edition;
    sal_Int32 MaxMinutes = 5;
    sal_Int32 ExpectedMinutes = 0;
    bool FromDownloadToPatch = true;
    std::vector<AIChatOnboardingStep> Steps;
    AIChatOnboardingPrivacy Privacy;
    AIChatOnboardingLocalModel LocalModel;
    AIChatOnboardingConnector Connector;
    AIChatOnboardingDemoPatch DemoPatch;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    AIChatOnboardingGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString ResumeStateRef;
};

struct AIChatOnboardingResult
{
    bool Success = false;
    AIChatOnboardingFlow Flow;
    OUString Message;
};

class AIChatOnboardingRuntime final
{
public:
    AIChatOnboardingRuntime();

    const OUString& GetOnboardingStoreUrl() const { return m_sOnboardingStoreUrl; }

    AIChatOnboardingResult SaveFlow(const AIChatTenantContext& rContext,
                                    const AIChatTenantActionScope& rScope,
                                    const AIChatOnboardingFlow& rFlow) const;
    AIChatOnboardingResult MarkStepComplete(const AIChatTenantContext& rContext,
                                            const AIChatTenantActionScope& rScope,
                                            const AIChatOnboardingFlow& rFlow,
                                            const OUString& rStepKind,
                                            const OUString& rEvidenceId) const;
    AIChatOnboardingResult RecordSkipOrResume(const AIChatTenantContext& rContext,
                                              const AIChatTenantActionScope& rScope,
                                              const AIChatOnboardingFlow& rFlow,
                                              const OUString& rState,
                                              const OUString& rEvidenceId) const;

    static bool IsFlowIdAllowed(const OUString& rFlowId);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsLocaleAllowed(const OUString& rLocale);
    static bool IsEditionAllowed(const OUString& rEdition);
    static bool IsStepKindAllowed(const OUString& rKind);
    static bool IsStepStateAllowed(const OUString& rState);
    static bool IsLocalModelModeAllowed(const OUString& rMode);
    static bool IsLocalModelProviderAllowed(const OUString& rProvider);
    static bool IsDefaultModelAllowed(const OUString& rModel);
    static bool IsConnectorKindAllowed(const OUString& rKind);
    static bool IsSampleDocumentAllowed(const OUString& rSampleDocument);
    static bool IsDemoSurfaceAllowed(const OUString& rSurface);
    static bool IsRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsStepShapeAllowed(const AIChatOnboardingStep& rStep);
    static bool IsPrivacyShapeAllowed(const AIChatOnboardingPrivacy& rPrivacy);
    static bool IsLocalModelShapeAllowed(const AIChatOnboardingLocalModel& rLocalModel);
    static bool IsConnectorShapeAllowed(const AIChatOnboardingConnector& rConnector);
    static bool IsDemoPatchShapeAllowed(const AIChatOnboardingDemoPatch& rDemoPatch);
    static bool IsGateShapeAllowed(const AIChatOnboardingGateState& rGates);
    static bool IsFlowShapeAllowed(const AIChatOnboardingFlow& rFlow);
    static OUString MakeFlowId(const OUString& rLocale, const OUString& rEdition,
                               const OUString& rCreatedAt);
    static OUString MakeResumeStateRef(const AIChatOnboardingFlow& rFlow);

private:
    OUString m_sStorageRootUrl;
    OUString m_sOnboardingStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

