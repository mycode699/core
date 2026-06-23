/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: edition policy runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatEditionPolicyRuntime.hxx"

#include "AIChatAuditLogRuntime.hxx"
#include "AIChatKnowledgeIndexStore.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <unotools/pathoptions.hxx>

#include <algorithm>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral EDITION_POLICY_DIR_NAME = u"kqoffice-v3-ai-edition-policy";
constexpr OUStringLiteral EDITION_POLICY_FILE_NAME = u"edition-policy.tsv";

OUString EnsureNoTrailingSlash(OUString sUrl)
{
    while (sUrl.endsWith(u"/"))
        sUrl = sUrl.copy(0, sUrl.getLength() - 1);
    return sUrl;
}

OUString EscapeField(const OUString& rValue)
{
    OUStringBuffer aBuffer;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        switch (c)
        {
            case '\\':
                aBuffer.append(u"\\\\"_ustr);
                break;
            case '\n':
                aBuffer.append(u"\\n"_ustr);
                break;
            case '\r':
                aBuffer.append(u"\\r"_ustr);
                break;
            case '\t':
                aBuffer.append(u"\\t"_ustr);
                break;
            default:
                aBuffer.append(c);
                break;
        }
    }
    return aBuffer.makeStringAndClear();
}

bool IsLowerHex(const OUString& rValue, sal_Int32 nLength)
{
    if (rValue.getLength() != nLength)
        return false;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    }
    return true;
}

bool ContainsString(const std::vector<OUString>& rValues, const OUString& rValue)
{
    return std::find(rValues.begin(), rValues.end(), rValue) != rValues.end();
}

bool ContainsDuplicateString(const std::vector<OUString>& rValues)
{
    for (size_t i = 0; i < rValues.size(); ++i)
    {
        for (size_t j = i + 1; j < rValues.size(); ++j)
        {
            if (rValues[i] == rValues[j])
                return true;
        }
    }
    return false;
}

OUString JoinFields(const std::vector<OUString>& rValues)
{
    OUStringBuffer aBuffer;
    for (size_t i = 0; i < rValues.size(); ++i)
    {
        if (i > 0)
            aBuffer.append(u","_ustr);
        aBuffer.append(rValues[i]);
    }
    return aBuffer.makeStringAndClear();
}

