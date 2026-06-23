/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: perf/crash runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatPerfCrashRuntime.hxx"

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
constexpr OUStringLiteral PERF_CRASH_DIR_NAME = u"kqoffice-v3-ai-perf-crash";
constexpr OUStringLiteral PERF_CRASH_FILE_NAME = u"perf-crash.tsv";

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

bool EvidenceAllowed(const std::vector<OUString>& rEvidence,
                     bool (*pAllowed)(const OUString&))
{
    if (rEvidence.empty() || ContainsDuplicateString(rEvidence))
        return false;
    for (const OUString& rValue : rEvidence)
    {
        if (!pAllowed(rValue))
            return false;
    }
    return true;
}

bool HasPerfBaseEvidence(const std::vector<OUString>& rRequiredEvidence)
{
    return ContainsString(rRequiredEvidence, u"perf-sample"_ustr)
           && ContainsString(rRequiredEvidence, u"system-profile"_ustr)
           && ContainsString(rRequiredEvidence, u"local-provider-proof"_ustr)
           && ContainsString(rRequiredEvidence, u"knowledge-index-sample"_ustr)
           && ContainsString(rRequiredEvidence, u"policy-decision"_ustr)
           && ContainsString(rRequiredEvidence, u"audit-log-entry"_ustr)
           && ContainsString(rRequiredEvidence, u"distribution-update-policy"_ustr)
           && ContainsString(rRequiredEvidence, u"error-recovery-ux"_ustr)
           && ContainsString(rRequiredEvidence, u"localcloud-no-egress"_ustr);
}

bool HasCrashBaseEvidence(const std::vector<OUString>& rRequiredEvidence)
{
    return ContainsString(rRequiredEvidence, u"autosave-snapshot"_ustr)
           && ContainsString(rRequiredEvidence, u"sigkill-marker"_ustr)
           && ContainsString(rRequiredEvidence, u"recovery-dialog-shown"_ustr)
           && ContainsString(rRequiredEvidence, u"restore-applied"_ustr)
           && ContainsString(rRequiredEvidence, u"diff-zero-proof"_ustr)
           && ContainsString(rRequiredEvidence, u"policy-decision"_ustr)
           && ContainsString(rRequiredEvidence, u"audit-log-entry"_ustr)
           && ContainsString(rRequiredEvidence, u"distribution-update-policy"_ustr)
           && ContainsString(rRequiredEvidence, u"error-recovery-ux"_ustr)
           && ContainsString(rRequiredEvidence, u"localcloud-no-egress"_ustr);
}

bool ScopeAllowed(const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
                  const OUString& rExpectedTarget)
{
    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    return aScopeResult.Success && rScope.TargetType == rExpectedTarget
           && rScope.Surface == rExpectedTarget;
}

bool RefShapeAllowed(const OUString& rRef, const OUString& rPrefix)
{
    return rRef.startsWith(rPrefix) && rRef.getLength() > rPrefix.getLength();
}

bool DocumentIdAllowed(const OUString& rDocumentId)
{
    if (rDocumentId.isEmpty() || rDocumentId.getLength() > 80)
        return false;
    for (sal_Int32 i = 0; i < rDocumentId.getLength(); ++i)
    {
        const sal_Unicode c = rDocumentId[i];
        if (!((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-'
              || c == u'_'))
            return false;
    }
    return true;
}

AIChatPerfCrashResult MakeDeniedResult(const OUString& rReason)
{
    AIChatPerfCrashResult aResult;
    aResult.Message
        = u"perf-crash-denied reason="_ustr + rReason
          + u" fail-closed-user-visible=true metadata-only=true"_ustr
          + u" coldStartTargetMs=2000 firstTokenTargetMs=800 retrievalTargetMs=200"_ustr
          + u" provider=ollama-local model=llama3.2:3b corpusDocuments=10000 topK=5"_ustr
          + u" autosaveIntervalSeconds=30 recoveryDialogSeconds=30 diffExpected=zero"_ustr
          + u" dataLossTolerance=none local-file-only=true publicEgress=false"_ustr
          + u" hiddenModelDownload=false storesDocumentContent=false raw-sample-payload=false"_ustr
          + u" runtime-sampler=not-started benchmark-daemon=not-started"_ustr
          + u" sigkill-runner=not-started crash-injector=not-started"_ustr
          + u" telemetry-upload=not-started cloud-recovery=not-started webview=false"_ustr;
    return aResult;
}
}

