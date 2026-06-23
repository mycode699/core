/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: starter pack runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatStarterPackRuntime.hxx"

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
constexpr OUStringLiteral STARTER_PACK_DIR_NAME = u"kqoffice-v3-ai-starter-pack";
constexpr OUStringLiteral STARTER_PACK_FILE_NAME = u"starter-pack.tsv";
constexpr sal_Int32 STARTER_TEMPLATE_COUNT = 30;
constexpr sal_Int32 STARTER_SCENARIO_COUNT = 10;
constexpr sal_Int32 STARTER_PER_SURFACE_COUNT = 10;

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

bool IsSlugChar(sal_Unicode c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-';
}

bool IsSlug(const OUString& rValue)
{
    if (rValue.isEmpty())
        return false;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        if (!IsSlugChar(rValue[i]))
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
        if (!AIChatStarterPackRuntime::IsRequiredEvidenceAllowed(rEvidence))
            return false;
    }
    return true;
}

bool HasValidLocales(const std::vector<OUString>& rLocales)
{
    if (rLocales.size() < 2 || ContainsDuplicateString(rLocales))
        return false;
    for (const OUString& rLocale : rLocales)
    {
        if (!AIChatStarterPackRuntime::IsLocaleAllowed(rLocale))
            return false;
    }
    return ContainsString(rLocales, u"zh-CN"_ustr) && ContainsString(rLocales, u"en-US"_ustr);
}

bool ScopeAllowed(const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope)
{
    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    return aScopeResult.Success && rScope.TargetType == u"starter-pack"_ustr
           && rScope.Surface == u"starter-pack"_ustr;
}

AIChatStarterPackResult MakeDeniedResult(const OUString& rReason)
{
    AIChatStarterPackResult aResult;
    aResult.Message
        = u"starter-pack-denied reason="_ustr + rReason
          + u" fail-closed-user-visible=true metadata-only=true local-first=true"_ustr
          + u" storesDocumentContent=false raw-template-body=false raw-document=false"_ustr
          + u" publicEgress=false requiresNetwork=false cdn-fetch=false"_ustr
          + u" template-binary-storage=false installer-wiring-runtime=not-started"_ustr
          + u" sample-open-ui-runtime=not-started webview=false standalone-gallery=false"_ustr;
    return aResult;
}

OUString ExpectedExtensionForSurface(const OUString& rSurface)
{
    if (rSurface == u"writer"_ustr)
        return u".ott"_ustr;
    if (rSurface == u"calc"_ustr)
        return u".ots"_ustr;
    if (rSurface == u"impress"_ustr)
        return u".otp"_ustr;
    return OUString();
}

OUString PathPrefixForSurface(const OUString& rSurface)
{
    return u"templates/v3-starter-pack/"_ustr + rSurface + u"/"_ustr;
}

sal_Int32 CountSurface(const std::vector<AIChatStarterPackTemplate>& rTemplates,
                       const OUString& rSurface)
{
    sal_Int32 nCount = 0;
    for (const AIChatStarterPackTemplate& rTemplate : rTemplates)
    {
        if (rTemplate.Surface == rSurface)
            ++nCount;
    }
    return nCount;
}

sal_Int32 CountScenario(const std::vector<AIChatStarterPackTemplate>& rTemplates,
                        const OUString& rScenario)
{
    sal_Int32 nCount = 0;
    for (const AIChatStarterPackTemplate& rTemplate : rTemplates)
    {
        if (rTemplate.Scenario == rScenario)
            ++nCount;
    }
    return nCount;
}

bool HasUniqueTemplateIdsAndPaths(const std::vector<AIChatStarterPackTemplate>& rTemplates)
{
    std::vector<OUString> aIds;
    std::vector<OUString> aPaths;
    for (const AIChatStarterPackTemplate& rTemplate : rTemplates)
    {
        aIds.push_back(rTemplate.TemplateId);
        aPaths.push_back(rTemplate.Path);
    }
    return !ContainsDuplicateString(aIds) && !ContainsDuplicateString(aPaths);
}

std::vector<OUString> ExpectedScenarios()
{
    return { u"meeting-notes"_ustr, u"okr"_ustr, u"prd"_ustr, u"weekly-report"_ustr,
             u"contract-brief"_ustr, u"budget"_ustr, u"sales-dashboard"_ustr,
             u"project-gantt"_ustr, u"roadshow"_ustr, u"retrospective"_ustr };
}
}