bool AppendUtf8Line(const OUString& rUrl, const OUString& rLine)
{
    const OString sUtf8 = OUStringToOString(rLine, RTL_TEXTENCODING_UTF8);
    osl::File aFile(rUrl);
    osl::FileBase::RC eError = aFile.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (eError != osl::FileBase::E_None)
        eError = aFile.open(osl_File_OpenFlag_Write);
    if (eError != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    aFile.getSize(nSize);
    aFile.setPos(osl_Pos_Absolut, nSize);

    sal_uInt64 nWritten = 0;
    const bool bWritten
        = aFile.write(sUtf8.getStr(), sUtf8.getLength(), nWritten) == osl::FileBase::E_None
          && nWritten == static_cast<sal_uInt64>(sUtf8.getLength());
    aFile.close();
    return bWritten;
}

OUString MakeStoreLine(const OUString& rKind, const std::vector<OUString>& rFields)
{
    OUStringBuffer aBuffer;
    aBuffer.append(EscapeField(rKind));
    for (const OUString& rField : rFields)
    {
        aBuffer.append(u"\t"_ustr);
        aBuffer.append(EscapeField(rField));
    }
    aBuffer.append(u"\n"_ustr);
    return aBuffer.makeStringAndClear();
}

bool HasValidEvidenceIds(const std::vector<OUString>& rEvidenceIds)
{
    if (rEvidenceIds.empty() || ContainsDuplicateString(rEvidenceIds))
        return false;
    for (const OUString& rEvidenceId : rEvidenceIds)
    {
        if (!AIChatTenantContextRuntime::IsEvidenceIdAllowed(rEvidenceId))
            return false;
    }
    return true;
}

bool HasOnlyAllowedEvidence(const std::vector<OUString>& rRequired)
{
    if (rRequired.empty() || ContainsDuplicateString(rRequired))
        return false;
    for (const OUString& rEvidence : rRequired)
    {
        if (!AIChatEditionPolicyRuntime::IsRequiredEvidenceAllowed(rEvidence))
            return false;
    }
    return true;
}

std::vector<OUString> ExpectedEditionOrder()
{
    return { u"personal-free"_ustr, u"personal-pro"_ustr, u"enterprise"_ustr,
             u"enterprise-self-hosted"_ustr };
}

const AIChatEditionDefinition* FindEdition(const AIChatEditionPolicy& rPolicy,
                                           const OUString& rEditionId)
{
    for (const AIChatEditionDefinition& rEdition : rPolicy.Editions)
    {
        if (rEdition.EditionId == rEditionId)
            return &rEdition;
    }
    return nullptr;
}

bool EditionOrderMatchesContract(const std::vector<AIChatEditionDefinition>& rEditions)
{
    const std::vector<OUString> aOrder = ExpectedEditionOrder();
    if (rEditions.size() != aOrder.size())
        return false;
    for (size_t i = 0; i < rEditions.size(); ++i)
    {
        if (rEditions[i].EditionId != aOrder[i])
            return false;
    }
    return true;
}

bool ScopeAllowed(const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope)
{
    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    return aScopeResult.Success && rScope.TargetType == u"edition-policy"_ustr
           && rScope.Surface == u"edition-policy"_ustr;
}

AIChatEditionPolicyResult MakeDeniedResult(const OUString& rReason)
{
    AIChatEditionPolicyResult aResult;
    aResult.Message
        = u"edition-policy-denied reason="_ustr + rReason
          + u" fail-closed-user-visible=true metadata-only=true local-first=true"_ustr
          + u" freemium=true personal-free-local=true functionLockAllowed=false"_ustr
          + u" featureLocked=false enterpriseAuditMandatory=true auditBypassAllowed=false"_ustr
          + u" storesDocumentContent=false publicEgressDefault=false hidden-cloud-default=false"_ustr
          + u" billing-runtime=not-started license-server-runtime=not-started"_ustr
          + u" account-cloud-login=not-started entitlement-fetch=not-started remote-admin-ui=not-started"_ustr;
    return aResult;
}
}

AIChatEditionPolicyRuntime::AIChatEditionPolicyRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + EDITION_POLICY_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sEditionPolicyStoreUrl = m_sStorageRootUrl + u"/"_ustr + EDITION_POLICY_FILE_NAME;
}

bool AIChatEditionPolicyRuntime::IsPolicyIdAllowed(const OUString& rPolicyId)
{
    return rPolicyId.startsWith(u"edp-"_ustr) && IsLowerHex(rPolicyId.copy(4), 16);
}

bool AIChatEditionPolicyRuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return AIChatAuditLogRuntime::IsTimestampAllowed(rTimestamp);
}

bool AIChatEditionPolicyRuntime::IsEditionIdAllowed(const OUString& rEditionId)
{
    return rEditionId == u"personal-free"_ustr || rEditionId == u"personal-pro"_ustr
           || rEditionId == u"enterprise"_ustr
           || rEditionId == u"enterprise-self-hosted"_ustr;
}

bool AIChatEditionPolicyRuntime::IsCurrencyAllowed(const OUString& rCurrency)
{
    return rCurrency == u"CNY"_ustr;
}

bool AIChatEditionPolicyRuntime::IsPeriodAllowed(const OUString& rPeriod)
{
    return rPeriod == u"free"_ustr || rPeriod == u"monthly"_ustr
           || rPeriod == u"one-time-plus-monthly"_ustr;
}

bool AIChatEditionPolicyRuntime::IsDeploymentModeAllowed(const OUString& rMode)
{
    return rMode == u"desktop-local"_ustr || rMode == u"enterprise-managed"_ustr
           || rMode == u"w8-self-hosted"_ustr;
}

bool AIChatEditionPolicyRuntime::IsServiceModeAllowedForEdition(const OUString& rEditionId,
                                                                const OUString& rServiceMode)
{
    if (rEditionId == u"enterprise-self-hosted"_ustr)
        return rServiceMode == u"private"_ustr;
    return rServiceMode == u"offline"_ustr || rServiceMode == u"private"_ustr;
}

