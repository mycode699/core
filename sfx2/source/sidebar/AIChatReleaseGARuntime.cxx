/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: release GA runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatReleaseGARuntime.hxx"

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
constexpr OUStringLiteral RELEASE_GA_DIR_NAME = u"kqoffice-v3-ai-release-ga";
constexpr OUStringLiteral RELEASE_GA_FILE_NAME = u"release-ga.tsv";

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

std::vector<OUString> ExpectedReleasePlatforms()
{
    return { u"macos"_ustr, u"windows"_ustr, u"linux"_ustr, u"self-hosted"_ustr };
}

std::vector<OUString> ExpectedGateIds()
{
    return { u"v2-regression-green"_ustr,
             u"h8-connector-contract"_ustr,
             u"h9-eval-baseline"_ustr,
             u"h10-localcloud-no-egress"_ustr,
             u"h11-perf-baseline"_ustr,
             u"h12-crash-recovery"_ustr,
             u"w9-onboarding-flow"_ustr,
             u"w9-starter-pack"_ustr,
             u"w9-edition-policy"_ustr,
             u"w9-i18n-locale"_ustr,
             u"w9-manual-docs"_ustr,
             u"w9-distribution-update"_ustr,
             u"w9-error-recovery-ux"_ustr,
             u"source-archive-clean"_ustr,
             u"windows-toast-proof"_ustr,
             u"release-policy-decisions"_ustr };
}

bool RefShapeAllowed(const OUString& rRef, const OUString& rPrefix)
{
    return rRef.startsWith(rPrefix) && rRef.getLength() > rPrefix.getLength();
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

bool HasReleaseBaseEvidence(const std::vector<OUString>& rEvidence)
{
    return ContainsString(rEvidence, u"release-ga-checklist"_ustr)
           && ContainsString(rEvidence, u"v2-regression-green"_ustr)
           && ContainsString(rEvidence, u"v3-self-test-green"_ustr)
           && ContainsString(rEvidence, u"v3-only-green"_ustr)
           && ContainsString(rEvidence, u"human-approval"_ustr)
           && ContainsString(rEvidence, u"evidence-record"_ustr)
           && ContainsString(rEvidence, u"release-signoff"_ustr);
}

bool ReleaseRequiredEvidenceAllowed(const std::vector<OUString>& rEvidence)
{
    if (rEvidence.empty() || ContainsDuplicateString(rEvidence))
        return false;
    for (const OUString& rValue : rEvidence)
    {
        if (!AIChatReleaseGARuntime::IsReleaseRequiredEvidenceAllowed(rValue))
            return false;
    }
    return true;
}

bool EvidenceRefsAllowed(const std::vector<OUString>& rEvidenceRefs)
{
    if (rEvidenceRefs.empty() || ContainsDuplicateString(rEvidenceRefs))
        return false;
    for (const OUString& rEvidenceRef : rEvidenceRefs)
    {
        if (!RefShapeAllowed(rEvidenceRef, u"evidence-record:"_ustr)
            && !RefShapeAllowed(rEvidenceRef, u"test-evidence:"_ustr)
            && !RefShapeAllowed(rEvidenceRef, u"manual-evidence:"_ustr))
            return false;
    }
    return true;
}

bool ScopeAllowed(const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope)
{
    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    return aScopeResult.Success && rScope.TargetType == u"release-ga-checklist"_ustr
           && rScope.Surface == u"release-ga-checklist"_ustr;
}

AIChatReleaseGAResult MakeDeniedResult(const OUString& rReason)
{
    AIChatReleaseGAResult aResult;
    aResult.Message
        = u"release-ga-denied reason="_ustr + rReason
          + u" fail-closed-user-visible=true metadata-only=true"_ustr
          + u" humanApprovalRequired=true automatedApprovalAllowed=false"_ustr
          + u" signoffEvidenceRequired=true canShip=false blocksGA=true"_ustr
          + u" requiresExplicitUserAuthorization=true publicEgress=false"_ustr
          + u" raw-release-payload=false raw-evidence-payload=false raw-signoff-payload=false"_ustr
          + u" storesDocumentContent=false artifact-publishing-runtime=not-started"_ustr
          + u" code-signing-runtime=not-started notarization-submission=not-started"_ustr
          + u" update-channel-publication=not-started release-upload=not-started"_ustr
          + u" telemetry-upload=not-started public-network=not-started"_ustr
          + u" release-publishing=false signing-execution=false upload-execution=false"_ustr;
    return aResult;
}
}

