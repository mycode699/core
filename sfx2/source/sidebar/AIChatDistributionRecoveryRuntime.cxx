/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: distribution/recovery runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatDistributionRecoveryRuntime.hxx"

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
constexpr OUStringLiteral DISTRIBUTION_RECOVERY_DIR_NAME
    = u"kqoffice-v3-ai-distribution-recovery";
constexpr OUStringLiteral DISTRIBUTION_RECOVERY_FILE_NAME = u"distribution-recovery.tsv";

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

bool HasDistributionBaseEvidence(const std::vector<OUString>& rRequiredEvidence)
{
    return ContainsString(rRequiredEvidence, u"distribution-update-policy"_ustr)
           && ContainsString(rRequiredEvidence, u"artifact-signature"_ustr)
           && ContainsString(rRequiredEvidence, u"installer-smoke"_ustr)
           && ContainsString(rRequiredEvidence, u"update-prompt"_ustr)
           && ContainsString(rRequiredEvidence, u"rollback-proof"_ustr)
           && ContainsString(rRequiredEvidence, u"evidence-record"_ustr)
           && ContainsString(rRequiredEvidence, u"v2-regression-green"_ustr)
           && ContainsString(rRequiredEvidence, u"policy-decision"_ustr)
           && ContainsString(rRequiredEvidence, u"audit-log-entry"_ustr)
           && ContainsString(rRequiredEvidence, u"edition-policy"_ustr)
           && ContainsString(rRequiredEvidence, u"i18n-locale-policy"_ustr)
           && ContainsString(rRequiredEvidence, u"manual-docs-manifest"_ustr)
           && ContainsString(rRequiredEvidence, u"localcloud-no-egress"_ustr);
}

bool HasRecoveryBaseEvidence(const std::vector<OUString>& rRequiredEvidence)
{
    return ContainsString(rRequiredEvidence, u"error-recovery-ux"_ustr)
           && ContainsString(rRequiredEvidence, u"inline-guidance"_ustr)
           && ContainsString(rRequiredEvidence, u"next-step-action"_ustr)
           && ContainsString(rRequiredEvidence, u"openable-evidence"_ustr)
           && ContainsString(rRequiredEvidence, u"diagnostics-export"_ustr)
           && ContainsString(rRequiredEvidence, u"evidence-record"_ustr)
           && ContainsString(rRequiredEvidence, u"v2-regression-green"_ustr)
           && ContainsString(rRequiredEvidence, u"policy-decision"_ustr)
           && ContainsString(rRequiredEvidence, u"audit-log-entry"_ustr)
           && ContainsString(rRequiredEvidence, u"edition-policy"_ustr)
           && ContainsString(rRequiredEvidence, u"i18n-locale-policy"_ustr)
           && ContainsString(rRequiredEvidence, u"manual-docs-manifest"_ustr)
           && ContainsString(rRequiredEvidence, u"localcloud-no-egress"_ustr);
}

std::vector<OUString> ExpectedPlatforms()
{
    return { u"macos"_ustr, u"windows"_ustr, u"linux"_ustr, u"self-hosted"_ustr };
}

std::vector<OUString> ExpectedArtifacts()
{
    return { u"DMG"_ustr, u"MSI"_ustr, u"AppImage"_ustr, u"docker"_ustr };
}

std::vector<OUString> ExpectedRecoveryKinds()
{
    return { u"provider-timeout"_ustr, u"connector-auth-expired"_ustr,
             u"policy-denied"_ustr, u"patch-apply-failed"_ustr };
}

std::vector<OUString> ExpectedRecoverySurfaces()
{
    return { u"writer"_ustr, u"calc"_ustr, u"companion"_ustr, u"impress"_ustr };
}

const AIChatErrorRecoveryScenario* FindScenario(
    const AIChatErrorRecoveryUxManifest& rManifest, const OUString& rScenarioKind)
{
    for (const AIChatErrorRecoveryScenario& rScenario : rManifest.Scenarios)
    {
        if (rScenario.Kind == rScenarioKind)
            return &rScenario;
    }
    return nullptr;
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

AIChatDistributionRecoveryResult MakeDeniedResult(const OUString& rReason)
{
    AIChatDistributionRecoveryResult aResult;
    aResult.Message
        = u"distribution-recovery-denied reason="_ustr + rReason
          + u" fail-closed-user-visible=true metadata-only=true"_ustr
          + u" storesDocumentContent=false publicEgress=false raw-update-payload=false"_ustr
          + u" promptRequired=true oneClick=true deferrable=true forceUpdateAllowed=false"_ustr
          + u" requiresPublicInternet=false lanSupported=true rollbackRequired=true"_ustr
          + u" inline-guidance=true openableEvidence=true diagnosticsExportable=true"_ustr
          + u" mainDocumentUnchangedUntilApply=true deadEndAllowed=false"_ustr
          + u" installer-packaging-runtime=not-started update-server-runtime=not-started"_ustr
          + u" network-downloader-runtime=not-started updater-daemon=not-started"_ustr
          + u" os-notification-bridge=not-started crash-reporter=not-started"_ustr
          + u" remote-recovery-service=not-started webview=false"_ustr;
    return aResult;
}
}