AIChatStarterPackRuntime::AIChatStarterPackRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + STARTER_PACK_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sStarterPackStoreUrl = m_sStorageRootUrl + u"/"_ustr + STARTER_PACK_FILE_NAME;
}

bool AIChatStarterPackRuntime::IsManifestIdAllowed(const OUString& rManifestId)
{
    return rManifestId.startsWith(u"spm-"_ustr) && IsLowerHex(rManifestId.copy(4), 16);
}

bool AIChatStarterPackRuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return AIChatAuditLogRuntime::IsTimestampAllowed(rTimestamp);
}

bool AIChatStarterPackRuntime::IsPackNameAllowed(const OUString& rPackName)
{
    return rPackName == u"v3-starter-pack"_ustr;
}

bool AIChatStarterPackRuntime::IsDistributionAllowed(const OUString& rDistribution)
{
    return rDistribution == u"embedded"_ustr || rDistribution == u"self-hosted-lan"_ustr;
}

bool AIChatStarterPackRuntime::IsTemplateRootAllowed(const OUString& rTemplateRoot)
{
    return rTemplateRoot == u"templates/v3-starter-pack/"_ustr;
}

bool AIChatStarterPackRuntime::IsLocaleStrategyAllowed(const OUString& rLocaleStrategy)
{
    return rLocaleStrategy == u"ui-locale-first"_ustr
           || rLocaleStrategy == u"bilingual-zh-en"_ustr;
}

bool AIChatStarterPackRuntime::IsSurfaceAllowed(const OUString& rSurface)
{
    return rSurface == u"writer"_ustr || rSurface == u"calc"_ustr || rSurface == u"impress"_ustr;
}

bool AIChatStarterPackRuntime::IsScenarioAllowed(const OUString& rScenario)
{
    const std::vector<OUString> aScenarios = ExpectedScenarios();
    return ContainsString(aScenarios, rScenario);
}

bool AIChatStarterPackRuntime::IsTemplateIdAllowed(const OUString& rTemplateId,
                                                   const OUString& rSurface)
{
    const OUString sPrefix = u"tpl-"_ustr + rSurface + u"-"_ustr;
    return IsSurfaceAllowed(rSurface)
           && rTemplateId.startsWith(sPrefix)
           && IsSlug(rTemplateId.copy(sPrefix.getLength()));
}

bool AIChatStarterPackRuntime::IsTemplatePathAllowed(const OUString& rPath,
                                                     const OUString& rSurface)
{
    return IsSurfaceAllowed(rSurface) && rPath.startsWith(PathPrefixForSurface(rSurface))
           && rPath.endsWith(ExpectedExtensionForSurface(rSurface));
}

bool AIChatStarterPackRuntime::IsTitleKeyAllowed(const OUString& rTitleKey)
{
    return rTitleKey.startsWith(u"starter-pack."_ustr) && IsSlug(rTitleKey.copy(13));
}

bool AIChatStarterPackRuntime::IsLocaleAllowed(const OUString& rLocale)
{
    return rLocale == u"zh-CN"_ustr || rLocale == u"en-US"_ustr
           || rLocale == u"ja-JP"_ustr || rLocale == u"zh-TW"_ustr;
}

bool AIChatStarterPackRuntime::IsActionKindAllowedForSurface(const OUString& rSurface,
                                                             const OUString& rActionKind)
{
    return (rSurface == u"writer"_ustr && rActionKind == u"ParagraphAction"_ustr)
           || (rSurface == u"calc"_ustr && rActionKind == u"CellAction"_ustr)
           || (rSurface == u"impress"_ustr && rActionKind == u"SlideElementAction"_ustr);
}

bool AIChatStarterPackRuntime::IsRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"starter-pack-manifest"_ustr
           || rEvidence == u"template-install"_ustr
           || rEvidence == u"sample-patch-result"_ustr
           || rEvidence == u"sample-open-smoke"_ustr || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"v2-regression-green"_ustr
           || rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr;
}