bool AIChatEditionPolicyRuntime::IsRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"edition-policy"_ustr || rEvidence == u"pricing-snapshot"_ustr
           || rEvidence == u"audit-lock"_ustr || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"v2-regression-green"_ustr
           || rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"tenant-context"_ustr || rEvidence == u"starter-pack-manifest"_ustr
           || rEvidence == u"onboarding-flow"_ustr || rEvidence == u"localcloud-no-egress"_ustr;
}

bool AIChatEditionPolicyRuntime::IsPriceShapeAllowed(const OUString& rEditionId,
                                                     const AIChatEditionPrice& rPrice)
{
    if (!IsCurrencyAllowed(rPrice.Currency) || !IsPeriodAllowed(rPrice.Period))
        return false;
    if (rEditionId == u"personal-free"_ustr)
        return rPrice.Amount == 0 && rPrice.Period == u"free"_ustr && !rPrice.SeatBased;
    if (rEditionId == u"personal-pro"_ustr)
        return rPrice.Amount == 39 && rPrice.Period == u"monthly"_ustr && !rPrice.SeatBased;
    if (rEditionId == u"enterprise"_ustr)
        return rPrice.Amount == 199 && rPrice.Period == u"monthly"_ustr && rPrice.SeatBased;
    if (rEditionId == u"enterprise-self-hosted"_ustr)
        return rPrice.Amount == 9999 && rPrice.Period == u"one-time-plus-monthly"_ustr
               && rPrice.SeatBased;
    return false;
}

bool AIChatEditionPolicyRuntime::IsAuditShapeAllowed(const OUString& rEditionId,
                                                     const AIChatEditionAudit& rAudit)
{
    const bool bEnterprise = rEditionId == u"enterprise"_ustr
                             || rEditionId == u"enterprise-self-hosted"_ustr;
    return rAudit.Enabled == bEnterprise && rAudit.RequiredForEdition == bEnterprise
           && !rAudit.BypassAllowed
           && (!bEnterprise || rAudit.AuditLockRef.startsWith(u"audit-lock:"_ustr));
}

bool AIChatEditionPolicyRuntime::IsLimitsShapeAllowed(const OUString& rEditionId,
                                                      const AIChatEditionLimits& rLimits)
{
    if (rEditionId == u"personal-free"_ustr)
    {
        return rLimits.ConnectorMax == 5 && rLimits.KnowledgeIndexDocumentMax == 10000
               && rLimits.AgentConcurrencyMax == 1 && !rLimits.UnlimitedScale;
    }
    if (rEditionId == u"personal-pro"_ustr)
    {
        return rLimits.ConnectorMax == 20 && rLimits.KnowledgeIndexDocumentMax == 50000
               && rLimits.AgentConcurrencyMax == 3 && !rLimits.UnlimitedScale;
    }
    if (rEditionId == u"enterprise"_ustr || rEditionId == u"enterprise-self-hosted"_ustr)
    {
        return rLimits.ConnectorMax == 1000000
               && rLimits.KnowledgeIndexDocumentMax == 1000000000
               && rLimits.AgentConcurrencyMax == 1000000 && rLimits.UnlimitedScale;
    }
    return false;
}

bool AIChatEditionPolicyRuntime::IsFeatureAccessShapeAllowed(
    const AIChatEditionFeatureAccess& rFeatures)
{
    return rFeatures.AIPatch && rFeatures.LocalModel && rFeatures.StarterPack
           && rFeatures.CompanionApproval && !rFeatures.FeatureLocked;
}

bool AIChatEditionPolicyRuntime::IsDeploymentShapeAllowed(
    const OUString& rEditionId, const AIChatEditionDeployment& rDeployment)
{
    if (!IsDeploymentModeAllowed(rDeployment.Mode) || rDeployment.RequiresPublicCloud)
        return false;
    if (rEditionId == u"enterprise-self-hosted"_ustr)
        return rDeployment.Mode == u"w8-self-hosted"_ustr && rDeployment.W8SelfHosted;
    if (rEditionId == u"enterprise"_ustr)
        return rDeployment.Mode == u"enterprise-managed"_ustr && !rDeployment.W8SelfHosted;
    return rDeployment.Mode == u"desktop-local"_ustr && !rDeployment.W8SelfHosted;
}