AIChatDistributionRecoveryRuntime::AIChatDistributionRecoveryRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + DISTRIBUTION_RECOVERY_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sDistributionRecoveryStoreUrl
        = m_sStorageRootUrl + u"/"_ustr + DISTRIBUTION_RECOVERY_FILE_NAME;
}

bool AIChatDistributionRecoveryRuntime::IsDistributionManifestIdAllowed(
    const OUString& rManifestId)
{
    return rManifestId.startsWith(u"dist-"_ustr) && IsLowerHex(rManifestId.copy(5), 16);
}

bool AIChatDistributionRecoveryRuntime::IsErrorRecoveryManifestIdAllowed(
    const OUString& rManifestId)
{
    return rManifestId.startsWith(u"errux-"_ustr) && IsLowerHex(rManifestId.copy(6), 16);
}

bool AIChatDistributionRecoveryRuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return AIChatAuditLogRuntime::IsTimestampAllowed(rTimestamp);
}

bool AIChatDistributionRecoveryRuntime::IsPlatformArtifactAllowed(
    const OUString& rPlatform, const OUString& rArtifact)
{
    return (rPlatform == u"macos"_ustr && rArtifact == u"DMG"_ustr)
           || (rPlatform == u"windows"_ustr && rArtifact == u"MSI"_ustr)
           || (rPlatform == u"linux"_ustr && rArtifact == u"AppImage"_ustr)
           || (rPlatform == u"self-hosted"_ustr && rArtifact == u"docker"_ustr);
}

bool AIChatDistributionRecoveryRuntime::IsDistributionChannelOrderAllowed(
    const std::vector<AIChatDistributionChannel>& rChannels)
{
    const std::vector<OUString> aPlatforms = ExpectedPlatforms();
    const std::vector<OUString> aArtifacts = ExpectedArtifacts();
    if (rChannels.size() != aPlatforms.size())
        return false;
    for (size_t i = 0; i < rChannels.size(); ++i)
    {
        if (rChannels[i].Platform != aPlatforms[i] || rChannels[i].Artifact != aArtifacts[i])
            return false;
    }
    return true;
}

bool AIChatDistributionRecoveryRuntime::IsDistributionRequiredEvidenceAllowed(
    const OUString& rEvidence)
{
    return rEvidence == u"distribution-update-policy"_ustr
           || rEvidence == u"artifact-signature"_ustr
           || rEvidence == u"installer-smoke"_ustr
           || rEvidence == u"update-prompt"_ustr
           || rEvidence == u"rollback-proof"_ustr
           || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"v2-regression-green"_ustr
           || rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"edition-policy"_ustr
           || rEvidence == u"i18n-locale-policy"_ustr
           || rEvidence == u"manual-docs-manifest"_ustr
           || rEvidence == u"localcloud-no-egress"_ustr;
}

bool AIChatDistributionRecoveryRuntime::IsErrorRecoveryRequiredEvidenceAllowed(
    const OUString& rEvidence)
{
    return rEvidence == u"error-recovery-ux"_ustr
           || rEvidence == u"inline-guidance"_ustr
           || rEvidence == u"next-step-action"_ustr
           || rEvidence == u"openable-evidence"_ustr
           || rEvidence == u"diagnostics-export"_ustr
           || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"v2-regression-green"_ustr
           || rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"edition-policy"_ustr
           || rEvidence == u"i18n-locale-policy"_ustr
           || rEvidence == u"manual-docs-manifest"_ustr
           || rEvidence == u"localcloud-no-egress"_ustr;
}

bool AIChatDistributionRecoveryRuntime::IsRecoveryKindAllowed(const OUString& rKind)
{
    return ContainsString(ExpectedRecoveryKinds(), rKind);
}

bool AIChatDistributionRecoveryRuntime::IsRecoverySurfaceAllowed(const OUString& rSurface)
{
    return rSurface == u"writer"_ustr || rSurface == u"calc"_ustr
           || rSurface == u"impress"_ustr || rSurface == u"companion"_ustr;
}

bool AIChatDistributionRecoveryRuntime::IsNextStepAllowed(const OUString& rStep)
{
    return rStep == u"retry"_ustr || rStep == u"choose-local-model"_ustr
           || rStep == u"reconnect-connector"_ustr || rStep == u"request-approval"_ustr
           || rStep == u"open-evidence"_ustr || rStep == u"rollback-preview"_ustr
           || rStep == u"open-help"_ustr || rStep == u"export-diagnostics"_ustr;
}