AIChatReleaseGARuntime::AIChatReleaseGARuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + RELEASE_GA_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sReleaseGAStoreUrl = m_sStorageRootUrl + u"/"_ustr + RELEASE_GA_FILE_NAME;
}

bool AIChatReleaseGARuntime::IsChecklistIdAllowed(const OUString& rChecklistId)
{
    return rChecklistId.startsWith(u"relga-"_ustr) && IsLowerHex(rChecklistId.copy(6), 16);
}

bool AIChatReleaseGARuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return AIChatAuditLogRuntime::IsTimestampAllowed(rTimestamp);
}

bool AIChatReleaseGARuntime::IsReleasePlatformAllowed(const OUString& rPlatform)
{
    return ContainsString(ExpectedReleasePlatforms(), rPlatform);
}

bool AIChatReleaseGARuntime::IsReleaseGateIdAllowed(const OUString& rGateId)
{
    return ContainsString(ExpectedGateIds(), rGateId);
}

bool AIChatReleaseGARuntime::IsReleaseGateStatusAllowed(const OUString& rGateId,
                                                        const OUString& rStatus)
{
    if (rGateId == u"source-archive-clean"_ustr
        || rGateId == u"release-policy-decisions"_ustr)
        return rStatus == u"pending-release-decision"_ustr;
    if (rGateId == u"windows-toast-proof"_ustr)
        return rStatus == u"pending-runtime"_ustr;
    return rStatus == u"contract-active"_ustr;
}

bool AIChatReleaseGARuntime::IsReleaseGateOwnerAllowed(const OUString& rOwner)
{
    return rOwner == u"qa-owner"_ustr || rOwner == u"release-owner"_ustr
           || rOwner == u"repo-owner"_ustr || rOwner == u"runtime-owner"_ustr;
}

bool AIChatReleaseGARuntime::IsReleaseGateEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"v2-harness-sweep"_ustr
           || rEvidence == u"tests/v3-connector-manifest-contract-test.sh"_ustr
           || rEvidence == u"tests/v3-eval-baseline-test.sh"_ustr
           || rEvidence == u"tests/v3-local-cloud-no-egress-test.sh"_ustr
           || rEvidence == u"tests/v3-perf-baseline-test.sh"_ustr
           || rEvidence == u"tests/v3-crash-recovery-test.sh"_ustr
           || rEvidence == u"tests/v3-onboarding-flow-test.sh"_ustr
           || rEvidence == u"tests/v3-starter-pack-test.sh"_ustr
           || rEvidence == u"tests/v3-edition-policy-test.sh"_ustr
           || rEvidence == u"tests/v3-i18n-locale-test.sh"_ustr
           || rEvidence == u"tests/v3-manual-docs-test.sh"_ustr
           || rEvidence == u"tests/v3-distribution-update-test.sh"_ustr
           || rEvidence == u"tests/v3-error-recovery-ux-test.sh"_ustr
           || rEvidence == u"source-archive-commits"_ustr
           || rEvidence == u"h10-boundary-clean"_ustr
           || rEvidence == u"windows-host-compile"_ustr
           || rEvidence == u"manual-toast-proof"_ustr || rEvidence == u"d5-branding"_ustr
           || rEvidence == u"d8-team-identifier"_ustr
           || rEvidence == u"b3-push-strategy"_ustr;
}