bool AIChatEditionPolicyRuntime::IsDataBoundaryShapeAllowed(
    const AIChatEditionDataBoundary& rBoundary)
{
    return rBoundary.LocalFirst && !rBoundary.StoresDocumentContent
           && !rBoundary.PublicEgressDefault && !rBoundary.ExplicitPublicEgressOptIn;
}

bool AIChatEditionPolicyRuntime::IsEditionShapeAllowed(
    const AIChatEditionDefinition& rEdition)
{
    return IsEditionIdAllowed(rEdition.EditionId)
           && IsPriceShapeAllowed(rEdition.EditionId, rEdition.Price)
           && IsAuditShapeAllowed(rEdition.EditionId, rEdition.Audit)
           && IsLimitsShapeAllowed(rEdition.EditionId, rEdition.Limits)
           && IsFeatureAccessShapeAllowed(rEdition.FeatureAccess)
           && IsDeploymentShapeAllowed(rEdition.EditionId, rEdition.Deployment)
           && IsDataBoundaryShapeAllowed(rEdition.DataBoundary);
}

bool AIChatEditionPolicyRuntime::IsBusinessModelShapeAllowed(
    const AIChatEditionBusinessModel& rBusinessModel)
{
    return rBusinessModel.Mode == u"freemium"_ustr && rBusinessModel.PersonalFreeLocal
           && rBusinessModel.EnterpriseChargesByAudit && !rBusinessModel.FunctionLockAllowed;
}

bool AIChatEditionPolicyRuntime::IsGuardrailShapeAllowed(
    const AIChatEditionGuardrails& rGuardrails)
{
    return rGuardrails.LimitsOnlyScaleAndAudit && rGuardrails.PersonalEditionFullLocalAI
           && rGuardrails.EnterpriseAuditMandatory && !rGuardrails.TrialBypassAuditAllowed;
}

bool AIChatEditionPolicyRuntime::IsGateShapeAllowed(const AIChatEditionGateState& rGates)
{
    return rGates.BlocksGA && rGates.RequiresV2RegressionGreen
           && rGates.RuntimeImplementation == u"metadata-runtime-active"_ustr
           && !rGates.BillingRuntimeActive && !rGates.LicenseServerActive
           && !rGates.AccountCloudLoginActive && !rGates.EntitlementFetchActive;
}

bool AIChatEditionPolicyRuntime::IsPolicyShapeAllowed(const AIChatEditionPolicy& rPolicy)
{
    if (!IsPolicyIdAllowed(rPolicy.PolicyId)
        || rPolicy.SchemaVersion != u"v3-edition-policy/0.1"_ustr
        || !IsTimestampAllowed(rPolicy.CreatedAt)
        || !IsBusinessModelShapeAllowed(rPolicy.BusinessModel)
        || !EditionOrderMatchesContract(rPolicy.Editions)
        || !IsGuardrailShapeAllowed(rPolicy.Guardrails)
        || !ContainsString(rPolicy.RequiredEvidence, u"edition-policy"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"pricing-snapshot"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"audit-lock"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"evidence-record"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"v2-regression-green"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"policy-decision"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"audit-log-entry"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"tenant-context"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"starter-pack-manifest"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"onboarding-flow"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"localcloud-no-egress"_ustr)
        || !HasOnlyAllowedEvidence(rPolicy.RequiredEvidence)
        || !HasValidEvidenceIds(rPolicy.EvidenceIds) || !IsGateShapeAllowed(rPolicy.Gates)
        || !rPolicy.TenantContextRef.startsWith(u"tenant-context:"_ustr)
        || !rPolicy.PolicyContextRef.startsWith(u"policy-context:"_ustr)
        || !rPolicy.AuditChainRef.startsWith(u"audit-chain:"_ustr)
        || !rPolicy.OnboardingRef.startsWith(u"onboarding-flow:"_ustr)
        || !rPolicy.StarterPackRef.startsWith(u"starter-pack:"_ustr)
        || !rPolicy.NoEgressRef.startsWith(u"localcloud-no-egress:"_ustr)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rPolicy.HashReference))
        return false;

    for (const AIChatEditionDefinition& rEdition : rPolicy.Editions)
    {
        if (!IsEditionShapeAllowed(rEdition))
            return false;
    }
    return true;
}