bool AIChatDistributionRecoveryRuntime::IsSmokeKindAllowed(const OUString& rSmokeKind)
{
    return rSmokeKind == u"installer-smoke"_ustr || rSmokeKind == u"update-prompt"_ustr
           || rSmokeKind == u"rollback-proof"_ustr;
}

bool AIChatDistributionRecoveryRuntime::IsDistributionPolicyShapeAllowed(
    const AIChatDistributionPolicy& rPolicy)
{
    if (!IsDistributionChannelOrderAllowed(rPolicy.FirstLaunchChannels)
        || !rPolicy.ArtifactSigningRequired || !rPolicy.ChecksumRequired
        || !rPolicy.NotarizationRequired || !rPolicy.OfflineInstallSupported
        || !rPolicy.NoPublicCloudRequired)
        return false;

    for (const AIChatDistributionChannel& rChannel : rPolicy.FirstLaunchChannels)
    {
        if (!IsPlatformArtifactAllowed(rChannel.Platform, rChannel.Artifact)
            || !rChannel.Primary || !rChannel.InstallerSmokeRequired
            || rChannel.DownloadToFirstPatchMaxMinutes != 5)
            return false;
    }
    return true;
}

bool AIChatDistributionRecoveryRuntime::IsUpdatePolicyShapeAllowed(
    const AIChatUpdatePolicy& rPolicy)
{
    return rPolicy.Mode == u"prompt-one-click"_ustr && rPolicy.PromptRequired
           && rPolicy.OneClick && rPolicy.Deferrable && !rPolicy.ForceUpdateAllowed
           && rPolicy.SelfHostServer == u"w8-update-server"_ustr && rPolicy.LanSupported
           && !rPolicy.PublicInternetRequired && rPolicy.RollbackRequired;
}

bool AIChatDistributionRecoveryRuntime::IsDistributionManifestShapeAllowed(
    const AIChatDistributionUpdateManifest& rManifest)
{
    return IsDistributionManifestIdAllowed(rManifest.ManifestId)
           && rManifest.SchemaVersion == u"v3-distribution-update/0.1"_ustr
           && IsTimestampAllowed(rManifest.CreatedAt)
           && IsDistributionPolicyShapeAllowed(rManifest.Distribution)
           && IsUpdatePolicyShapeAllowed(rManifest.Update)
           && rManifest.Onboarding.DownloadToFirstPatchMaxMinutes == 5
           && rManifest.Onboarding.RequiresStarterPack
           && rManifest.Onboarding.RequiresOnboardingFlow
           && HasDistributionBaseEvidence(rManifest.RequiredEvidence)
           && EvidenceAllowed(rManifest.RequiredEvidence, IsDistributionRequiredEvidenceAllowed)
           && HasValidEvidenceIds(rManifest.EvidenceIds) && rManifest.Gates.BlocksGA
           && rManifest.Gates.RequiresV2RegressionGreen
           && rManifest.Gates.RuntimeImplementation == u"metadata-runtime-active"_ustr
           && !rManifest.Gates.InstallerPackagingRuntimeActive
           && !rManifest.Gates.UpdateServerRuntimeActive
           && !rManifest.Gates.NetworkDownloadRuntimeActive
           && !rManifest.Gates.UpdaterDaemonActive
           && RefShapeAllowed(rManifest.TenantContextRef, u"tenant-context:"_ustr)
           && RefShapeAllowed(rManifest.PolicyContextRef, u"policy-context:"_ustr)
           && RefShapeAllowed(rManifest.AuditChainRef, u"audit-chain:"_ustr)
           && RefShapeAllowed(rManifest.EditionPolicyRef, u"edition-policy:"_ustr)
           && RefShapeAllowed(rManifest.I18nLocalePolicyRef, u"i18n-locale-policy:"_ustr)
           && RefShapeAllowed(rManifest.ManualDocsRef, u"manual-docs:"_ustr)
           && RefShapeAllowed(rManifest.NoEgressRef, u"localcloud-no-egress:"_ustr)
           && RefShapeAllowed(rManifest.ReleaseEvidenceRef, u"release-evidence:"_ustr)
           && AIChatTenantContextRuntime::IsHashReferenceAllowed(rManifest.HashReference);
}