bool AIChatReleaseGARuntime::IsReleaseRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"release-ga-checklist"_ustr
           || rEvidence == u"v2-regression-green"_ustr
           || rEvidence == u"v3-self-test-green"_ustr
           || rEvidence == u"v3-only-green"_ustr || rEvidence == u"human-approval"_ustr
           || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"release-signoff"_ustr;
}

bool AIChatReleaseGARuntime::IsReleaseApproverAllowed(const OUString& rApprover)
{
    return rApprover == u"repo-owner"_ustr || rApprover == u"release-owner"_ustr
           || rApprover == u"qa-owner"_ustr;
}

bool AIChatReleaseGARuntime::IsReleaseScopeShapeAllowed(
    const AIChatReleaseGAScope& rScope)
{
    return rScope.Product == u"kqoffice-v3"_ustr
           && rScope.ReleasePhase == u"ga-blocking-contract"_ustr
           && rScope.SupportedPlatforms == ExpectedReleasePlatforms()
           && rScope.DefaultDataBoundary == u"local-first"_ustr;
}

bool AIChatReleaseGARuntime::IsReleaseGateRosterAllowed(
    const std::vector<AIChatReleaseGAReadinessGate>& rGates)
{
    const std::vector<OUString> aExpectedGateIds = ExpectedGateIds();
    if (rGates.size() != aExpectedGateIds.size())
        return false;
    for (size_t i = 0; i < rGates.size(); ++i)
    {
        const AIChatReleaseGAReadinessGate& rGate = rGates[i];
        if (rGate.GateId != aExpectedGateIds[i] || rGate.Title.getLength() < 8
            || !IsReleaseGateStatusAllowed(rGate.GateId, rGate.Status)
            || !IsReleaseGateOwnerAllowed(rGate.Owner) || !rGate.BlocksGA
            || rGate.RequiredEvidence.empty()
            || ContainsDuplicateString(rGate.RequiredEvidence))
            return false;
        for (const OUString& rEvidence : rGate.RequiredEvidence)
        {
            if (!IsReleaseGateEvidenceAllowed(rEvidence))
                return false;
        }
    }
    return true;
}

bool AIChatReleaseGARuntime::IsReleaseApprovalsShapeAllowed(
    const AIChatReleaseGAApprovals& rApprovals)
{
    return rApprovals.HumanApprovalRequired && !rApprovals.AutomatedApprovalAllowed
           && rApprovals.Approvers.size() == 3
           && rApprovals.Approvers[0] == u"repo-owner"_ustr
           && rApprovals.Approvers[1] == u"release-owner"_ustr
           && rApprovals.Approvers[2] == u"qa-owner"_ustr
           && !ContainsDuplicateString(rApprovals.Approvers)
           && rApprovals.SignoffEvidenceRequired;
}