bool AIChatEditionPolicyRuntime::IsSelectionShapeAllowed(
    const AIChatEditionPolicy& rPolicy, const AIChatEditionSelection& rSelection)
{
    const AIChatEditionDefinition* pEdition = FindEdition(rPolicy, rSelection.EditionId);
    if (!pEdition)
        return false;
    const bool bEnterprise = rSelection.EditionId == u"enterprise"_ustr
                             || rSelection.EditionId == u"enterprise-self-hosted"_ustr;
    return rSelection.SelectionId.startsWith(u"eds-"_ustr)
           && rSelection.PolicyId == rPolicy.PolicyId
           && AIChatTenantContextRuntime::IsUserIdAllowed(rSelection.UserId)
           && AIChatTenantContextRuntime::IsTenantIdAllowed(rSelection.TenantId)
           && AIChatTenantContextRuntime::IsWorkspaceIdAllowed(rSelection.WorkspaceId)
           && IsServiceModeAllowedForEdition(rSelection.EditionId, rSelection.ServiceMode)
           && rSelection.LocalAIDefault && !rSelection.PublicEgress
           && rSelection.EnterpriseGateSatisfied == bEnterprise
           && rSelection.AuditRequired == pEdition->Audit.RequiredForEdition
           && rSelection.AuditEnabled == pEdition->Audit.Enabled
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rSelection.EvidenceId)
           && rSelection.AuditLogRef.startsWith(u"audit-log-entry:"_ustr)
           && rSelection.PolicyDecisionRef.startsWith(u"policy-decision:"_ustr);
}

OUString AIChatEditionPolicyRuntime::MakePolicyId(const OUString& rCreatedAt)
{
    return u"edp-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(u"edition-policy:"_ustr + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatEditionPolicyRuntime::MakePolicyHashReference(
    const AIChatEditionPolicy& rPolicy)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rPolicy.PolicyId + u":"_ustr + rPolicy.CreatedAt + u":freemium:"_ustr
                 + OUString::number(static_cast<sal_Int32>(rPolicy.Editions.size())));
}

OUString AIChatEditionPolicyRuntime::MakeSelectionId(
    const AIChatEditionPolicy& rPolicy, const AIChatEditionSelection& rSelection)
{
    return u"eds-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rPolicy.PolicyId + u":"_ustr + rSelection.EditionId + u":"_ustr
                 + rSelection.UserId + u":"_ustr + rSelection.EvidenceId)
                 .copy(0, 16);
}