AIChatPerfCrashRuntime::AIChatPerfCrashRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + PERF_CRASH_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sPerfCrashStoreUrl = m_sStorageRootUrl + u"/"_ustr + PERF_CRASH_FILE_NAME;
}

bool AIChatPerfCrashRuntime::IsPerfTargetIdAllowed(const OUString& rTargetId)
{
    return rTargetId.startsWith(u"v3-perf-baseline-"_ustr)
           && (rTargetId.endsWith(u"-001"_ustr) || rTargetId.endsWith(u"-002"_ustr)
               || rTargetId.endsWith(u"-003"_ustr));
}

bool AIChatPerfCrashRuntime::IsCrashTargetIdAllowed(const OUString& rTargetId)
{
    return rTargetId.startsWith(u"v3-crash-recovery-"_ustr)
           && (rTargetId.endsWith(u"-001"_ustr) || rTargetId.endsWith(u"-002"_ustr)
               || rTargetId.endsWith(u"-003"_ustr));
}

bool AIChatPerfCrashRuntime::IsPlatformAllowed(const OUString& rPlatform)
{
    return rPlatform == u"macos-arm64"_ustr || rPlatform == u"linux-x86_64"_ustr
           || rPlatform == u"windows-x86_64"_ustr;
}

bool AIChatPerfCrashRuntime::IsPackageRouteAllowedForPlatform(const OUString& rPlatform,
                                                              const OUString& rRoute)
{
    return (rPlatform == u"macos-arm64"_ustr && rRoute == u"dmg"_ustr)
           || (rPlatform == u"linux-x86_64"_ustr && rRoute == u"appimage"_ustr)
           || (rPlatform == u"windows-x86_64"_ustr && rRoute == u"msi"_ustr);
}

bool AIChatPerfCrashRuntime::IsPerfRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"perf-sample"_ustr || rEvidence == u"system-profile"_ustr
           || rEvidence == u"local-provider-proof"_ustr
           || rEvidence == u"knowledge-index-sample"_ustr
           || rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"distribution-update-policy"_ustr
           || rEvidence == u"error-recovery-ux"_ustr
           || rEvidence == u"localcloud-no-egress"_ustr;
}

bool AIChatPerfCrashRuntime::IsCrashRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"autosave-snapshot"_ustr || rEvidence == u"sigkill-marker"_ustr
           || rEvidence == u"recovery-dialog-shown"_ustr
           || rEvidence == u"restore-applied"_ustr || rEvidence == u"diff-zero-proof"_ustr
           || rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"distribution-update-policy"_ustr
           || rEvidence == u"error-recovery-ux"_ustr
           || rEvidence == u"localcloud-no-egress"_ustr;
}

bool AIChatPerfCrashRuntime::IsDocumentSurfaceAllowedForPlatform(
    const OUString& rPlatform, const OUString& rSurface)
{
    return (rPlatform == u"macos-arm64"_ustr && rSurface == u"writer"_ustr)
           || (rPlatform == u"linux-x86_64"_ustr && rSurface == u"calc"_ustr)
           || (rPlatform == u"windows-x86_64"_ustr && rSurface == u"impress"_ustr);
}

bool AIChatPerfCrashRuntime::IsEditKindAllowedForSurface(const OUString& rSurface,
                                                         const OUString& rEditKind)
{
    return (rSurface == u"writer"_ustr && rEditKind == u"text-insert"_ustr)
           || (rSurface == u"calc"_ustr && rEditKind == u"cell-edit"_ustr)
           || (rSurface == u"impress"_ustr && rEditKind == u"slide-text-edit"_ustr);
}