bool AIChatDistributionRecoveryRuntime::IsUpdateRollbackSmokeRecordShapeAllowed(
    const AIChatDistributionUpdateManifest& rManifest,
    const AIChatUpdateRollbackSmokeRecord& rRecord)
{
    return rRecord.RecordId.startsWith(u"dus-"_ustr)
           && IsLowerHex(rRecord.RecordId.copy(4), 16)
           && rRecord.ManifestId == rManifest.ManifestId
           && IsPlatformArtifactAllowed(rRecord.Platform, rRecord.Artifact)
           && IsSmokeKindAllowed(rRecord.SmokeKind)
           && RefShapeAllowed(rRecord.ArtifactSignatureRef, u"artifact-signature:"_ustr)
           && RefShapeAllowed(rRecord.ChecksumRef, u"checksum:"_ustr)
           && RefShapeAllowed(rRecord.NotarizationRef, u"notarization:"_ustr)
           && RefShapeAllowed(rRecord.RollbackProofRef, u"rollback-proof:"_ustr)
           && rRecord.DownloadToFirstPatchMinutes == 5 && rRecord.PromptShown
           && rRecord.OneClickAvailable && rRecord.Deferrable && !rRecord.Forced
           && !rRecord.PublicInternetRequired && rRecord.LanSupported
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rRecord.EvidenceId)
           && RefShapeAllowed(rRecord.AuditLogRef, u"audit-log-entry:"_ustr);
}

bool AIChatDistributionRecoveryRuntime::IsErrorRecoveryPolicyShapeAllowed(
    const AIChatErrorRecoveryPolicy& rPolicy)
{
    return rPolicy.Presentation == u"inline-guidance"_ustr && rPolicy.NextStepRequired
           && rPolicy.EvidenceOpenable && !rPolicy.DeadEndAllowed
           && rPolicy.MainDocumentUnchangedUntilApply && rPolicy.HumanReadableCauseRequired;
}

bool AIChatDistributionRecoveryRuntime::IsErrorRecoveryScenarioOrderAllowed(
    const std::vector<AIChatErrorRecoveryScenario>& rScenarios)
{
    const std::vector<OUString> aKinds = ExpectedRecoveryKinds();
    const std::vector<OUString> aSurfaces = ExpectedRecoverySurfaces();
    if (rScenarios.size() != aKinds.size())
        return false;
    for (size_t i = 0; i < rScenarios.size(); ++i)
    {
        if (rScenarios[i].Kind != aKinds[i] || rScenarios[i].Surface != aSurfaces[i])
            return false;
    }
    return true;
}

bool AIChatDistributionRecoveryRuntime::IsErrorRecoveryUxManifestShapeAllowed(
    const AIChatErrorRecoveryUxManifest& rManifest)
{
    if (!IsErrorRecoveryManifestIdAllowed(rManifest.ManifestId)
        || rManifest.SchemaVersion != u"v3-error-recovery-ux/0.1"_ustr
        || !IsTimestampAllowed(rManifest.CreatedAt)
        || !IsErrorRecoveryPolicyShapeAllowed(rManifest.Policy)
        || !IsErrorRecoveryScenarioOrderAllowed(rManifest.Scenarios)
        || !HasRecoveryBaseEvidence(rManifest.RequiredEvidence)
        || !EvidenceAllowed(rManifest.RequiredEvidence, IsErrorRecoveryRequiredEvidenceAllowed)
        || !HasValidEvidenceIds(rManifest.EvidenceIds) || !rManifest.Gates.BlocksGA
        || !rManifest.Gates.RequiresV2RegressionGreen
        || rManifest.Gates.RuntimeImplementation != u"metadata-runtime-active"_ustr
        || rManifest.Gates.InlineGuidanceUiRuntimeActive
        || rManifest.Gates.DiagnosticsExporterRuntimeActive
        || rManifest.Gates.RemoteRecoveryServiceActive
        || rManifest.Gates.CrashReportRuntimeActive
        || rManifest.Gates.OsNotificationBridgeActive
        || !RefShapeAllowed(rManifest.TenantContextRef, u"tenant-context:"_ustr)
        || !RefShapeAllowed(rManifest.PolicyContextRef, u"policy-context:"_ustr)
        || !RefShapeAllowed(rManifest.AuditChainRef, u"audit-chain:"_ustr)
        || !RefShapeAllowed(rManifest.EditionPolicyRef, u"edition-policy:"_ustr)
        || !RefShapeAllowed(rManifest.I18nLocalePolicyRef, u"i18n-locale-policy:"_ustr)
        || !RefShapeAllowed(rManifest.ManualDocsRef, u"manual-docs:"_ustr)
        || !RefShapeAllowed(rManifest.NoEgressRef, u"localcloud-no-egress:"_ustr)
        || !RefShapeAllowed(rManifest.ReleaseEvidenceRef, u"release-evidence:"_ustr)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rManifest.HashReference))
        return false;

    for (const AIChatErrorRecoveryScenario& rScenario : rManifest.Scenarios)
    {
        if (!IsRecoveryKindAllowed(rScenario.Kind) || !IsRecoverySurfaceAllowed(rScenario.Surface)
            || rScenario.CauseToken != u"error."_ustr + rScenario.Kind
            || rScenario.NextSteps.size() < 2 || ContainsDuplicateString(rScenario.NextSteps)
            || !ContainsString(rScenario.NextSteps, u"open-evidence"_ustr)
            || !ContainsString(rScenario.NextSteps, u"export-diagnostics"_ustr)
            || !rScenario.EvidenceOpenable
            || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rScenario.EvidenceId)
            || !rScenario.DiagnosticsExportable || !rScenario.MainDocumentUnchanged
            || !rScenario.CanRetry || !rScenario.CanRollback || !rScenario.RequiresUserChoice)
            return false;
        for (const OUString& rNextStep : rScenario.NextSteps)
        {
            if (!IsNextStepAllowed(rNextStep))
                return false;
        }
    }
    return true;
}