AIChatEditionPolicyResult AIChatEditionPolicyRuntime::SavePolicy(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatEditionPolicy& rPolicy) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatEditionPolicy aPolicy = rPolicy;
    if (aPolicy.PolicyId.isEmpty())
        aPolicy.PolicyId = MakePolicyId(aPolicy.CreatedAt);
    aPolicy.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aPolicy.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aPolicy.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aPolicy.OnboardingRef.isEmpty())
        aPolicy.OnboardingRef = u"onboarding-flow:metadata-runtime-active"_ustr;
    if (aPolicy.StarterPackRef.isEmpty())
        aPolicy.StarterPackRef = u"starter-pack:metadata-runtime-active"_ustr;
    if (aPolicy.NoEgressRef.isEmpty())
        aPolicy.NoEgressRef = u"localcloud-no-egress:default-deny"_ustr;
    if (aPolicy.HashReference.isEmpty())
        aPolicy.HashReference = MakePolicyHashReference(aPolicy);

    if (!IsPolicyShapeAllowed(aPolicy))
        return MakeDeniedResult(u"invalid-edition-policy-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"edition-policy"_ustr,
        { aPolicy.PolicyId, aPolicy.SchemaVersion, aPolicy.CreatedAt,
          aPolicy.BusinessModel.Mode,
          OUString::number(static_cast<sal_Int32>(aPolicy.Editions.size())),
          JoinFields(aPolicy.RequiredEvidence), JoinFields(aPolicy.EvidenceIds),
          aPolicy.TenantContextRef, aPolicy.PolicyContextRef, aPolicy.AuditChainRef,
          aPolicy.OnboardingRef, aPolicy.StarterPackRef, aPolicy.NoEgressRef,
          aPolicy.HashReference });
    if (!AppendUtf8Line(m_sEditionPolicyStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatEditionPolicyResult aResult;
    aResult.Success = true;
    aResult.Policy = aPolicy;
    aResult.Message
        = u"edition-policy-saved policy-id="_ustr + aPolicy.PolicyId
          + u" freemium=true personal-free-local=true personal-pro=true enterprise=true"_ustr
          + u" enterprise-self-hosted=true editions=4 functionLockAllowed=false"_ustr
          + u" limitsOnlyScaleAndAudit=true personalEditionFullLocalAI=true"_ustr
          + u" enterpriseAuditMandatory=true trialBypassAuditAllowed=false"_ustr
          + u" localAIDefault=true local-first=true storesDocumentContent=false"_ustr
          + u" publicEgressDefault=false hidden-cloud-default=false"_ustr
          + u" onboarding-ref="_ustr + aPolicy.OnboardingRef
          + u" starter-pack-ref="_ustr + aPolicy.StarterPackRef
          + u" no-egress-ref="_ustr + aPolicy.NoEgressRef
          + u" runtimeImplementation=metadata-runtime-active native-sfx2-metadata=true"_ustr
          + u" billing-runtime=not-started license-server-runtime=not-started"_ustr
          + u" account-cloud-login=not-started entitlement-fetch=not-started"_ustr
          + u" installer-activation=not-started remote-admin-ui=not-started"_ustr;
    return aResult;
}

AIChatEditionPolicyResult AIChatEditionPolicyRuntime::RecordEditionSelection(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatEditionPolicy& rPolicy, const AIChatEditionSelection& rSelection) const
{
    AIChatEditionPolicyResult aBase = SavePolicy(rContext, rScope, rPolicy);
    if (!aBase.Success)
        return aBase;

    AIChatEditionSelection aSelection = rSelection;
    aSelection.PolicyId = aBase.Policy.PolicyId;
    if (aSelection.SelectionId.isEmpty())
        aSelection.SelectionId = MakeSelectionId(aBase.Policy, aSelection);
    if (aSelection.AuditLogRef.isEmpty())
        aSelection.AuditLogRef = u"audit-log-entry:"_ustr + aSelection.EvidenceId;
    if (aSelection.PolicyDecisionRef.isEmpty())
        aSelection.PolicyDecisionRef = u"policy-decision:"_ustr + aSelection.EvidenceId;

    if (!IsSelectionShapeAllowed(aBase.Policy, aSelection))
        return MakeDeniedResult(u"invalid-edition-selection-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"edition-selection"_ustr,
        { aSelection.SelectionId, aSelection.PolicyId, aSelection.EditionId,
          aSelection.UserId, aSelection.TenantId, aSelection.WorkspaceId,
          aSelection.ServiceMode, aSelection.EvidenceId, aSelection.AuditLogRef,
          aSelection.PolicyDecisionRef,
          aSelection.EnterpriseGateSatisfied ? u"enterpriseGateSatisfied=true"_ustr
                                             : u"enterpriseGateSatisfied=false"_ustr,
          aSelection.AuditRequired ? u"auditRequired=true"_ustr : u"auditRequired=false"_ustr });
    if (!AppendUtf8Line(m_sEditionPolicyStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.Selection = aSelection;
    aBase.Message
        = u"edition-selection-recorded selection-id="_ustr + aSelection.SelectionId
          + u" policy-id="_ustr + aSelection.PolicyId + u" edition-id="_ustr
          + aSelection.EditionId + u" service-mode="_ustr + aSelection.ServiceMode
          + u" localAIDefault=true publicEgress=false hidden-cloud-default=false"_ustr
          + u" enterpriseGateSatisfied="_ustr
          + (aSelection.EnterpriseGateSatisfied ? u"true"_ustr : u"false"_ustr)
          + u" auditRequired="_ustr
          + (aSelection.AuditRequired ? u"true"_ustr : u"false"_ustr)
          + u" auditEnabled="_ustr + (aSelection.AuditEnabled ? u"true"_ustr : u"false"_ustr)
          + u" evidence-id="_ustr + aSelection.EvidenceId
          + u" audit-log-ref="_ustr + aSelection.AuditLogRef
          + u" policy-decision-ref="_ustr + aSelection.PolicyDecisionRef
          + u" metadata-only=true storesDocumentContent=false"_ustr;
    return aBase;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