bool AIChatReleaseGARuntime::IsReleaseChecklistShapeAllowed(
    const AIChatReleaseGAChecklist& rChecklist)
{
    return IsChecklistIdAllowed(rChecklist.ChecklistId)
           && rChecklist.SchemaVersion == u"v3-release-ga-checklist/0.1"_ustr
           && IsTimestampAllowed(rChecklist.CreatedAt)
           && IsReleaseScopeShapeAllowed(rChecklist.Scope)
           && IsReleaseGateRosterAllowed(rChecklist.ReadinessGates)
           && IsReleaseApprovalsShapeAllowed(rChecklist.Approvals)
           && HasReleaseBaseEvidence(rChecklist.RequiredEvidence)
           && ReleaseRequiredEvidenceAllowed(rChecklist.RequiredEvidence)
           && HasValidEvidenceIds(rChecklist.EvidenceIds) && rChecklist.Gates.BlocksGA
           && !rChecklist.Gates.CanShip && rChecklist.Gates.RequiresExplicitUserAuthorization
           && rChecklist.Gates.RuntimeImplementation == u"not-started"_ustr
           && !rChecklist.Gates.ArtifactPublishingRuntimeActive
           && !rChecklist.Gates.CodeSigningRuntimeActive
           && !rChecklist.Gates.NotarizationSubmissionRuntimeActive
           && !rChecklist.Gates.UpdateChannelPublicationRuntimeActive
           && !rChecklist.Gates.ReleaseUploadRuntimeActive
           && !rChecklist.Gates.ExternalMetricUploadRuntimeActive
           && !rChecklist.Gates.PublicNetworkRuntimeActive
           && RefShapeAllowed(rChecklist.TenantContextRef, u"tenant-context:"_ustr)
           && RefShapeAllowed(rChecklist.PolicyContextRef, u"policy-context:"_ustr)
           && RefShapeAllowed(rChecklist.AuditChainRef, u"audit-chain:"_ustr)
           && RefShapeAllowed(rChecklist.DistributionRecoveryRef,
                              u"distribution-recovery:"_ustr)
           && RefShapeAllowed(rChecklist.PerfCrashRef, u"perf-crash:"_ustr)
           && RefShapeAllowed(rChecklist.NoEgressRef, u"localcloud-no-egress:"_ustr)
           && RefShapeAllowed(rChecklist.V2RegressionRef, u"v2-regression:"_ustr)
           && RefShapeAllowed(rChecklist.V3SelfTestRef, u"v3-self-test:"_ustr)
           && RefShapeAllowed(rChecklist.V3OnlyRef, u"v3-only:"_ustr)
           && RefShapeAllowed(rChecklist.SourceArchiveRef, u"source-archive:"_ustr)
           && RefShapeAllowed(rChecklist.WindowsToastProofRef, u"windows-toast-proof:"_ustr)
           && RefShapeAllowed(rChecklist.ReleasePolicyDecisionRef,
                              u"release-policy-decisions:"_ustr)
           && RefShapeAllowed(rChecklist.ReleaseEvidenceRef, u"release-evidence:"_ustr)
           && AIChatTenantContextRuntime::IsHashReferenceAllowed(rChecklist.HashReference);
}

bool AIChatReleaseGARuntime::IsGateEvidenceRecordShapeAllowed(
    const AIChatReleaseGAChecklist& rChecklist,
    const AIChatReleaseGAGateEvidenceRecord& rRecord)
{
    return rRecord.RecordId.startsWith(u"rge-"_ustr)
           && IsLowerHex(rRecord.RecordId.copy(4), 16)
           && rRecord.ChecklistId == rChecklist.ChecklistId
           && IsReleaseGateIdAllowed(rRecord.GateId)
           && rRecord.GateStatus == u"green"_ustr && rRecord.GateGreen
           && rRecord.BlocksGA && EvidenceRefsAllowed(rRecord.EvidenceRefs)
           && RefShapeAllowed(rRecord.ArtifactRef, u"artifact-ref:"_ustr)
           && RefShapeAllowed(rRecord.SigningRef, u"signing-ref:"_ustr)
           && RefShapeAllowed(rRecord.UpdateChannelRef, u"update-channel:"_ustr)
           && RefShapeAllowed(rRecord.RecoveryProofRef, u"recovery-proof:"_ustr)
           && RefShapeAllowed(rRecord.PolicyDecisionRef, u"policy-decision:"_ustr)
           && !rRecord.PublicEgressRequired && !rRecord.StoresRawEvidence
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rRecord.EvidenceId)
           && RefShapeAllowed(rRecord.AuditLogRef, u"audit-log-entry:"_ustr);
}