bool AIChatStarterPackRuntime::IsOpenTargetRefAllowed(const OUString& rOpenTargetRef)
{
    return rOpenTargetRef.startsWith(u"template-open:"_ustr)
           || rOpenTargetRef.startsWith(u"native-template-manager:"_ustr);
}

bool AIChatStarterPackRuntime::IsSamplePatchShapeAllowed(
    const OUString& rSurface, const AIChatStarterPackSamplePatch& rSamplePatch)
{
    return rSamplePatch.Required
           && IsActionKindAllowedForSurface(rSurface, rSamplePatch.ActionKind)
           && rSamplePatch.MustSucceed && rSamplePatch.RequiresUndo
           && rSamplePatch.EvidenceRequired
           && rSamplePatch.ApplyPlanRef.startsWith(u"aprt-"_ustr)
           && rSamplePatch.DiffReviewRef.startsWith(u"diff-review:"_ustr)
           && rSamplePatch.ApprovalRef.startsWith(u"approval:"_ustr)
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rSamplePatch.EvidenceId);
}

bool AIChatStarterPackRuntime::IsDataBoundaryAllowed(
    const AIChatStarterPackDataBoundary& rBoundary)
{
    return !rBoundary.StoresDocumentContent && !rBoundary.PublicEgress && rBoundary.LocalFirst
           && rBoundary.HashOnly;
}

bool AIChatStarterPackRuntime::IsTemplateShapeAllowed(
    const AIChatStarterPackTemplate& rTemplate)
{
    return IsSurfaceAllowed(rTemplate.Surface) && IsScenarioAllowed(rTemplate.Scenario)
           && IsTemplateIdAllowed(rTemplate.TemplateId, rTemplate.Surface)
           && IsTemplatePathAllowed(rTemplate.Path, rTemplate.Surface)
           && IsTitleKeyAllowed(rTemplate.TitleKey) && HasValidLocales(rTemplate.LocaleReady)
           && AIChatTenantContextRuntime::IsHashReferenceAllowed(rTemplate.HashReference)
           && IsSamplePatchShapeAllowed(rTemplate.Surface, rTemplate.SamplePatch)
           && IsDataBoundaryAllowed(rTemplate.DataBoundary);
}

bool AIChatStarterPackRuntime::IsCoverageShapeAllowed(
    const AIChatStarterPackCoverage& rCoverage,
    const std::vector<AIChatStarterPackTemplate>& rTemplates)
{
    if (rCoverage.TemplateCount != STARTER_TEMPLATE_COUNT
        || rCoverage.BusinessScenarioCount != STARTER_SCENARIO_COUNT
        || rCoverage.WriterCount != STARTER_PER_SURFACE_COUNT
        || rCoverage.CalcCount != STARTER_PER_SURFACE_COUNT
        || rCoverage.ImpressCount != STARTER_PER_SURFACE_COUNT || !rCoverage.PatchSmokeRequired
        || static_cast<sal_Int32>(rTemplates.size()) != STARTER_TEMPLATE_COUNT
        || !HasUniqueTemplateIdsAndPaths(rTemplates))
        return false;

    if (CountSurface(rTemplates, u"writer"_ustr) != STARTER_PER_SURFACE_COUNT
        || CountSurface(rTemplates, u"calc"_ustr) != STARTER_PER_SURFACE_COUNT
        || CountSurface(rTemplates, u"impress"_ustr) != STARTER_PER_SURFACE_COUNT)
        return false;

    const std::vector<OUString> aScenarios = ExpectedScenarios();
    for (const OUString& rScenario : aScenarios)
    {
        if (CountScenario(rTemplates, rScenario) != 3)
            return false;
    }
    return true;
}

bool AIChatStarterPackRuntime::IsInstallationShapeAllowed(
    const AIChatStarterPackInstallation& rInstallation)
{
    return rInstallation.Installable && !rInstallation.RequiresNetwork
           && rInstallation.W8SelfHostedCompatible
           && IsDistributionAllowed(rInstallation.Distribution)
           && rInstallation.EmbeddedDefault
           && IsTemplateRootAllowed(rInstallation.TemplateRoot)
           && IsLocaleStrategyAllowed(rInstallation.LocaleStrategy)
           && rInstallation.BundleLocationRef.startsWith(u"local-bundle:"_ustr);
}

