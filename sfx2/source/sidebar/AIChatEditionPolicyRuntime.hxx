/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: edition policy runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatStarterPackRuntime.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatEditionPrice
{
    OUString Currency;
    sal_Int32 Amount = 0;
    OUString Period;
    bool SeatBased = false;
};

struct AIChatEditionAudit
{
    bool Enabled = false;
    bool RequiredForEdition = false;
    bool BypassAllowed = false;
    OUString AuditLockRef;
};

struct AIChatEditionLimits
{
    sal_Int32 ConnectorMax = 0;
    sal_Int32 KnowledgeIndexDocumentMax = 0;
    sal_Int32 AgentConcurrencyMax = 0;
    bool UnlimitedScale = false;
};

struct AIChatEditionFeatureAccess
{
    bool AIPatch = true;
    bool LocalModel = true;
    bool StarterPack = true;
    bool CompanionApproval = true;
    bool FeatureLocked = false;
};

struct AIChatEditionDeployment
{
    OUString Mode;
    bool W8SelfHosted = false;
    bool RequiresPublicCloud = false;
};

struct AIChatEditionDataBoundary
{
    bool LocalFirst = true;
    bool StoresDocumentContent = false;
    bool PublicEgressDefault = false;
    bool ExplicitPublicEgressOptIn = false;
};

struct AIChatEditionDefinition
{
    OUString EditionId;
    AIChatEditionPrice Price;
    AIChatEditionAudit Audit;
    AIChatEditionLimits Limits;
    AIChatEditionFeatureAccess FeatureAccess;
    AIChatEditionDeployment Deployment;
    AIChatEditionDataBoundary DataBoundary;
};

struct AIChatEditionBusinessModel
{
    OUString Mode;
    bool PersonalFreeLocal = true;
    bool EnterpriseChargesByAudit = true;
    bool FunctionLockAllowed = false;
};

struct AIChatEditionGuardrails
{
    bool LimitsOnlyScaleAndAudit = true;
    bool PersonalEditionFullLocalAI = true;
    bool EnterpriseAuditMandatory = true;
    bool TrialBypassAuditAllowed = false;
};

struct AIChatEditionGateState
{
    bool BlocksGA = true;
    bool RequiresV2RegressionGreen = true;
    OUString RuntimeImplementation;
    bool BillingRuntimeActive = false;
    bool LicenseServerActive = false;
    bool AccountCloudLoginActive = false;
    bool EntitlementFetchActive = false;
};

struct AIChatEditionPolicy
{
    OUString PolicyId;
    OUString SchemaVersion;
    OUString CreatedAt;
    AIChatEditionBusinessModel BusinessModel;
    std::vector<AIChatEditionDefinition> Editions;
    AIChatEditionGuardrails Guardrails;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    AIChatEditionGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString OnboardingRef;
    OUString StarterPackRef;
    OUString NoEgressRef;
    OUString HashReference;
};

struct AIChatEditionSelection
{
    OUString SelectionId;
    OUString PolicyId;
    OUString EditionId;
    OUString UserId;
    OUString TenantId;
    OUString WorkspaceId;
    OUString ServiceMode;
    bool LocalAIDefault = true;
    bool PublicEgress = false;
    bool EnterpriseGateSatisfied = false;
    bool AuditRequired = false;
    bool AuditEnabled = false;
    OUString EvidenceId;
    OUString AuditLogRef;
    OUString PolicyDecisionRef;
};

struct AIChatEditionPolicyResult
{
    bool Success = false;
    AIChatEditionPolicy Policy;
    AIChatEditionSelection Selection;
    OUString Message;
};

class AIChatEditionPolicyRuntime final
{
public:
    AIChatEditionPolicyRuntime();

    const OUString& GetEditionPolicyStoreUrl() const { return m_sEditionPolicyStoreUrl; }

    AIChatEditionPolicyResult SavePolicy(const AIChatTenantContext& rContext,
                                         const AIChatTenantActionScope& rScope,
                                         const AIChatEditionPolicy& rPolicy) const;
    AIChatEditionPolicyResult RecordEditionSelection(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatEditionPolicy& rPolicy, const AIChatEditionSelection& rSelection) const;

    static bool IsPolicyIdAllowed(const OUString& rPolicyId);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsEditionIdAllowed(const OUString& rEditionId);
    static bool IsCurrencyAllowed(const OUString& rCurrency);
    static bool IsPeriodAllowed(const OUString& rPeriod);
    static bool IsDeploymentModeAllowed(const OUString& rMode);
    static bool IsServiceModeAllowedForEdition(const OUString& rEditionId,
                                               const OUString& rServiceMode);
    static bool IsRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsPriceShapeAllowed(const OUString& rEditionId,
                                    const AIChatEditionPrice& rPrice);
    static bool IsAuditShapeAllowed(const OUString& rEditionId,
                                    const AIChatEditionAudit& rAudit);
    static bool IsLimitsShapeAllowed(const OUString& rEditionId,
                                     const AIChatEditionLimits& rLimits);
    static bool IsFeatureAccessShapeAllowed(const AIChatEditionFeatureAccess& rFeatures);
    static bool IsDeploymentShapeAllowed(const OUString& rEditionId,
                                         const AIChatEditionDeployment& rDeployment);
    static bool IsDataBoundaryShapeAllowed(const AIChatEditionDataBoundary& rBoundary);
    static bool IsEditionShapeAllowed(const AIChatEditionDefinition& rEdition);
    static bool IsBusinessModelShapeAllowed(const AIChatEditionBusinessModel& rBusinessModel);
    static bool IsGuardrailShapeAllowed(const AIChatEditionGuardrails& rGuardrails);
    static bool IsGateShapeAllowed(const AIChatEditionGateState& rGates);
    static bool IsPolicyShapeAllowed(const AIChatEditionPolicy& rPolicy);
    static bool IsSelectionShapeAllowed(const AIChatEditionPolicy& rPolicy,
                                        const AIChatEditionSelection& rSelection);
    static OUString MakePolicyId(const OUString& rCreatedAt);
    static OUString MakePolicyHashReference(const AIChatEditionPolicy& rPolicy);
    static OUString MakeSelectionId(const AIChatEditionPolicy& rPolicy,
                                    const AIChatEditionSelection& rSelection);

private:
    OUString m_sStorageRootUrl;
    OUString m_sEditionPolicyStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