bool AIChatPerfCrashRuntime::IsPerfTargetShapeAllowed(
    const AIChatPerfBaselineTarget& rTarget)
{
    return IsPerfTargetIdAllowed(rTarget.TargetId) && rTarget.Wave == u"w9"_ustr
           && rTarget.Category == u"perf-baseline"_ustr
           && IsPlatformAllowed(rTarget.Platform)
           && rTarget.MeasurementMode == u"metadata-runtime-active"_ustr
           && rTarget.ColdStart.TargetMs == 2000
           && rTarget.ColdStart.Measurement == u"main-window-interactive"_ustr
           && IsPackageRouteAllowedForPlatform(rTarget.Platform, rTarget.ColdStart.PackageRoute)
           && rTarget.FirstToken.TargetMs == 800
           && rTarget.FirstToken.Trigger == u"cmd-shift-k-chat"_ustr
           && rTarget.FirstToken.Provider == u"ollama-local"_ustr
           && rTarget.FirstToken.Model == u"llama3.2:3b"_ustr
           && rTarget.Retrieval.TargetMs == 200 && rTarget.Retrieval.CorpusDocuments == 10000
           && rTarget.Retrieval.TopK == 5
           && rTarget.Retrieval.Index == u"local-knowledge-index"_ustr
           && HasPerfBaseEvidence(rTarget.RequiredEvidence)
           && EvidenceAllowed(rTarget.RequiredEvidence, IsPerfRequiredEvidenceAllowed)
           && HasValidEvidenceIds(rTarget.EvidenceIds) && rTarget.BlocksGA
           && rTarget.Gates.BlocksGA
           && rTarget.Gates.RuntimeImplementation == u"metadata-runtime-active"_ustr
           && !rTarget.Gates.RuntimeSamplerActive && !rTarget.Gates.ModelDownloadActive
           && !rTarget.Gates.ExternalMetricUploadActive && !rTarget.Gates.BackgroundProbeActive
           && RefShapeAllowed(rTarget.TenantContextRef, u"tenant-context:"_ustr)
           && RefShapeAllowed(rTarget.PolicyContextRef, u"policy-context:"_ustr)
           && RefShapeAllowed(rTarget.AuditChainRef, u"audit-chain:"_ustr)
           && RefShapeAllowed(rTarget.DistributionRecoveryRef, u"distribution-recovery:"_ustr)
           && RefShapeAllowed(rTarget.NoEgressRef, u"localcloud-no-egress:"_ustr)
           && RefShapeAllowed(rTarget.ReleaseEvidenceRef, u"release-evidence:"_ustr)
           && AIChatTenantContextRuntime::IsHashReferenceAllowed(rTarget.HashReference);
}

bool AIChatPerfCrashRuntime::IsPerfSampleRecordShapeAllowed(
    const AIChatPerfBaselineTarget& rTarget, const AIChatPerfSampleRecord& rRecord)
{
    return rRecord.SampleId.startsWith(u"pfs-"_ustr)
           && IsLowerHex(rRecord.SampleId.copy(4), 16) && rRecord.TargetId == rTarget.TargetId
           && rRecord.Platform == rTarget.Platform
           && rRecord.ColdStartMs <= rTarget.ColdStart.TargetMs
           && rRecord.FirstTokenMs <= rTarget.FirstToken.TargetMs
           && rRecord.RetrievalMs <= rTarget.Retrieval.TargetMs
           && rRecord.PackageRoute == rTarget.ColdStart.PackageRoute
           && rRecord.Provider == u"ollama-local"_ustr && rRecord.Model == u"llama3.2:3b"_ustr
           && rRecord.CorpusDocuments >= 10000 && rRecord.TopK == 5
           && RefShapeAllowed(rRecord.SystemProfileRef, u"system-profile:"_ustr)
           && RefShapeAllowed(rRecord.LocalProviderProofRef, u"local-provider-proof:"_ustr)
           && RefShapeAllowed(rRecord.KnowledgeIndexSampleRef, u"knowledge-index-sample:"_ustr)
           && !rRecord.PublicEgress && !rRecord.HiddenModelDownload
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rRecord.EvidenceId)
           && RefShapeAllowed(rRecord.AuditLogRef, u"audit-log-entry:"_ustr);
}