bool AIChatDistributionRecoveryRuntime::IsRecoveryActionRecordShapeAllowed(
    const AIChatErrorRecoveryUxManifest& rManifest,
    const AIChatRecoveryActionRecord& rRecord)
{
    const AIChatErrorRecoveryScenario* pScenario = FindScenario(rManifest, rRecord.ScenarioKind);
    return rRecord.ActionId.startsWith(u"rxn-"_ustr)
           && IsLowerHex(rRecord.ActionId.copy(4), 16)
           && rRecord.ManifestId == rManifest.ManifestId && pScenario
           && rRecord.Surface == pScenario->Surface
           && ContainsString(pScenario->NextSteps, rRecord.ChosenStep)
           && rRecord.MainDocumentUnchanged && rRecord.RetryAvailable
           && rRecord.RollbackAvailable && rRecord.DiagnosticsExportable
           && rRecord.EvidenceOpenable && rRecord.RequiresUserChoice
           && !rRecord.DocumentMutationApplied
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rRecord.EvidenceId)
           && RefShapeAllowed(rRecord.AuditLogRef, u"audit-log-entry:"_ustr);
}

OUString AIChatDistributionRecoveryRuntime::MakeDistributionManifestId(
    const OUString& rCreatedAt)
{
    return u"dist-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(u"distribution-update:"_ustr
                                                         + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatDistributionRecoveryRuntime::MakeErrorRecoveryManifestId(
    const OUString& rCreatedAt)
{
    return u"errux-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(u"error-recovery-ux:"_ustr
                                                         + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatDistributionRecoveryRuntime::MakeDistributionHashReference(
    const AIChatDistributionUpdateManifest& rManifest)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rManifest.ManifestId + u":"_ustr + rManifest.CreatedAt + u":"_ustr
                 + rManifest.Update.Mode + u":"_ustr
                 + OUString::number(static_cast<sal_Int32>(
                       rManifest.Distribution.FirstLaunchChannels.size())));
}

OUString AIChatDistributionRecoveryRuntime::MakeErrorRecoveryHashReference(
    const AIChatErrorRecoveryUxManifest& rManifest)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rManifest.ManifestId + u":"_ustr + rManifest.CreatedAt + u":"_ustr
                 + rManifest.Policy.Presentation + u":"_ustr
                 + OUString::number(static_cast<sal_Int32>(rManifest.Scenarios.size())));
}

OUString AIChatDistributionRecoveryRuntime::MakeSmokeRecordId(
    const AIChatDistributionUpdateManifest& rManifest,
    const AIChatUpdateRollbackSmokeRecord& rRecord)
{
    return u"dus-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rManifest.ManifestId + u":"_ustr + rRecord.Platform + u":"_ustr
                 + rRecord.Artifact + u":"_ustr + rRecord.SmokeKind + u":"_ustr
                 + rRecord.EvidenceId)
                 .copy(0, 16);
}

OUString AIChatDistributionRecoveryRuntime::MakeRecoveryActionId(
    const AIChatErrorRecoveryUxManifest& rManifest,
    const AIChatRecoveryActionRecord& rRecord)
{
    return u"rxn-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rManifest.ManifestId + u":"_ustr + rRecord.ScenarioKind + u":"_ustr
                 + rRecord.ChosenStep + u":"_ustr + rRecord.EvidenceId)
                 .copy(0, 16);
}