bool AIChatReleaseGARuntime::IsReleaseSignoffRecordShapeAllowed(
    const AIChatReleaseGAChecklist& rChecklist,
    const AIChatReleaseGASignoffRecord& rRecord)
{
    return rRecord.SignoffId.startsWith(u"rgs-"_ustr)
           && IsLowerHex(rRecord.SignoffId.copy(4), 16)
           && rRecord.ChecklistId == rChecklist.ChecklistId
           && IsReleaseApproverAllowed(rRecord.ApproverRole)
           && AIChatTenantContextRuntime::IsUserIdAllowed(rRecord.ApproverId)
           && RefShapeAllowed(rRecord.HumanApprovalRef, u"human-approval:"_ustr)
           && RefShapeAllowed(rRecord.ReleaseSignoffRef, u"release-signoff:"_ustr)
           && RefShapeAllowed(rRecord.ArtifactBundleRef, u"artifact-bundle:"_ustr)
           && RefShapeAllowed(rRecord.SigningRef, u"signing-ref:"_ustr)
           && RefShapeAllowed(rRecord.UpdateChannelRef, u"update-channel:"_ustr)
           && RefShapeAllowed(rRecord.RecoveryProofRef, u"recovery-proof:"_ustr)
           && RefShapeAllowed(rRecord.SourceArchiveRef, u"source-archive:"_ustr)
           && RefShapeAllowed(rRecord.WindowsToastProofRef, u"windows-toast-proof:"_ustr)
           && RefShapeAllowed(rRecord.ReleasePolicyDecisionRef,
                              u"release-policy-decisions:"_ustr)
           && RefShapeAllowed(rRecord.PolicyDecisionRef, u"policy-decision:"_ustr)
           && rRecord.HumanApprovalRecorded && !rRecord.AutomatedApproval
           && rRecord.AllBlockingGatesGreen && rRecord.ArtifactsSigned
           && rRecord.UpdateChannelReady && rRecord.RecoveryProofPresent
           && rRecord.RequiresExplicitUserAuthorization && !rRecord.CanShip
           && !rRecord.ReleasePublished
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rRecord.EvidenceId)
           && RefShapeAllowed(rRecord.AuditLogRef, u"audit-log-entry:"_ustr);
}

OUString AIChatReleaseGARuntime::MakeReleaseGAChecklistId(const OUString& rCreatedAt)
{
    return u"relga-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(u"release-ga:"_ustr + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatReleaseGARuntime::MakeReleaseGAChecklistHashReference(
    const AIChatReleaseGAChecklist& rChecklist)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rChecklist.ChecklistId + u":"_ustr + rChecklist.CreatedAt + u":"_ustr
                 + rChecklist.Scope.Product + u":gates=16:canShip=false"_ustr);
}

OUString AIChatReleaseGARuntime::MakeGateEvidenceRecordId(
    const AIChatReleaseGAChecklist& rChecklist,
    const AIChatReleaseGAGateEvidenceRecord& rRecord)
{
    return u"rge-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rChecklist.ChecklistId + u":"_ustr + rRecord.GateId + u":"_ustr
                 + rRecord.EvidenceId)
                 .copy(0, 16);
}

OUString AIChatReleaseGARuntime::MakeReleaseSignoffId(
    const AIChatReleaseGAChecklist& rChecklist,
    const AIChatReleaseGASignoffRecord& rRecord)
{
    return u"rgs-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rChecklist.ChecklistId + u":"_ustr + rRecord.ApproverRole + u":"_ustr
                 + rRecord.EvidenceId)
                 .copy(0, 16);
}

