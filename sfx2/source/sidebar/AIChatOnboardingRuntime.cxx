/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: first-run onboarding runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatOnboardingRuntime.hxx"

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
constexpr OUStringLiteral ONBOARDING_DIR_NAME = u"kqoffice-v3-ai-onboarding";
constexpr OUStringLiteral ONBOARDING_FILE_NAME = u"onboarding.tsv";

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
    if (rEvidenceIds.empty())
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
    for (const OUString& rEvidence : rRequired)
    {
        if (!AIChatOnboardingRuntime::IsRequiredEvidenceAllowed(rEvidence))
            return false;
    }
    return true;
}

sal_Int32 TotalStepSeconds(const std::vector<AIChatOnboardingStep>& rSteps)
{
    sal_Int32 nTotal = 0;
    for (const AIChatOnboardingStep& rStep : rSteps)
        nTotal += rStep.MaxSeconds;
    return nTotal;
}

bool StepOrderMatchesContract(const std::vector<AIChatOnboardingStep>& rSteps)
{
    static const OUString EXPECTED_KINDS[] = {
        u"welcome"_ustr, u"local-model"_ustr, u"connector"_ustr, u"privacy"_ustr,
        u"demo-patch"_ustr
    };
    if (rSteps.size() != 5)
        return false;
    for (size_t i = 0; i < rSteps.size(); ++i)
    {
        if (rSteps[i].Order != static_cast<sal_Int32>(i + 1)
            || rSteps[i].Kind != EXPECTED_KINDS[i])
            return false;
    }
    return true;
}

bool ScopeAllowed(const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope)
{
    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    return aScopeResult.Success
           && (rScope.TargetType == u"chat"_ustr || rScope.TargetType == u"provider"_ustr)
           && rScope.Surface == u"chat"_ustr;
}

AIChatOnboardingResult MakeDeniedResult(const OUString& rReason)
{
    AIChatOnboardingResult aResult;
    aResult.Message
        = u"onboarding-denied reason="_ustr + rReason
          + u" fail-closed-user-visible=true metadata-only=true"_ustr
          + u" local-first=true noSilentUpload=true publicEgress=false"_ustr
          + u" storesDocumentContent=false hidden-model-download=false connector-writeback=false"_ustr
          + u" demoPatchApplyBeforeApproval=false webview=false standalone-wizard=false"_ustr;
    return aResult;
}
}

AIChatOnboardingRuntime::AIChatOnboardingRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + ONBOARDING_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sOnboardingStoreUrl = m_sStorageRootUrl + u"/"_ustr + ONBOARDING_FILE_NAME;
}

bool AIChatOnboardingRuntime::IsFlowIdAllowed(const OUString& rFlowId)
{
    return rFlowId.startsWith(u"onb-"_ustr) && IsLowerHex(rFlowId.copy(4), 16);
}

bool AIChatOnboardingRuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return AIChatAuditLogRuntime::IsTimestampAllowed(rTimestamp);
}

bool AIChatOnboardingRuntime::IsLocaleAllowed(const OUString& rLocale)
{
    return rLocale == u"zh-CN"_ustr || rLocale == u"en-US"_ustr
           || rLocale == u"ja-JP"_ustr || rLocale == u"zh-TW"_ustr;
}

bool AIChatOnboardingRuntime::IsEditionAllowed(const OUString& rEdition)
{
    return rEdition == u"personal-free"_ustr || rEdition == u"personal-pro"_ustr
           || rEdition == u"enterprise"_ustr || rEdition == u"enterprise-self-hosted"_ustr;
}

bool AIChatOnboardingRuntime::IsStepKindAllowed(const OUString& rKind)
{
    return rKind == u"welcome"_ustr || rKind == u"local-model"_ustr
           || rKind == u"connector"_ustr || rKind == u"privacy"_ustr
           || rKind == u"demo-patch"_ustr;
}

bool AIChatOnboardingRuntime::IsStepStateAllowed(const OUString& rState)
{
    return rState == u"pending"_ustr || rState == u"active"_ustr || rState == u"completed"_ustr
           || rState == u"skipped"_ustr || rState == u"resume-ready"_ustr;
}

bool AIChatOnboardingRuntime::IsLocalModelModeAllowed(const OUString& rMode)
{
    return rMode == u"select-or-skip"_ustr || rMode == u"preconfigured"_ustr;
}

bool AIChatOnboardingRuntime::IsLocalModelProviderAllowed(const OUString& rProvider)
{
    return rProvider == u"ollama-local"_ustr || rProvider == u"enterprise-local"_ustr
           || rProvider == u"none"_ustr;
}