bool AIChatPerfCrashRuntime::IsCrashRecoveryTargetShapeAllowed(
    const AIChatCrashRecoveryTarget& rTarget)
{
    const AIChatCrashRecoveryScenarioTarget& rScenario = rTarget.Scenario;
    return IsCrashTargetIdAllowed(rTarget.TargetId) && rTarget.Wave == u"w9"_ustr
           && rTarget.Category == u"crash-recovery"_ustr
           && IsPlatformAllowed(rTarget.Platform)
           && rTarget.MeasurementMode == u"metadata-runtime-active"_ustr
           && IsDocumentSurfaceAllowedForPlatform(rTarget.Platform, rScenario.DocumentSurface)
           && rScenario.CrashTrigger.Mode == u"sigkill"_ustr
           && rScenario.CrashTrigger.ProcessState == u"editing-unsaved"_ustr
           && DocumentIdAllowed(rScenario.UnsavedEdit.DocumentId)
           && IsEditKindAllowedForSurface(rScenario.DocumentSurface, rScenario.UnsavedEdit.EditKind)
           && rScenario.UnsavedEdit.MinCharacters >= 1
           && rScenario.UnsavedEdit.IncludesUnsavedState
           && rScenario.Autosave.IntervalSeconds == 30
           && rScenario.Autosave.Storage == u"local-file-only"_ustr
           && !rScenario.Autosave.PublicEgress
           && rScenario.Recovery.Dialog == u"RecoveryDialog"_ustr
           && rScenario.Recovery.MaxDialogSeconds == 30 && rScenario.Recovery.OneClickRestore
           && rScenario.Recovery.DiffExpected == u"zero"_ustr
           && rScenario.Recovery.DataLossTolerance == u"none"_ustr
           && HasCrashBaseEvidence(rTarget.RequiredEvidence)
           && EvidenceAllowed(rTarget.RequiredEvidence, IsCrashRequiredEvidenceAllowed)
           && HasValidEvidenceIds(rTarget.EvidenceIds) && rTarget.BlocksGA
           && rTarget.Gates.BlocksGA
           && rTarget.Gates.RuntimeImplementation == u"metadata-runtime-active"_ustr
           && !rTarget.Gates.SigkillRunnerActive && !rTarget.Gates.AutosaveEngineRuntimeActive
           && !rTarget.Gates.RecoveryDialogRuntimeActive && !rTarget.Gates.CloudRecoveryRuntimeActive
           && !rTarget.Gates.MainDocumentWriteRuntimeActive
           && RefShapeAllowed(rTarget.TenantContextRef, u"tenant-context:"_ustr)
           && RefShapeAllowed(rTarget.PolicyContextRef, u"policy-context:"_ustr)
           && RefShapeAllowed(rTarget.AuditChainRef, u"audit-chain:"_ustr)
           && RefShapeAllowed(rTarget.DistributionRecoveryRef, u"distribution-recovery:"_ustr)
           && RefShapeAllowed(rTarget.NoEgressRef, u"localcloud-no-egress:"_ustr)
           && RefShapeAllowed(rTarget.ReleaseEvidenceRef, u"release-evidence:"_ustr)
           && AIChatTenantContextRuntime::IsHashReferenceAllowed(rTarget.HashReference);
}

bool AIChatPerfCrashRuntime::IsCrashRecoverySampleRecordShapeAllowed(
    const AIChatCrashRecoveryTarget& rTarget,
    const AIChatCrashRecoverySampleRecord& rRecord)
{
    return rRecord.SampleId.startsWith(u"crs-"_ustr)
           && IsLowerHex(rRecord.SampleId.copy(4), 16) && rRecord.TargetId == rTarget.TargetId
           && rRecord.Platform == rTarget.Platform
           && rRecord.DocumentSurface == rTarget.Scenario.DocumentSurface
           && rRecord.DocumentId == rTarget.Scenario.UnsavedEdit.DocumentId
           && RefShapeAllowed(rRecord.AutosaveSnapshotRef, u"autosave-snapshot:"_ustr)
           && RefShapeAllowed(rRecord.SigkillMarkerRef, u"sigkill-marker:"_ustr)
           && RefShapeAllowed(rRecord.RecoveryDialogRef, u"recovery-dialog-shown:"_ustr)
           && RefShapeAllowed(rRecord.RestoreAppliedRef, u"restore-applied:"_ustr)
           && RefShapeAllowed(rRecord.DiffZeroProofRef, u"diff-zero-proof:"_ustr)
           && rRecord.AutosaveIntervalSeconds <= 30 && rRecord.RecoveryDialogSeconds <= 30
           && rRecord.OneClickRestore && rRecord.DiffExpected == u"zero"_ustr
           && rRecord.DataLossTolerance == u"none"_ustr && !rRecord.MainDocumentChanged
           && !rRecord.PublicEgress
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rRecord.EvidenceId)
           && RefShapeAllowed(rRecord.AuditLogRef, u"audit-log-entry:"_ustr);
}