bool AIChatStarterPackRuntime::IsGateShapeAllowed(const AIChatStarterPackGateState& rGates)
{
    return rGates.BlocksGA && rGates.RequiresV2RegressionGreen
           && rGates.RuntimeImplementation == u"metadata-runtime-active"_ustr
           && !rGates.TemplateAssetsInstalled && !rGates.InstallerWiringComplete
           && rGates.SampleOpenSmokeRequired;
}

bool AIChatStarterPackRuntime::IsManifestShapeAllowed(
    const AIChatStarterPackManifest& rManifest)
{
    if (!IsManifestIdAllowed(rManifest.ManifestId)
        || rManifest.SchemaVersion != u"v3-starter-pack-manifest/0.1"_ustr
        || !IsTimestampAllowed(rManifest.CreatedAt) || !IsPackNameAllowed(rManifest.PackName)
        || !IsCoverageShapeAllowed(rManifest.Coverage, rManifest.Templates)
        || !IsInstallationShapeAllowed(rManifest.Installation)
        || !ContainsString(rManifest.RequiredEvidence, u"starter-pack-manifest"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"template-install"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"sample-patch-result"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"sample-open-smoke"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"evidence-record"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"v2-regression-green"_ustr)
        || !HasOnlyAllowedEvidence(rManifest.RequiredEvidence)
        || !HasValidEvidenceIds(rManifest.EvidenceIds) || !IsGateShapeAllowed(rManifest.Gates)
        || !rManifest.TenantContextRef.startsWith(u"tenant-context:"_ustr)
        || !rManifest.PolicyContextRef.startsWith(u"policy-context:"_ustr)
        || !rManifest.AuditChainRef.startsWith(u"audit-chain:"_ustr)
        || !rManifest.OnboardingDemoRef.startsWith(u"onboarding-demo:"_ustr)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rManifest.ManifestHashReference))
        return false;

    for (const AIChatStarterPackTemplate& rTemplate : rManifest.Templates)
    {
        if (!IsTemplateShapeAllowed(rTemplate))
            return false;
    }
    return true;
}

bool AIChatStarterPackRuntime::IsSmokeShapeAllowed(const AIChatStarterPackSmoke& rSmoke)
{
    return rSmoke.SmokeId.startsWith(u"sps-"_ustr)
           && IsManifestIdAllowed(rSmoke.ManifestId)
           && IsSurfaceAllowed(rSmoke.Surface) && IsScenarioAllowed(rSmoke.Scenario)
           && IsTemplateIdAllowed(rSmoke.TemplateId, rSmoke.Surface)
           && IsOpenTargetRefAllowed(rSmoke.OpenTargetRef) && rSmoke.SampleOpenSucceeded
           && rSmoke.PatchSmokeSucceeded && rSmoke.UndoSucceeded && rSmoke.EvidenceRequired
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rSmoke.EvidenceId)
           && rSmoke.AuditLogRef.startsWith(u"audit-log-entry:"_ustr)
           && !rSmoke.StoresDocumentContent && !rSmoke.PublicEgress;
}

OUString AIChatStarterPackRuntime::MakeManifestId(const OUString& rCreatedAt)
{
    return u"spm-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(u"starter-pack:"_ustr + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatStarterPackRuntime::MakeTemplateHashReference(
    const AIChatStarterPackTemplate& rTemplate)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rTemplate.TemplateId + u":"_ustr + rTemplate.Surface + u":"_ustr
                 + rTemplate.Scenario + u":"_ustr + rTemplate.Path);
}

OUString AIChatStarterPackRuntime::MakeManifestHashReference(
    const AIChatStarterPackManifest& rManifest)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rManifest.ManifestId + u":"_ustr + rManifest.CreatedAt + u":"_ustr
                 + rManifest.PackName + u":"_ustr
                 + OUString::number(static_cast<sal_Int32>(rManifest.Templates.size())));
}

OUString AIChatStarterPackRuntime::MakeSmokeId(
    const AIChatStarterPackManifest& rManifest, const AIChatStarterPackTemplate& rTemplate,
    const OUString& rEvidenceId)
{
    return u"sps-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rManifest.ManifestId + u":"_ustr + rTemplate.TemplateId + u":"_ustr
                 + rEvidenceId)
                 .copy(0, 16);
}