bool AIChatOnboardingRuntime::IsDefaultModelAllowed(const OUString& rModel)
{
    return rModel == u"llama3.2:3b"_ustr || rModel == u"qwen3:0.6b"_ustr
           || rModel == u"enterprise-local"_ustr || rModel == u"none"_ustr;
}

bool AIChatOnboardingRuntime::IsConnectorKindAllowed(const OUString& rKind)
{
    return rKind == u"local-fs"_ustr || rKind == u"feishu-docs"_ustr
           || rKind == u"wechat-work-docs"_ustr || rKind == u"notion"_ustr
           || rKind == u"sharepoint"_ustr || rKind == u"confluence"_ustr;
}

bool AIChatOnboardingRuntime::IsSampleDocumentAllowed(const OUString& rSampleDocument)
{
    return rSampleDocument == u"starter-writer-brief"_ustr
           || rSampleDocument == u"starter-calc-budget"_ustr
           || rSampleDocument == u"starter-impress-review"_ustr;
}

bool AIChatOnboardingRuntime::IsDemoSurfaceAllowed(const OUString& rSurface)
{
    return rSurface == u"writer"_ustr || rSurface == u"calc"_ustr || rSurface == u"impress"_ustr;
}

bool AIChatOnboardingRuntime::IsRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"onboarding-flow"_ustr || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"local-model-choice"_ustr
           || rEvidence == u"privacy-confirmation"_ustr
           || rEvidence == u"demo-patch-result"_ustr
           || rEvidence == u"connector-choice"_ustr;
}

bool AIChatOnboardingRuntime::IsStepShapeAllowed(const AIChatOnboardingStep& rStep)
{
    if (rStep.Order < 1 || rStep.Order > 5 || !IsStepKindAllowed(rStep.Kind)
        || !rStep.TitleKey.startsWith(u"onboarding."_ustr) || rStep.MaxSeconds < 10
        || rStep.MaxSeconds > 180 || !rStep.EvidenceRequired || !IsStepStateAllowed(rStep.State)
        || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rStep.EvidenceId))
        return false;
    if (rStep.Kind == u"connector"_ustr)
        return !rStep.Required;
    return rStep.Required;
}

bool AIChatOnboardingRuntime::IsPrivacyShapeAllowed(const AIChatOnboardingPrivacy& rPrivacy)
{
    return rPrivacy.NoSilentUpload && rPrivacy.LocalFirst && rPrivacy.ExplicitCloudOptIn
           && !rPrivacy.StoresDocumentContent && rPrivacy.Acknowledged
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rPrivacy.EvidenceId);
}

bool AIChatOnboardingRuntime::IsLocalModelShapeAllowed(
    const AIChatOnboardingLocalModel& rLocalModel)
{
    return IsLocalModelModeAllowed(rLocalModel.Mode)
           && IsLocalModelProviderAllowed(rLocalModel.Provider)
           && IsDefaultModelAllowed(rLocalModel.DefaultModel) && rLocalModel.CanSkip
           && rLocalModel.OfflineCapable && !rLocalModel.ExplicitDownloadApproved
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rLocalModel.EvidenceId);
}

bool AIChatOnboardingRuntime::IsConnectorShapeAllowed(const AIChatOnboardingConnector& rConnector)
{
    if (rConnector.Required || rConnector.MaxInitialConnectors < 0
        || rConnector.MaxInitialConnectors > 1 || rConnector.OptionalKinds.empty()
        || !rConnector.RequiresEvidence
        || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rConnector.EvidenceId))
        return false;
    for (const OUString& rKind : rConnector.OptionalKinds)
    {
        if (!IsConnectorKindAllowed(rKind))
            return false;
    }
    if (!rConnector.SelectedKind.isEmpty())
    {
        if (!ContainsString(rConnector.OptionalKinds, rConnector.SelectedKind)
            || !rConnector.UserOptIn)
            return false;
    }
    return true;
}

bool AIChatOnboardingRuntime::IsDemoPatchShapeAllowed(
    const AIChatOnboardingDemoPatch& rDemoPatch)
{
    if (!IsSampleDocumentAllowed(rDemoPatch.SampleDocument) || rDemoPatch.Surfaces.empty()
        || !rDemoPatch.MustSucceed || !rDemoPatch.RequiresUndo || !rDemoPatch.ResultEvidence
        || rDemoPatch.ApplyPlanRef.isEmpty() || !rDemoPatch.ApplyPlanRef.startsWith(u"aprt-"_ustr)
        || !rDemoPatch.DiffReviewRef.startsWith(u"diff-review:"_ustr)
        || !rDemoPatch.ApprovalRef.startsWith(u"approval:"_ustr)
        || !rDemoPatch.ExplicitApprovalRequired || rDemoPatch.PatchApplied
        || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rDemoPatch.EvidenceId))
        return false;
    for (const OUString& rSurface : rDemoPatch.Surfaces)
    {
        if (!IsDemoSurfaceAllowed(rSurface))
            return false;
    }
    return true;
}