OUString AIChatPerfCrashRuntime::MakePerfTargetId(const OUString& rPlatform)
{
    return u"v3-perf-baseline-"_ustr + rPlatform + u"-001"_ustr;
}

OUString AIChatPerfCrashRuntime::MakeCrashTargetId(const OUString& rPlatform,
                                                   const OUString& rSurface)
{
    return u"v3-crash-recovery-"_ustr + rPlatform.getToken(0, '-') + u"-"_ustr + rSurface
           + u"-001"_ustr;
}

OUString AIChatPerfCrashRuntime::MakePerfTargetHashReference(
    const AIChatPerfBaselineTarget& rTarget)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rTarget.TargetId + u":"_ustr + rTarget.Platform + u":2000:800:200"_ustr);
}

OUString AIChatPerfCrashRuntime::MakeCrashTargetHashReference(
    const AIChatCrashRecoveryTarget& rTarget)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rTarget.TargetId + u":"_ustr + rTarget.Platform + u":"_ustr
                 + rTarget.Scenario.DocumentSurface + u":30:30:zero"_ustr);
}

OUString AIChatPerfCrashRuntime::MakePerfSampleId(
    const AIChatPerfBaselineTarget& rTarget, const AIChatPerfSampleRecord& rRecord)
{
    return u"pfs-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rTarget.TargetId + u":"_ustr + rRecord.Platform + u":"_ustr
                 + rRecord.EvidenceId)
                 .copy(0, 16);
}

OUString AIChatPerfCrashRuntime::MakeCrashSampleId(
    const AIChatCrashRecoveryTarget& rTarget,
    const AIChatCrashRecoverySampleRecord& rRecord)
{
    return u"crs-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rTarget.TargetId + u":"_ustr + rRecord.DocumentId + u":"_ustr
                 + rRecord.EvidenceId)
                 .copy(0, 16);
}