AIChatDistributionRecoveryResult AIChatDistributionRecoveryRuntime::SaveDistributionUpdatePolicy(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatDistributionUpdateManifest& rManifest) const
{
    if (!ScopeAllowed(rContext, rScope, u"distribution-update"_ustr))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatDistributionUpdateManifest aManifest = rManifest;
    if (aManifest.ManifestId.isEmpty())
        aManifest.ManifestId = MakeDistributionManifestId(aManifest.CreatedAt);
    aManifest.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aManifest.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aManifest.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aManifest.EditionPolicyRef.isEmpty())
        aManifest.EditionPolicyRef = u"edition-policy:metadata-runtime-active"_ustr;
    if (aManifest.I18nLocalePolicyRef.isEmpty())
        aManifest.I18nLocalePolicyRef = u"i18n-locale-policy:metadata-runtime-active"_ustr;
    if (aManifest.ManualDocsRef.isEmpty())
        aManifest.ManualDocsRef = u"manual-docs:metadata-runtime-active"_ustr;
    if (aManifest.NoEgressRef.isEmpty())
        aManifest.NoEgressRef = u"localcloud-no-egress:default-deny"_ustr;
    if (aManifest.ReleaseEvidenceRef.isEmpty())
        aManifest.ReleaseEvidenceRef = u"release-evidence:ga-blocker"_ustr;
    if (aManifest.HashReference.isEmpty())
        aManifest.HashReference = MakeDistributionHashReference(aManifest);

    if (!IsDistributionManifestShapeAllowed(aManifest))
        return MakeDeniedResult(u"invalid-distribution-update-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"distribution-update-policy"_ustr,
        { aManifest.ManifestId, aManifest.SchemaVersion, aManifest.CreatedAt,
          u"channels=macos/DMG,windows/MSI,linux/AppImage,self-hosted/docker"_ustr,
          u"artifactSigningRequired=true"_ustr, u"checksumRequired=true"_ustr,
          u"notarizationRequired=true"_ustr, u"offlineInstallSupported=true"_ustr,
          u"noPublicCloudRequired=true"_ustr, aManifest.Update.Mode,
          u"promptRequired=true"_ustr, u"oneClick=true"_ustr, u"deferrable=true"_ustr,
          u"forceUpdateAllowed=false"_ustr, aManifest.Update.SelfHostServer,
          u"lanSupported=true"_ustr, u"publicInternetRequired=false"_ustr,
          u"rollbackRequired=true"_ustr,
          OUString::number(aManifest.Onboarding.DownloadToFirstPatchMaxMinutes),
          JoinFields(aManifest.RequiredEvidence), JoinFields(aManifest.EvidenceIds),
          aManifest.TenantContextRef, aManifest.PolicyContextRef, aManifest.AuditChainRef,
          aManifest.EditionPolicyRef, aManifest.I18nLocalePolicyRef, aManifest.ManualDocsRef,
          aManifest.NoEgressRef, aManifest.ReleaseEvidenceRef, aManifest.HashReference });
    if (!AppendUtf8Line(m_sDistributionRecoveryStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatDistributionRecoveryResult aResult;
    aResult.Success = true;
    aResult.DistributionManifest = aManifest;
    aResult.Message
        = u"distribution-update-policy-saved manifest-id="_ustr + aManifest.ManifestId
          + u" channels=macos/DMG,windows/MSI,linux/AppImage,self-hosted/docker"_ustr
          + u" artifactSigningRequired=true checksumRequired=true notarizationRequired=true"_ustr
          + u" offlineInstallSupported=true noPublicCloudRequired=true"_ustr
          + u" downloadToFirstPatchMaxMinutes=5 promptRequired=true oneClick=true"_ustr
          + u" deferrable=true forceUpdateAllowed=false selfHostServer=w8-update-server"_ustr
          + u" lanSupported=true publicInternetRequired=false rollbackRequired=true"_ustr
          + u" starterPackRequired=true onboardingFlowRequired=true"_ustr
          + u" edition-policy-ref="_ustr + aManifest.EditionPolicyRef
          + u" i18n-locale-policy-ref="_ustr + aManifest.I18nLocalePolicyRef
          + u" manual-docs-ref="_ustr + aManifest.ManualDocsRef
          + u" no-egress-ref="_ustr + aManifest.NoEgressRef
          + u" release-evidence-ref="_ustr + aManifest.ReleaseEvidenceRef
          + u" metadata-only=true runtimeImplementation=metadata-runtime-active"_ustr
          + u" installer-packaging-runtime=not-started update-server-runtime=not-started"_ustr
          + u" network-downloader-runtime=not-started updater-daemon=not-started"_ustr
          + u" storesDocumentContent=false publicEgress=false"_ustr;
    return aResult;
}

AIChatDistributionRecoveryResult AIChatDistributionRecoveryRuntime::RecordUpdateRollbackSmoke(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatDistributionUpdateManifest& rManifest,
    const AIChatUpdateRollbackSmokeRecord& rRecord) const
{
    AIChatDistributionRecoveryResult aBase
        = SaveDistributionUpdatePolicy(rContext, rScope, rManifest);
    if (!aBase.Success)
        return aBase;

    AIChatUpdateRollbackSmokeRecord aRecord = rRecord;
    aRecord.ManifestId = aBase.DistributionManifest.ManifestId;
    if (aRecord.RecordId.isEmpty())
        aRecord.RecordId = MakeSmokeRecordId(aBase.DistributionManifest, aRecord);
    if (aRecord.AuditLogRef.isEmpty())
        aRecord.AuditLogRef = u"audit-log-entry:"_ustr + aRecord.EvidenceId;

    if (!IsUpdateRollbackSmokeRecordShapeAllowed(aBase.DistributionManifest, aRecord))
        return MakeDeniedResult(u"invalid-update-rollback-smoke-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"update-rollback-smoke"_ustr,
        { aRecord.RecordId, aRecord.ManifestId, aRecord.Platform, aRecord.Artifact,
          aRecord.SmokeKind, aRecord.ArtifactSignatureRef, aRecord.ChecksumRef,
          aRecord.NotarizationRef, aRecord.RollbackProofRef,
          OUString::number(aRecord.DownloadToFirstPatchMinutes),
          u"promptShown=true"_ustr, u"oneClickAvailable=true"_ustr, u"deferrable=true"_ustr,
          u"forced=false"_ustr, u"publicInternetRequired=false"_ustr,
          u"lanSupported=true"_ustr, aRecord.EvidenceId, aRecord.AuditLogRef });
    if (!AppendUtf8Line(m_sDistributionRecoveryStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.SmokeRecord = aRecord;
    aBase.Message
        = u"distribution-update-smoke-recorded record-id="_ustr + aRecord.RecordId
          + u" manifest-id="_ustr + aRecord.ManifestId + u" platform="_ustr
          + aRecord.Platform + u" artifact="_ustr + aRecord.Artifact
          + u" smoke-kind="_ustr + aRecord.SmokeKind
          + u" artifact-signature-ref="_ustr + aRecord.ArtifactSignatureRef
          + u" checksum-ref="_ustr + aRecord.ChecksumRef
          + u" notarization-ref="_ustr + aRecord.NotarizationRef
          + u" rollback-proof-ref="_ustr + aRecord.RollbackProofRef
          + u" downloadToFirstPatchMinutes=5 promptShown=true oneClickAvailable=true"_ustr
          + u" deferrable=true forced=false publicInternetRequired=false lanSupported=true"_ustr
          + u" evidence-id="_ustr + aRecord.EvidenceId
          + u" audit-log-ref="_ustr + aRecord.AuditLogRef
          + u" metadata-only=true raw-installer-payload=false update-binary=false"_ustr
          + u" network-download=false installer-execution=false rollback-execution=false"_ustr;
    return aBase;
}

AIChatDistributionRecoveryResult AIChatDistributionRecoveryRuntime::SaveErrorRecoveryUxPolicy(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatErrorRecoveryUxManifest& rManifest) const
{
    if (!ScopeAllowed(rContext, rScope, u"error-recovery-ux"_ustr))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatErrorRecoveryUxManifest aManifest = rManifest;
    if (aManifest.ManifestId.isEmpty())
        aManifest.ManifestId = MakeErrorRecoveryManifestId(aManifest.CreatedAt);
    aManifest.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aManifest.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aManifest.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aManifest.EditionPolicyRef.isEmpty())
        aManifest.EditionPolicyRef = u"edition-policy:metadata-runtime-active"_ustr;
    if (aManifest.I18nLocalePolicyRef.isEmpty())
        aManifest.I18nLocalePolicyRef = u"i18n-locale-policy:metadata-runtime-active"_ustr;
    if (aManifest.ManualDocsRef.isEmpty())
        aManifest.ManualDocsRef = u"manual-docs:metadata-runtime-active"_ustr;
    if (aManifest.NoEgressRef.isEmpty())
        aManifest.NoEgressRef = u"localcloud-no-egress:default-deny"_ustr;
    if (aManifest.ReleaseEvidenceRef.isEmpty())
        aManifest.ReleaseEvidenceRef = u"release-evidence:ga-blocker"_ustr;
    if (aManifest.HashReference.isEmpty())
        aManifest.HashReference = MakeErrorRecoveryHashReference(aManifest);

    if (!IsErrorRecoveryUxManifestShapeAllowed(aManifest))
        return MakeDeniedResult(u"invalid-error-recovery-ux-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"error-recovery-ux-policy"_ustr,
        { aManifest.ManifestId, aManifest.SchemaVersion, aManifest.CreatedAt,
          aManifest.Policy.Presentation, u"nextStepRequired=true"_ustr,
          u"evidenceOpenable=true"_ustr, u"deadEndAllowed=false"_ustr,
          u"mainDocumentUnchangedUntilApply=true"_ustr,
          u"humanReadableCauseRequired=true"_ustr,
          OUString::number(static_cast<sal_Int32>(aManifest.Scenarios.size())),
          JoinFields(aManifest.RequiredEvidence), JoinFields(aManifest.EvidenceIds),
          aManifest.TenantContextRef, aManifest.PolicyContextRef, aManifest.AuditChainRef,
          aManifest.EditionPolicyRef, aManifest.I18nLocalePolicyRef, aManifest.ManualDocsRef,
          aManifest.NoEgressRef, aManifest.ReleaseEvidenceRef, aManifest.HashReference });
    if (!AppendUtf8Line(m_sDistributionRecoveryStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatDistributionRecoveryResult aResult;
    aResult.Success = true;
    aResult.RecoveryManifest = aManifest;
    aResult.Message
        = u"error-recovery-ux-policy-saved manifest-id="_ustr + aManifest.ManifestId
          + u" presentation=inline-guidance nextStepRequired=true evidenceOpenable=true"_ustr
          + u" deadEndAllowed=false mainDocumentUnchangedUntilApply=true"_ustr
          + u" humanReadableCauseRequired=true scenarios=4"_ustr
          + u" scenarioKinds=provider-timeout,connector-auth-expired,policy-denied,patch-apply-failed"_ustr
          + u" surfaces=writer,calc,companion,impress openableEvidence=true"_ustr
          + u" diagnosticsExportable=true retry=true rollback=true requiresUserChoice=true"_ustr
          + u" edition-policy-ref="_ustr + aManifest.EditionPolicyRef
          + u" i18n-locale-policy-ref="_ustr + aManifest.I18nLocalePolicyRef
          + u" manual-docs-ref="_ustr + aManifest.ManualDocsRef
          + u" no-egress-ref="_ustr + aManifest.NoEgressRef
          + u" release-evidence-ref="_ustr + aManifest.ReleaseEvidenceRef
          + u" metadata-only=true runtimeImplementation=metadata-runtime-active"_ustr
          + u" inline-guidance-ui-runtime=not-started diagnostics-exporter-runtime=not-started"_ustr
          + u" remote-recovery-service=not-started crash-reporter=not-started"_ustr
          + u" os-notification-bridge=not-started webview=false publicEgress=false"_ustr;
    return aResult;
}

AIChatDistributionRecoveryResult AIChatDistributionRecoveryRuntime::RecordRecoveryAction(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatErrorRecoveryUxManifest& rManifest,
    const AIChatRecoveryActionRecord& rRecord) const
{
    AIChatDistributionRecoveryResult aBase
        = SaveErrorRecoveryUxPolicy(rContext, rScope, rManifest);
    if (!aBase.Success)
        return aBase;

    AIChatRecoveryActionRecord aRecord = rRecord;
    aRecord.ManifestId = aBase.RecoveryManifest.ManifestId;
    if (aRecord.ActionId.isEmpty())
        aRecord.ActionId = MakeRecoveryActionId(aBase.RecoveryManifest, aRecord);
    if (aRecord.AuditLogRef.isEmpty())
        aRecord.AuditLogRef = u"audit-log-entry:"_ustr + aRecord.EvidenceId;

    if (!IsRecoveryActionRecordShapeAllowed(aBase.RecoveryManifest, aRecord))
        return MakeDeniedResult(u"invalid-recovery-action-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"recovery-action"_ustr,
        { aRecord.ActionId, aRecord.ManifestId, aRecord.ScenarioKind, aRecord.Surface,
          aRecord.ChosenStep, u"mainDocumentUnchanged=true"_ustr,
          u"retryAvailable=true"_ustr, u"rollbackAvailable=true"_ustr,
          u"diagnosticsExportable=true"_ustr, u"evidenceOpenable=true"_ustr,
          u"requiresUserChoice=true"_ustr, u"documentMutationApplied=false"_ustr,
          aRecord.EvidenceId, aRecord.AuditLogRef });
    if (!AppendUtf8Line(m_sDistributionRecoveryStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.RecoveryAction = aRecord;
    aBase.Message
        = u"recovery-action-recorded action-id="_ustr + aRecord.ActionId
          + u" manifest-id="_ustr + aRecord.ManifestId + u" scenario-kind="_ustr
          + aRecord.ScenarioKind + u" surface="_ustr + aRecord.Surface
          + u" chosen-step="_ustr + aRecord.ChosenStep
          + u" mainDocumentUnchanged=true retryAvailable=true rollbackAvailable=true"_ustr
          + u" diagnosticsExportable=true evidenceOpenable=true requiresUserChoice=true"_ustr
          + u" documentMutationApplied=false evidence-id="_ustr + aRecord.EvidenceId
          + u" audit-log-ref="_ustr + aRecord.AuditLogRef
          + u" metadata-only=true raw-error-payload=false main-document-write=false"_ustr
          + u" remote-recovery-service=not-started crash-reporter=not-started"_ustr;
    return aBase;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