bool AIChatOnboardingRuntime::IsGateShapeAllowed(const AIChatOnboardingGateState& rGates)
{
    return rGates.BlocksGA && rGates.RequiresV2RegressionGreen
           && rGates.RuntimeImplementation == u"metadata-runtime-active"_ustr
           && rGates.RecoverySupported && rGates.CanSkipAndResume;
}

bool AIChatOnboardingRuntime::IsFlowShapeAllowed(const AIChatOnboardingFlow& rFlow)
{
    if (!IsFlowIdAllowed(rFlow.FlowId) || rFlow.SchemaVersion != u"v3-onboarding-flow/0.1"_ustr
        || !IsTimestampAllowed(rFlow.CreatedAt) || !IsLocaleAllowed(rFlow.Locale)
        || !IsEditionAllowed(rFlow.Edition) || rFlow.MaxMinutes != 5
        || rFlow.ExpectedMinutes < 1 || rFlow.ExpectedMinutes > 5 || !rFlow.FromDownloadToPatch
        || !StepOrderMatchesContract(rFlow.Steps) || TotalStepSeconds(rFlow.Steps) > 300
        || !IsPrivacyShapeAllowed(rFlow.Privacy)
        || !IsLocalModelShapeAllowed(rFlow.LocalModel)
        || !IsConnectorShapeAllowed(rFlow.Connector)
        || !IsDemoPatchShapeAllowed(rFlow.DemoPatch)
        || !ContainsString(rFlow.RequiredEvidence, u"onboarding-flow"_ustr)
        || !ContainsString(rFlow.RequiredEvidence, u"evidence-record"_ustr)
        || !ContainsString(rFlow.RequiredEvidence, u"local-model-choice"_ustr)
        || !ContainsString(rFlow.RequiredEvidence, u"privacy-confirmation"_ustr)
        || !ContainsString(rFlow.RequiredEvidence, u"demo-patch-result"_ustr)
        || !HasOnlyAllowedEvidence(rFlow.RequiredEvidence) || !HasValidEvidenceIds(rFlow.EvidenceIds)
        || !IsGateShapeAllowed(rFlow.Gates)
        || !rFlow.TenantContextRef.startsWith(u"tenant-context:"_ustr)
        || !rFlow.PolicyContextRef.startsWith(u"policy-context:"_ustr)
        || !rFlow.AuditChainRef.startsWith(u"audit-chain:"_ustr)
        || !rFlow.ResumeStateRef.startsWith(u"onboarding-resume:"_ustr))
        return false;

    for (const AIChatOnboardingStep& rStep : rFlow.Steps)
    {
        if (!IsStepShapeAllowed(rStep))
            return false;
    }
    return true;
}

OUString AIChatOnboardingRuntime::MakeFlowId(const OUString& rLocale, const OUString& rEdition,
                                             const OUString& rCreatedAt)
{
    return u"onb-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rLocale + u":"_ustr + rEdition
                                                         + u":onboarding:"_ustr + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatOnboardingRuntime::MakeResumeStateRef(const AIChatOnboardingFlow& rFlow)
{
    return u"onboarding-resume:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rFlow.FlowId + u":"_ustr
                                                         + rFlow.Locale + u":"_ustr
                                                         + rFlow.Edition)
                 .copy(0, 16);
}