AIChatReleaseGAResult AIChatReleaseGARuntime::SaveReleaseGAChecklist(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatReleaseGAChecklist& rChecklist) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatReleaseGAChecklist aChecklist = rChecklist;
    if (aChecklist.ChecklistId.isEmpty())
        aChecklist.ChecklistId = MakeReleaseGAChecklistId(aChecklist.CreatedAt);
    aChecklist.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aChecklist.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aChecklist.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aChecklist.DistributionRecoveryRef.isEmpty())
        aChecklist.DistributionRecoveryRef
            = u"distribution-recovery:metadata-runtime-active"_ustr;
    if (aChecklist.PerfCrashRef.isEmpty())
        aChecklist.PerfCrashRef = u"perf-crash:metadata-runtime-active"_ustr;
    if (aChecklist.NoEgressRef.isEmpty())
        aChecklist.NoEgressRef = u"localcloud-no-egress:default-deny"_ustr;
    if (aChecklist.V2RegressionRef.isEmpty())
        aChecklist.V2RegressionRef = u"v2-regression:harness-sweep-green-required"_ustr;
    if (aChecklist.V3SelfTestRef.isEmpty())
        aChecklist.V3SelfTestRef = u"v3-self-test:self-test-green-required"_ustr;
    if (aChecklist.V3OnlyRef.isEmpty())
        aChecklist.V3OnlyRef = u"v3-only:contract-gates-green-required"_ustr;
    if (aChecklist.SourceArchiveRef.isEmpty())
        aChecklist.SourceArchiveRef = u"source-archive:pending-release-decision"_ustr;
    if (aChecklist.WindowsToastProofRef.isEmpty())
        aChecklist.WindowsToastProofRef = u"windows-toast-proof:pending-runtime"_ustr;
    if (aChecklist.ReleasePolicyDecisionRef.isEmpty())
        aChecklist.ReleasePolicyDecisionRef
            = u"release-policy-decisions:pending-release-decision"_ustr;
    if (aChecklist.ReleaseEvidenceRef.isEmpty())
        aChecklist.ReleaseEvidenceRef = u"release-evidence:ga-blocking-checklist"_ustr;
    if (aChecklist.HashReference.isEmpty())
        aChecklist.HashReference = MakeReleaseGAChecklistHashReference(aChecklist);

    if (!IsReleaseChecklistShapeAllowed(aChecklist))
        return MakeDeniedResult(u"invalid-release-ga-checklist-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"release-ga-checklist"_ustr,
        { aChecklist.ChecklistId, aChecklist.SchemaVersion, aChecklist.CreatedAt,
          aChecklist.Scope.Product, aChecklist.Scope.ReleasePhase,
          JoinFields(aChecklist.Scope.SupportedPlatforms),
          aChecklist.Scope.DefaultDataBoundary,
          OUString::number(static_cast<sal_Int32>(aChecklist.ReadinessGates.size())),
          JoinFields(aChecklist.RequiredEvidence), JoinFields(aChecklist.EvidenceIds),
          u"humanApprovalRequired=true"_ustr, u"automatedApprovalAllowed=false"_ustr,
          JoinFields(aChecklist.Approvals.Approvers), u"signoffEvidenceRequired=true"_ustr,
          u"blocksGA=true"_ustr, u"canShip=false"_ustr,
          u"requiresExplicitUserAuthorization=true"_ustr, aChecklist.Gates.RuntimeImplementation,
          aChecklist.TenantContextRef, aChecklist.PolicyContextRef,
          aChecklist.AuditChainRef, aChecklist.DistributionRecoveryRef,
          aChecklist.PerfCrashRef, aChecklist.NoEgressRef, aChecklist.V2RegressionRef,
          aChecklist.V3SelfTestRef, aChecklist.V3OnlyRef, aChecklist.SourceArchiveRef,
          aChecklist.WindowsToastProofRef, aChecklist.ReleasePolicyDecisionRef,
          aChecklist.ReleaseEvidenceRef, aChecklist.HashReference });
    if (!AppendUtf8Line(m_sReleaseGAStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatReleaseGAResult aResult;
    aResult.Success = true;
    aResult.Checklist = aChecklist;
    aResult.Message
        = u"release-ga-checklist-saved checklist-id="_ustr + aChecklist.ChecklistId
          + u" product=kqoffice-v3 releasePhase=ga-blocking-contract gates=16"_ustr
          + u" platforms=macos,windows,linux,self-hosted defaultDataBoundary=local-first"_ustr
          + u" humanApprovalRequired=true automatedApprovalAllowed=false"_ustr
          + u" approvers=repo-owner,release-owner,qa-owner signoffEvidenceRequired=true"_ustr
          + u" blocksGA=true canShip=false requiresExplicitUserAuthorization=true"_ustr
          + u" runtimeImplementation=not-started distribution-recovery-ref="_ustr
          + aChecklist.DistributionRecoveryRef + u" perf-crash-ref="_ustr
          + aChecklist.PerfCrashRef + u" no-egress-ref="_ustr + aChecklist.NoEgressRef
          + u" source-archive-ref="_ustr + aChecklist.SourceArchiveRef
          + u" windows-toast-proof-ref="_ustr + aChecklist.WindowsToastProofRef
          + u" release-policy-decision-ref="_ustr + aChecklist.ReleasePolicyDecisionRef
          + u" release-evidence-ref="_ustr + aChecklist.ReleaseEvidenceRef
          + u" metadata-only=true release-publishing=false code-signing=false"_ustr
          + u" notarization-submit=false update-channel-publication=false"_ustr
          + u" release-upload=false telemetry-upload=false publicEgress=false"_ustr;
    return aResult;
}

AIChatReleaseGAResult AIChatReleaseGARuntime::RecordGateEvidence(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatReleaseGAChecklist& rChecklist,
    const AIChatReleaseGAGateEvidenceRecord& rRecord) const
{
    AIChatReleaseGAResult aBase = SaveReleaseGAChecklist(rContext, rScope, rChecklist);
    if (!aBase.Success)
        return aBase;

    AIChatReleaseGAGateEvidenceRecord aRecord = rRecord;
    aRecord.ChecklistId = aBase.Checklist.ChecklistId;
    if (aRecord.RecordId.isEmpty())
        aRecord.RecordId = MakeGateEvidenceRecordId(aBase.Checklist, aRecord);
    if (aRecord.AuditLogRef.isEmpty())
        aRecord.AuditLogRef = u"audit-log-entry:"_ustr + aRecord.EvidenceId;

    if (!IsGateEvidenceRecordShapeAllowed(aBase.Checklist, aRecord))
        return MakeDeniedResult(u"invalid-gate-evidence-record-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"release-ga-gate-evidence"_ustr,
        { aRecord.RecordId, aRecord.ChecklistId, aRecord.GateId, aRecord.GateStatus,
          u"gateGreen=true"_ustr, u"blocksGA=true"_ustr, JoinFields(aRecord.EvidenceRefs),
          aRecord.ArtifactRef, aRecord.SigningRef, aRecord.UpdateChannelRef,
          aRecord.RecoveryProofRef, aRecord.PolicyDecisionRef,
          u"publicEgressRequired=false"_ustr, u"storesRawEvidence=false"_ustr,
          aRecord.EvidenceId, aRecord.AuditLogRef });
    if (!AppendUtf8Line(m_sReleaseGAStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.GateEvidence = aRecord;
    aBase.Message
        = u"release-ga-gate-evidence-recorded record-id="_ustr + aRecord.RecordId
          + u" checklist-id="_ustr + aRecord.ChecklistId + u" gate-id="_ustr
          + aRecord.GateId
          + u" gateStatus=green gateGreen=true blocksGA=true evidence-refs="_ustr
          + JoinFields(aRecord.EvidenceRefs) + u" artifact-ref="_ustr
          + aRecord.ArtifactRef + u" signing-ref="_ustr + aRecord.SigningRef
          + u" update-channel-ref="_ustr + aRecord.UpdateChannelRef
          + u" recovery-proof-ref="_ustr + aRecord.RecoveryProofRef
          + u" policy-decision-ref="_ustr + aRecord.PolicyDecisionRef
          + u" publicEgressRequired=false evidence-id="_ustr + aRecord.EvidenceId
          + u" audit-log-ref="_ustr + aRecord.AuditLogRef
          + u" metadata-only=true raw-evidence-payload=false artifact-publishing=false"_ustr
          + u" signing-execution=false update-channel-publish=false recovery-execution=false"_ustr;
    return aBase;
}

AIChatReleaseGAResult AIChatReleaseGARuntime::RecordReleaseSignoff(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatReleaseGAChecklist& rChecklist,
    const AIChatReleaseGASignoffRecord& rRecord) const
{
    AIChatReleaseGAResult aBase = SaveReleaseGAChecklist(rContext, rScope, rChecklist);
    if (!aBase.Success)
        return aBase;

    AIChatReleaseGASignoffRecord aRecord = rRecord;
    aRecord.ChecklistId = aBase.Checklist.ChecklistId;
    if (aRecord.SignoffId.isEmpty())
        aRecord.SignoffId = MakeReleaseSignoffId(aBase.Checklist, aRecord);
    if (aRecord.AuditLogRef.isEmpty())
        aRecord.AuditLogRef = u"audit-log-entry:"_ustr + aRecord.EvidenceId;

    if (!IsReleaseSignoffRecordShapeAllowed(aBase.Checklist, aRecord))
        return MakeDeniedResult(u"invalid-release-signoff-record-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"release-ga-signoff"_ustr,
        { aRecord.SignoffId, aRecord.ChecklistId, aRecord.ApproverRole,
          aRecord.ApproverId, aRecord.HumanApprovalRef, aRecord.ReleaseSignoffRef,
          aRecord.ArtifactBundleRef, aRecord.SigningRef, aRecord.UpdateChannelRef,
          aRecord.RecoveryProofRef, aRecord.SourceArchiveRef,
          aRecord.WindowsToastProofRef, aRecord.ReleasePolicyDecisionRef,
          aRecord.PolicyDecisionRef, u"humanApprovalRecorded=true"_ustr,
          u"automatedApproval=false"_ustr, u"allBlockingGatesGreen=true"_ustr,
          u"artifactsSigned=true"_ustr, u"updateChannelReady=true"_ustr,
          u"recoveryProofPresent=true"_ustr,
          u"requiresExplicitUserAuthorization=true"_ustr, u"canShip=false"_ustr,
          u"releasePublished=false"_ustr, aRecord.EvidenceId, aRecord.AuditLogRef });
    if (!AppendUtf8Line(m_sReleaseGAStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.Signoff = aRecord;
    aBase.Message
        = u"release-ga-signoff-recorded signoff-id="_ustr + aRecord.SignoffId
          + u" checklist-id="_ustr + aRecord.ChecklistId + u" approver-role="_ustr
          + aRecord.ApproverRole + u" human-approval-ref="_ustr
          + aRecord.HumanApprovalRef + u" release-signoff-ref="_ustr
          + aRecord.ReleaseSignoffRef + u" artifact-bundle-ref="_ustr
          + aRecord.ArtifactBundleRef + u" signing-ref="_ustr + aRecord.SigningRef
          + u" update-channel-ref="_ustr + aRecord.UpdateChannelRef
          + u" recovery-proof-ref="_ustr + aRecord.RecoveryProofRef
          + u" source-archive-ref="_ustr + aRecord.SourceArchiveRef
          + u" windows-toast-proof-ref="_ustr + aRecord.WindowsToastProofRef
          + u" release-policy-decision-ref="_ustr + aRecord.ReleasePolicyDecisionRef
          + u" humanApprovalRecorded=true automatedApproval=false"_ustr
          + u" automatedApprovalAllowed=false allBlockingGatesGreen=true"_ustr
          + u" artifactsSigned=true updateChannelReady=true recoveryProofPresent=true"_ustr
          + u" requiresExplicitUserAuthorization=true canShip=false releasePublished=false"_ustr
          + u" evidence-id="_ustr + aRecord.EvidenceId
          + u" audit-log-ref="_ustr + aRecord.AuditLogRef
          + u" metadata-only=true raw-signoff-payload=false artifact-publishing=false"_ustr
          + u" release-upload=false update-channel-publish=false publicEgress=false"_ustr;
    return aBase;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