AIChatPerfCrashResult AIChatPerfCrashRuntime::SavePerfBaselineTarget(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatPerfBaselineTarget& rTarget) const
{
    if (!ScopeAllowed(rContext, rScope, u"perf-baseline"_ustr))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatPerfBaselineTarget aTarget = rTarget;
    if (aTarget.TargetId.isEmpty())
        aTarget.TargetId = MakePerfTargetId(aTarget.Platform);
    aTarget.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aTarget.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aTarget.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aTarget.DistributionRecoveryRef.isEmpty())
        aTarget.DistributionRecoveryRef = u"distribution-recovery:metadata-runtime-active"_ustr;
    if (aTarget.NoEgressRef.isEmpty())
        aTarget.NoEgressRef = u"localcloud-no-egress:default-deny"_ustr;
    if (aTarget.ReleaseEvidenceRef.isEmpty())
        aTarget.ReleaseEvidenceRef = u"release-evidence:ga-blocker"_ustr;
    if (aTarget.HashReference.isEmpty())
        aTarget.HashReference = MakePerfTargetHashReference(aTarget);

    if (!IsPerfTargetShapeAllowed(aTarget))
        return MakeDeniedResult(u"invalid-perf-baseline-target-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"perf-baseline-target"_ustr,
        { aTarget.TargetId, aTarget.Wave, aTarget.Category, aTarget.Platform,
          aTarget.MeasurementMode, OUString::number(aTarget.ColdStart.TargetMs),
          aTarget.ColdStart.Measurement, aTarget.ColdStart.PackageRoute,
          OUString::number(aTarget.FirstToken.TargetMs), aTarget.FirstToken.Trigger,
          aTarget.FirstToken.Provider, aTarget.FirstToken.Model,
          OUString::number(aTarget.Retrieval.TargetMs),
          OUString::number(aTarget.Retrieval.CorpusDocuments),
          OUString::number(aTarget.Retrieval.TopK), aTarget.Retrieval.Index,
          JoinFields(aTarget.RequiredEvidence), JoinFields(aTarget.EvidenceIds),
          aTarget.TenantContextRef, aTarget.PolicyContextRef, aTarget.AuditChainRef,
          aTarget.DistributionRecoveryRef, aTarget.NoEgressRef, aTarget.ReleaseEvidenceRef,
          aTarget.HashReference });
    if (!AppendUtf8Line(m_sPerfCrashStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatPerfCrashResult aResult;
    aResult.Success = true;
    aResult.PerfTarget = aTarget;
    aResult.Message
        = u"perf-baseline-target-saved target-id="_ustr + aTarget.TargetId
          + u" platform="_ustr + aTarget.Platform
          + u" coldStartTargetMs=2000 measurement=main-window-interactive packageRoute="_ustr
          + aTarget.ColdStart.PackageRoute
          + u" firstTokenTargetMs=800 trigger=cmd-shift-k-chat provider=ollama-local"_ustr
          + u" model=llama3.2:3b retrievalTargetMs=200 corpusDocuments=10000 topK=5"_ustr
          + u" index=local-knowledge-index blocksGA=true"_ustr
          + u" distribution-recovery-ref="_ustr + aTarget.DistributionRecoveryRef
          + u" no-egress-ref="_ustr + aTarget.NoEgressRef
          + u" release-evidence-ref="_ustr + aTarget.ReleaseEvidenceRef
          + u" metadata-only=true runtimeImplementation=metadata-runtime-active"_ustr
          + u" runtime-sampler=not-started benchmark-daemon=not-started"_ustr
          + u" model-download=not-started telemetry-upload=not-started publicEgress=false"_ustr;
    return aResult;
}

AIChatPerfCrashResult AIChatPerfCrashRuntime::RecordPerfSample(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatPerfBaselineTarget& rTarget, const AIChatPerfSampleRecord& rRecord) const
{
    AIChatPerfCrashResult aBase = SavePerfBaselineTarget(rContext, rScope, rTarget);
    if (!aBase.Success)
        return aBase;

    AIChatPerfSampleRecord aRecord = rRecord;
    aRecord.TargetId = aBase.PerfTarget.TargetId;
    if (aRecord.SampleId.isEmpty())
        aRecord.SampleId = MakePerfSampleId(aBase.PerfTarget, aRecord);
    if (aRecord.AuditLogRef.isEmpty())
        aRecord.AuditLogRef = u"audit-log-entry:"_ustr + aRecord.EvidenceId;

    if (!IsPerfSampleRecordShapeAllowed(aBase.PerfTarget, aRecord))
        return MakeDeniedResult(u"invalid-perf-sample-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"perf-sample"_ustr,
        { aRecord.SampleId, aRecord.TargetId, aRecord.Platform,
          OUString::number(aRecord.ColdStartMs), OUString::number(aRecord.FirstTokenMs),
          OUString::number(aRecord.RetrievalMs), aRecord.PackageRoute, aRecord.Provider,
          aRecord.Model, OUString::number(aRecord.CorpusDocuments),
          OUString::number(aRecord.TopK), aRecord.SystemProfileRef,
          aRecord.LocalProviderProofRef, aRecord.KnowledgeIndexSampleRef,
          u"publicEgress=false"_ustr, u"hiddenModelDownload=false"_ustr, aRecord.EvidenceId,
          aRecord.AuditLogRef });
    if (!AppendUtf8Line(m_sPerfCrashStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.PerfSample = aRecord;
    aBase.Message
        = u"perf-sample-recorded sample-id="_ustr + aRecord.SampleId
          + u" target-id="_ustr + aRecord.TargetId + u" platform="_ustr
          + aRecord.Platform
          + u" coldStartMs="_ustr + OUString::number(aRecord.ColdStartMs)
          + u" firstTokenMs="_ustr + OUString::number(aRecord.FirstTokenMs)
          + u" retrievalMs="_ustr + OUString::number(aRecord.RetrievalMs)
          + u" targetsMet=true system-profile-ref="_ustr + aRecord.SystemProfileRef
          + u" local-provider-proof-ref="_ustr + aRecord.LocalProviderProofRef
          + u" knowledge-index-sample-ref="_ustr + aRecord.KnowledgeIndexSampleRef
          + u" publicEgress=false hiddenModelDownload=false evidence-id="_ustr
          + aRecord.EvidenceId
          + u" audit-log-ref="_ustr + aRecord.AuditLogRef
          + u" metadata-only=true raw-perf-payload=false runtime-benchmark=false"_ustr;
    return aBase;
}

AIChatPerfCrashResult AIChatPerfCrashRuntime::SaveCrashRecoveryTarget(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatCrashRecoveryTarget& rTarget) const
{
    if (!ScopeAllowed(rContext, rScope, u"crash-recovery"_ustr))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatCrashRecoveryTarget aTarget = rTarget;
    if (aTarget.TargetId.isEmpty())
        aTarget.TargetId = MakeCrashTargetId(aTarget.Platform, aTarget.Scenario.DocumentSurface);
    aTarget.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aTarget.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aTarget.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aTarget.DistributionRecoveryRef.isEmpty())
        aTarget.DistributionRecoveryRef = u"distribution-recovery:metadata-runtime-active"_ustr;
    if (aTarget.NoEgressRef.isEmpty())
        aTarget.NoEgressRef = u"localcloud-no-egress:default-deny"_ustr;
    if (aTarget.ReleaseEvidenceRef.isEmpty())
        aTarget.ReleaseEvidenceRef = u"release-evidence:ga-blocker"_ustr;
    if (aTarget.HashReference.isEmpty())
        aTarget.HashReference = MakeCrashTargetHashReference(aTarget);

    if (!IsCrashRecoveryTargetShapeAllowed(aTarget))
        return MakeDeniedResult(u"invalid-crash-recovery-target-shape"_ustr);

    const AIChatCrashRecoveryScenarioTarget& rScenario = aTarget.Scenario;
    const OUString sLine = MakeStoreLine(
        u"crash-recovery-target"_ustr,
        { aTarget.TargetId, aTarget.Wave, aTarget.Category, aTarget.Platform,
          aTarget.MeasurementMode, rScenario.DocumentSurface, rScenario.CrashTrigger.Mode,
          rScenario.CrashTrigger.ProcessState, rScenario.UnsavedEdit.DocumentId,
          rScenario.UnsavedEdit.EditKind,
          OUString::number(rScenario.UnsavedEdit.MinCharacters),
          u"includesUnsavedState=true"_ustr,
          OUString::number(rScenario.Autosave.IntervalSeconds), rScenario.Autosave.Storage,
          u"publicEgress=false"_ustr, rScenario.Recovery.Dialog,
          OUString::number(rScenario.Recovery.MaxDialogSeconds),
          u"oneClickRestore=true"_ustr, rScenario.Recovery.DiffExpected,
          rScenario.Recovery.DataLossTolerance, JoinFields(aTarget.RequiredEvidence),
          JoinFields(aTarget.EvidenceIds), aTarget.TenantContextRef, aTarget.PolicyContextRef,
          aTarget.AuditChainRef, aTarget.DistributionRecoveryRef, aTarget.NoEgressRef,
          aTarget.ReleaseEvidenceRef, aTarget.HashReference });
    if (!AppendUtf8Line(m_sPerfCrashStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatPerfCrashResult aResult;
    aResult.Success = true;
    aResult.CrashTarget = aTarget;
    aResult.Message
        = u"crash-recovery-target-saved target-id="_ustr + aTarget.TargetId
          + u" platform="_ustr + aTarget.Platform + u" documentSurface="_ustr
          + rScenario.DocumentSurface
          + u" crashTrigger=sigkill processState=editing-unsaved editKind="_ustr
          + rScenario.UnsavedEdit.EditKind
          + u" includesUnsavedState=true autosaveIntervalSeconds=30 storage=local-file-only"_ustr
          + u" publicEgress=false dialog=RecoveryDialog recoveryDialogSeconds=30"_ustr
          + u" oneClickRestore=true diffExpected=zero dataLossTolerance=none blocksGA=true"_ustr
          + u" distribution-recovery-ref="_ustr + aTarget.DistributionRecoveryRef
          + u" no-egress-ref="_ustr + aTarget.NoEgressRef
          + u" release-evidence-ref="_ustr + aTarget.ReleaseEvidenceRef
          + u" metadata-only=true runtimeImplementation=metadata-runtime-active"_ustr
          + u" sigkill-runner=not-started autosave-engine-runtime=not-started"_ustr
          + u" recovery-dialog-runtime=not-started cloud-recovery=not-started"_ustr
          + u" main-document-write-runtime=not-started"_ustr;
    return aResult;
}

AIChatPerfCrashResult AIChatPerfCrashRuntime::RecordCrashRecoverySample(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatCrashRecoveryTarget& rTarget,
    const AIChatCrashRecoverySampleRecord& rRecord) const
{
    AIChatPerfCrashResult aBase = SaveCrashRecoveryTarget(rContext, rScope, rTarget);
    if (!aBase.Success)
        return aBase;

    AIChatCrashRecoverySampleRecord aRecord = rRecord;
    aRecord.TargetId = aBase.CrashTarget.TargetId;
    if (aRecord.SampleId.isEmpty())
        aRecord.SampleId = MakeCrashSampleId(aBase.CrashTarget, aRecord);
    if (aRecord.AuditLogRef.isEmpty())
        aRecord.AuditLogRef = u"audit-log-entry:"_ustr + aRecord.EvidenceId;

    if (!IsCrashRecoverySampleRecordShapeAllowed(aBase.CrashTarget, aRecord))
        return MakeDeniedResult(u"invalid-crash-recovery-sample-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"crash-recovery-sample"_ustr,
        { aRecord.SampleId, aRecord.TargetId, aRecord.Platform, aRecord.DocumentSurface,
          aRecord.DocumentId, aRecord.AutosaveSnapshotRef, aRecord.SigkillMarkerRef,
          aRecord.RecoveryDialogRef, aRecord.RestoreAppliedRef, aRecord.DiffZeroProofRef,
          OUString::number(aRecord.AutosaveIntervalSeconds),
          OUString::number(aRecord.RecoveryDialogSeconds), u"oneClickRestore=true"_ustr,
          aRecord.DiffExpected, aRecord.DataLossTolerance, u"mainDocumentChanged=false"_ustr,
          u"publicEgress=false"_ustr, aRecord.EvidenceId, aRecord.AuditLogRef });
    if (!AppendUtf8Line(m_sPerfCrashStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.CrashSample = aRecord;
    aBase.Message
        = u"crash-recovery-sample-recorded sample-id="_ustr + aRecord.SampleId
          + u" target-id="_ustr + aRecord.TargetId + u" platform="_ustr
          + aRecord.Platform + u" documentSurface="_ustr + aRecord.DocumentSurface
          + u" autosave-snapshot-ref="_ustr + aRecord.AutosaveSnapshotRef
          + u" sigkill-marker-ref="_ustr + aRecord.SigkillMarkerRef
          + u" recovery-dialog-ref="_ustr + aRecord.RecoveryDialogRef
          + u" restore-applied-ref="_ustr + aRecord.RestoreAppliedRef
          + u" diff-zero-proof-ref="_ustr + aRecord.DiffZeroProofRef
          + u" autosaveIntervalSeconds=30 recoveryDialogSeconds=30 oneClickRestore=true"_ustr
          + u" diffExpected=zero dataLossTolerance=none mainDocumentChanged=false"_ustr
          + u" publicEgress=false evidence-id="_ustr + aRecord.EvidenceId
          + u" audit-log-ref="_ustr + aRecord.AuditLogRef
          + u" metadata-only=true raw-recovery-payload=false sigkill-execution=false"_ustr;
    return aBase;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