AIChatOnboardingResult AIChatOnboardingRuntime::SaveFlow(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatOnboardingFlow& rFlow) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatOnboardingFlow aFlow = rFlow;
    if (aFlow.FlowId.isEmpty())
        aFlow.FlowId = MakeFlowId(aFlow.Locale, aFlow.Edition, aFlow.CreatedAt);
    aFlow.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aFlow.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aFlow.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aFlow.ResumeStateRef.isEmpty())
        aFlow.ResumeStateRef = MakeResumeStateRef(aFlow);

    if (!IsFlowShapeAllowed(aFlow))
        return MakeDeniedResult(u"invalid-onboarding-flow-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"onboarding-flow"_ustr,
        { aFlow.FlowId, aFlow.SchemaVersion, aFlow.CreatedAt, aFlow.Locale, aFlow.Edition,
          OUString::number(aFlow.MaxMinutes), OUString::number(aFlow.ExpectedMinutes),
          JoinFields(aFlow.RequiredEvidence), JoinFields(aFlow.EvidenceIds),
          aFlow.Privacy.EvidenceId, aFlow.LocalModel.EvidenceId, aFlow.Connector.EvidenceId,
          aFlow.DemoPatch.EvidenceId, aFlow.DemoPatch.ApplyPlanRef, aFlow.DemoPatch.DiffReviewRef,
          aFlow.DemoPatch.ApprovalRef, aFlow.TenantContextRef, aFlow.PolicyContextRef,
          aFlow.AuditChainRef, aFlow.ResumeStateRef });
    if (!AppendUtf8Line(m_sOnboardingStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatOnboardingResult aResult;
    aResult.Success = true;
    aResult.Flow = aFlow;
    aResult.Message
        = u"onboarding-flow-saved flow-id="_ustr + aFlow.FlowId
          + u" locale="_ustr + aFlow.Locale + u" edition="_ustr + aFlow.Edition
          + u" five-steps=true maxMinutes=5 fromDownloadToPatch=true"_ustr
          + u" noSilentUpload=true localFirst=true explicitCloudOptIn=true"_ustr
          + u" storesDocumentContent=false cloud-history=false publicEgress=false"_ustr
          + u" hidden-model-download=false local-model-can-skip=true offlineCapable=true"_ustr
          + u" connector-required=false maxInitialConnectors="_ustr
          + OUString::number(aFlow.Connector.MaxInitialConnectors)
          + u" connector-writeback=false connector-evidence=true"_ustr
          + u" demoPatchMustSucceed=true requiresUndo=true resultEvidence=true"_ustr
          + u" demoPatchApplyBeforeApproval=false apply-plan-ref="_ustr
          + aFlow.DemoPatch.ApplyPlanRef + u" diff-review-ref="_ustr + aFlow.DemoPatch.DiffReviewRef
          + u" approval-ref="_ustr + aFlow.DemoPatch.ApprovalRef
          + u" resume-state-ref="_ustr + aFlow.ResumeStateRef
          + u" native-sfx2-metadata=true webview=false standalone-wizard=false"_ustr
          + u" onboarding-controller-runtime=not-started model-downloader-runtime=not-started"_ustr
          + u" cloud-account-login=not-started installer-updater=not-started"_ustr;
    return aResult;
}

AIChatOnboardingResult AIChatOnboardingRuntime::MarkStepComplete(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatOnboardingFlow& rFlow, const OUString& rStepKind,
    const OUString& rEvidenceId) const
{
    AIChatOnboardingResult aBase = SaveFlow(rContext, rScope, rFlow);
    if (!aBase.Success)
        return aBase;
    if (!IsStepKindAllowed(rStepKind) || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rEvidenceId))
        return MakeDeniedResult(u"invalid-step-completion-evidence"_ustr);

    if (rStepKind == u"privacy"_ustr && !aBase.Flow.Privacy.Acknowledged)
        return MakeDeniedResult(u"privacy-not-acknowledged"_ustr);
    if (rStepKind == u"demo-patch"_ustr
        && (aBase.Flow.DemoPatch.PatchApplied || !aBase.Flow.DemoPatch.ExplicitApprovalRequired))
        return MakeDeniedResult(u"demo-patch-approval-missing"_ustr);

    const OUString sLine = MakeStoreLine(
        u"onboarding-step-complete"_ustr,
        { aBase.Flow.FlowId, rStepKind, rEvidenceId, aBase.Flow.ResumeStateRef,
          u"completed"_ustr });
    if (!AppendUtf8Line(m_sOnboardingStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.Message
        = u"onboarding-step-completed flow-id="_ustr + aBase.Flow.FlowId
          + u" step-kind="_ustr + rStepKind + u" evidence-id="_ustr + rEvidenceId
          + u" metadata-only=true resume-state-ref="_ustr + aBase.Flow.ResumeStateRef
          + u" noSilentUpload=true storesDocumentContent=false"_ustr;
    return aBase;
}

AIChatOnboardingResult AIChatOnboardingRuntime::RecordSkipOrResume(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatOnboardingFlow& rFlow, const OUString& rState, const OUString& rEvidenceId) const
{
    AIChatOnboardingResult aBase = SaveFlow(rContext, rScope, rFlow);
    if (!aBase.Success)
        return aBase;
    if ((rState != u"skipped"_ustr && rState != u"resume-ready"_ustr)
        || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rEvidenceId))
        return MakeDeniedResult(u"invalid-skip-resume-state"_ustr);

    const OUString sLine = MakeStoreLine(
        u"onboarding-skip-resume"_ustr,
        { aBase.Flow.FlowId, rState, rEvidenceId, aBase.Flow.ResumeStateRef });
    if (!AppendUtf8Line(m_sOnboardingStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.Message
        = u"onboarding-skip-resume-recorded flow-id="_ustr + aBase.Flow.FlowId
          + u" state="_ustr + rState + u" evidence-id="_ustr + rEvidenceId
          + u" canSkipAndResume=true recoverySupported=true metadata-only=true"_ustr;
    return aBase;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