AIChatStarterPackResult AIChatStarterPackRuntime::RegisterManifest(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatStarterPackManifest& rManifest) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatStarterPackManifest aManifest = rManifest;
    if (aManifest.ManifestId.isEmpty())
        aManifest.ManifestId = MakeManifestId(aManifest.CreatedAt);
    aManifest.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aManifest.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aManifest.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aManifest.OnboardingDemoRef.isEmpty())
        aManifest.OnboardingDemoRef = u"onboarding-demo:starter-pack"_ustr;
    if (aManifest.ManifestHashReference.isEmpty())
        aManifest.ManifestHashReference = MakeManifestHashReference(aManifest);

    for (AIChatStarterPackTemplate& rTemplate : aManifest.Templates)
    {
        if (rTemplate.HashReference.isEmpty())
            rTemplate.HashReference = MakeTemplateHashReference(rTemplate);
    }

    if (!IsManifestShapeAllowed(aManifest))
        return MakeDeniedResult(u"invalid-starter-pack-manifest-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"starter-pack-manifest"_ustr,
        { aManifest.ManifestId, aManifest.SchemaVersion, aManifest.CreatedAt,
          aManifest.PackName, aManifest.Installation.Distribution,
          aManifest.Installation.TemplateRoot, aManifest.Installation.LocaleStrategy,
          aManifest.Installation.BundleLocationRef,
          OUString::number(aManifest.Coverage.TemplateCount),
          OUString::number(aManifest.Coverage.BusinessScenarioCount),
          OUString::number(aManifest.Coverage.WriterCount),
          OUString::number(aManifest.Coverage.CalcCount),
          OUString::number(aManifest.Coverage.ImpressCount),
          JoinFields(aManifest.RequiredEvidence), JoinFields(aManifest.EvidenceIds),
          aManifest.TenantContextRef, aManifest.PolicyContextRef, aManifest.AuditChainRef,
          aManifest.OnboardingDemoRef, aManifest.ManifestHashReference });
    if (!AppendUtf8Line(m_sStarterPackStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatStarterPackResult aResult;
    aResult.Success = true;
    aResult.Manifest = aManifest;
    aResult.Message
        = u"starter-pack-manifest-registered manifest-id="_ustr + aManifest.ManifestId
          + u" pack=v3-starter-pack templates=30 scenarios=10 writer=10 calc=10 impress=10"_ustr
          + u" patchSmokeRequired=true sampleOpenSmokeRequired=true"_ustr
          + u" distribution="_ustr + aManifest.Installation.Distribution
          + u" embeddedDefault=true localBundleRef="_ustr + aManifest.Installation.BundleLocationRef
          + u" requiresNetwork=false publicEgress=false w8SelfHostedCompatible=true"_ustr
          + u" metadata-only=true storesDocumentContent=false hashOnly=true"_ustr
          + u" onboarding-demo-ref="_ustr + aManifest.OnboardingDemoRef
          + u" manifest-hash-ref="_ustr + aManifest.ManifestHashReference
          + u" runtimeImplementation=metadata-runtime-active native-sfx2-metadata=true"_ustr
          + u" template-assets-installed=false installer-wiring-runtime=not-started"_ustr
          + u" sample-open-ui-runtime=not-started template-gallery-ui=not-started"_ustr
          + u" cdn-fetch=false model-download=false connector-writeback=false webview=false"_ustr;
    return aResult;
}

AIChatStarterPackResult AIChatStarterPackRuntime::RecordTemplateInstall(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatStarterPackManifest& rManifest, const AIChatStarterPackTemplate& rTemplate,
    const OUString& rEvidenceId) const
{
    AIChatStarterPackResult aBase = RegisterManifest(rContext, rScope, rManifest);
    if (!aBase.Success)
        return aBase;
    if (!AIChatTenantContextRuntime::IsEvidenceIdAllowed(rEvidenceId)
        || !IsTemplateShapeAllowed(rTemplate))
        return MakeDeniedResult(u"invalid-template-install-evidence"_ustr);
    if (!ContainsString(aBase.Manifest.RequiredEvidence, u"template-install"_ustr))
        return MakeDeniedResult(u"template-install-evidence-missing"_ustr);

    const OUString sLine = MakeStoreLine(
        u"template-install"_ustr,
        { aBase.Manifest.ManifestId, rTemplate.TemplateId, rTemplate.Surface, rTemplate.Scenario,
          rTemplate.Path, rTemplate.TitleKey, rTemplate.HashReference, rEvidenceId,
          rTemplate.SamplePatch.ActionKind, rTemplate.SamplePatch.EvidenceId,
          aBase.Manifest.Installation.BundleLocationRef });
    if (!AppendUtf8Line(m_sStarterPackStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.Message
        = u"starter-pack-template-install-recorded manifest-id="_ustr + aBase.Manifest.ManifestId
          + u" template-id="_ustr + rTemplate.TemplateId + u" surface="_ustr + rTemplate.Surface
          + u" scenario="_ustr + rTemplate.Scenario + u" evidence-id="_ustr + rEvidenceId
          + u" localBundleRef="_ustr + aBase.Manifest.Installation.BundleLocationRef
          + u" metadata-only=true template-binary-storage=false requiresNetwork=false"_ustr
          + u" publicEgress=false storesDocumentContent=false samplePatchRequired=true"_ustr
          + u" samplePatchMustSucceed=true requiresUndo=true evidenceRequired=true"_ustr;
    return aBase;
}

AIChatStarterPackResult AIChatStarterPackRuntime::RecordSampleOpenSmoke(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatStarterPackManifest& rManifest, const AIChatStarterPackSmoke& rSmoke) const
{
    AIChatStarterPackResult aBase = RegisterManifest(rContext, rScope, rManifest);
    if (!aBase.Success)
        return aBase;

    AIChatStarterPackSmoke aSmoke = rSmoke;
    if (aSmoke.SmokeId.isEmpty())
    {
        for (const AIChatStarterPackTemplate& rTemplate : aBase.Manifest.Templates)
        {
            if (rTemplate.TemplateId == aSmoke.TemplateId)
            {
                aSmoke.SmokeId = MakeSmokeId(aBase.Manifest, rTemplate, aSmoke.EvidenceId);
                break;
            }
        }
    }
    aSmoke.ManifestId = aBase.Manifest.ManifestId;
    if (aSmoke.AuditLogRef.isEmpty())
        aSmoke.AuditLogRef = u"audit-log-entry:"_ustr + aSmoke.EvidenceId;

    if (!IsSmokeShapeAllowed(aSmoke))
        return MakeDeniedResult(u"invalid-sample-open-smoke"_ustr);
    if (!ContainsString(aBase.Manifest.RequiredEvidence, u"sample-open-smoke"_ustr)
        || !ContainsString(aBase.Manifest.RequiredEvidence, u"sample-patch-result"_ustr))
        return MakeDeniedResult(u"sample-smoke-evidence-missing"_ustr);

    const OUString sLine = MakeStoreLine(
        u"sample-open-smoke"_ustr,
        { aSmoke.SmokeId, aSmoke.ManifestId, aSmoke.TemplateId, aSmoke.Surface,
          aSmoke.Scenario, aSmoke.OpenTargetRef, aSmoke.EvidenceId, aSmoke.AuditLogRef,
          u"sampleOpenSucceeded=true"_ustr, u"patchSmokeSucceeded=true"_ustr,
          u"undoSucceeded=true"_ustr });
    if (!AppendUtf8Line(m_sStarterPackStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.Smoke = aSmoke;
    aBase.Message
        = u"starter-pack-sample-open-smoke-recorded smoke-id="_ustr + aSmoke.SmokeId
          + u" manifest-id="_ustr + aSmoke.ManifestId + u" template-id="_ustr
          + aSmoke.TemplateId + u" surface="_ustr + aSmoke.Surface + u" scenario="_ustr
          + aSmoke.Scenario
          + u" sampleOpenSucceeded=true patchSmokeSucceeded=true undoSucceeded=true"_ustr
          + u" evidenceRequired=true evidence-id="_ustr + aSmoke.EvidenceId
          + u" audit-log-ref="_ustr + aSmoke.AuditLogRef
          + u" no-main-document-mutation-before-approval=true metadata-only=true"_ustr
          + u" storesDocumentContent=false publicEgress=false requiresNetwork=false"_ustr;
    return aBase;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
