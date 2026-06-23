/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: i18n/manual runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatI18nManualRuntime.hxx"

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
constexpr OUStringLiteral I18N_MANUAL_DIR_NAME = u"kqoffice-v3-ai-i18n-manual";
constexpr OUStringLiteral I18N_MANUAL_FILE_NAME = u"i18n-manual.tsv";
constexpr OUStringLiteral LANG_COMMAND_PATTERN
    = u"^/lang (zh-CN|en-US|ja-JP|zh-TW|zh|en|ja)$";

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

std::vector<OUString> LaunchLocales()
{
    return { u"zh-CN"_ustr, u"en-US"_ustr, u"ja-JP"_ustr, u"zh-TW"_ustr };
}

std::vector<OUString> RequiredManualLocales()
{
    return { u"zh-CN"_ustr, u"en-US"_ustr };
}

std::vector<OUString> FallbackManualLocales()
{
    return { u"ja-JP"_ustr, u"zh-TW"_ustr };
}

std::vector<OUString> ManualTopics()
{
    return { u"index"_ustr,       u"quickstart"_ustr, u"ai-features"_ustr,
             u"connectors"_ustr,  u"tenant-admin"_ustr, u"companion"_ustr,
             u"localcloud"_ustr,  u"troubleshooting"_ustr };
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

bool ScopeAllowed(const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope)
{
    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    return aScopeResult.Success && rScope.TargetType == u"i18n-manual"_ustr
           && rScope.Surface == u"i18n-manual"_ustr;
}

AIChatI18nManualResult MakeDeniedResult(const OUString& rReason)
{
    AIChatI18nManualResult aResult;
    aResult.Message
        = u"i18n-manual-denied reason="_ustr + rReason
          + u" fail-closed-user-visible=true metadata-only=true"_ustr
          + u" uiFollowsSystemLocale=true aiOutputMatchesUiLocale=true"_ustr
          + u" silentSwitchAllowed=false persistsWithoutUserAction=false"_ustr
          + u" manualEmbedded=true offlineReadable=true requiresPublicInternet=false"_ustr
          + u" storesDocumentContent=false publicEgress=false raw-manual-body=false"_ustr
          + u" help-viewer-runtime=not-started online-mirror-sync-runtime=not-started"_ustr
          + u" remote-docs-service=not-started cloud-translation=not-started webview=false"_ustr;
    return aResult;
}
}

AIChatI18nManualRuntime::AIChatI18nManualRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + I18N_MANUAL_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sI18nManualStoreUrl = m_sStorageRootUrl + u"/"_ustr + I18N_MANUAL_FILE_NAME;
}

bool AIChatI18nManualRuntime::IsLocalePolicyIdAllowed(const OUString& rPolicyId)
{
    return rPolicyId.startsWith(u"i18n-"_ustr) && IsLowerHex(rPolicyId.copy(5), 16);
}

bool AIChatI18nManualRuntime::IsManualManifestIdAllowed(const OUString& rManifestId)
{
    return rManifestId.startsWith(u"manual-"_ustr) && IsLowerHex(rManifestId.copy(7), 16);
}

bool AIChatI18nManualRuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return AIChatAuditLogRuntime::IsTimestampAllowed(rTimestamp);
}

bool AIChatI18nManualRuntime::IsLocaleAllowed(const OUString& rLocale)
{
    return rLocale == u"zh-CN"_ustr || rLocale == u"en-US"_ustr
           || rLocale == u"ja-JP"_ustr || rLocale == u"zh-TW"_ustr;
}

bool AIChatI18nManualRuntime::IsLaunchLocaleOrderAllowed(
    const std::vector<OUString>& rLocales)
{
    return rLocales == LaunchLocales();
}

bool AIChatI18nManualRuntime::IsRequiredManualLocalesAllowed(
    const std::vector<OUString>& rLocales)
{
    return rLocales == RequiredManualLocales();
}

bool AIChatI18nManualRuntime::IsAiOutputLanguageAllowed(const OUString& rLocale,
                                                        const OUString& rOutputLanguage)
{
    return (rLocale == u"zh-CN"_ustr && rOutputLanguage == u"Chinese"_ustr)
           || (rLocale == u"en-US"_ustr && rOutputLanguage == u"English"_ustr)
           || (rLocale == u"ja-JP"_ustr && rOutputLanguage == u"Japanese"_ustr)
           || (rLocale == u"zh-TW"_ustr && rOutputLanguage == u"Traditional Chinese"_ustr);
}

bool AIChatI18nManualRuntime::IsI18nRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"i18n-locale-policy"_ustr
           || rEvidence == u"locale-selection"_ustr
           || rEvidence == u"language-override"_ustr
           || rEvidence == u"ai-output-language"_ustr
           || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"edition-policy"_ustr || rEvidence == u"localcloud-no-egress"_ustr;
}

bool AIChatI18nManualRuntime::IsManualRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"manual-docs-manifest"_ustr
           || rEvidence == u"embedded-help-open"_ustr
           || rEvidence == u"online-mirror-sync"_ustr
           || rEvidence == u"locale-coverage"_ustr
           || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"v2-regression-green"_ustr
           || rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"edition-policy"_ustr || rEvidence == u"localcloud-no-egress"_ustr;
}

bool AIChatI18nManualRuntime::IsManualTopicIdAllowed(const OUString& rTopicId)
{
    return ContainsString(ManualTopics(), rTopicId);
}

bool AIChatI18nManualRuntime::IsManualTopicOrderAllowed(
    const std::vector<AIChatManualTopic>& rTopics)
{
    const std::vector<OUString> aTopics = ManualTopics();
    if (rTopics.size() != aTopics.size())
        return false;
    for (size_t i = 0; i < rTopics.size(); ++i)
    {
        if (rTopics[i].TopicId != aTopics[i])
            return false;
    }
    return true;
}

bool AIChatI18nManualRuntime::IsManualPathAllowed(const OUString& rTopicId,
                                                  const OUString& rLocale,
                                                  const OUString& rPath)
{
    if (!IsManualTopicIdAllowed(rTopicId) || !ContainsString(RequiredManualLocales(), rLocale))
        return false;
    return rPath == u"docs/manual/"_ustr + rLocale + u"/"_ustr + rTopicId + u".md"_ustr;
}

bool AIChatI18nManualRuntime::IsLocalePolicyShapeAllowed(
    const AIChatI18nLocalePolicy& rPolicy)
{
    if (!IsLocalePolicyIdAllowed(rPolicy.PolicyId)
        || rPolicy.SchemaVersion != u"v3-i18n-locale-policy/0.1"_ustr
        || !IsTimestampAllowed(rPolicy.CreatedAt)
        || !IsLocaleAllowed(rPolicy.Locale.OsLocale)
        || rPolicy.Locale.OsLocale != rPolicy.Locale.UiLocale
        || !IsLaunchLocaleOrderAllowed(rPolicy.Locale.SupportedLaunchLocales)
        || rPolicy.Locale.FallbackLocale != u"en-US"_ustr || !rPolicy.Ui.FollowsSystemLocale
        || !rPolicy.Ui.UsesExistingI18nPool || rPolicy.Ui.SilentSwitchAllowed
        || rPolicy.AiOutput.DefaultLocale != rPolicy.Locale.UiLocale
        || !rPolicy.AiOutput.MatchesUiLocale
        || !rPolicy.AiOutput.InlineOverride.Enabled
        || rPolicy.AiOutput.InlineOverride.CommandPattern != LANG_COMMAND_PATTERN
        || !rPolicy.AiOutput.InlineOverride.ExplicitOnly
        || rPolicy.AiOutput.InlineOverride.PersistsWithoutUserAction
        || !IsAiOutputLanguageAllowed(rPolicy.Locale.UiLocale, rPolicy.AiOutput.OutputLanguage)
        || !rPolicy.AiOutput.EvidenceRequired || !rPolicy.Manual.Embedded
        || !rPolicy.Manual.OnlineMirror
        || !IsRequiredManualLocalesAllowed(rPolicy.Manual.RequiredLocales)
        || !ContainsString(rPolicy.RequiredEvidence, u"i18n-locale-policy"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"locale-selection"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"language-override"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"ai-output-language"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"evidence-record"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"policy-decision"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"audit-log-entry"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"edition-policy"_ustr)
        || !ContainsString(rPolicy.RequiredEvidence, u"localcloud-no-egress"_ustr)
        || !EvidenceAllowed(rPolicy.RequiredEvidence, IsI18nRequiredEvidenceAllowed)
        || !HasValidEvidenceIds(rPolicy.EvidenceIds) || !rPolicy.Gates.BlocksGA
        || !rPolicy.Gates.RequiresV2RegressionGreen
        || rPolicy.Gates.RuntimeImplementation != u"metadata-runtime-active"_ustr
        || rPolicy.Gates.I18nPoolRuntimeActive || rPolicy.Gates.PromptLanguageRuntimeActive
        || rPolicy.Gates.CloudTranslationActive
        || !rPolicy.TenantContextRef.startsWith(u"tenant-context:"_ustr)
        || !rPolicy.PolicyContextRef.startsWith(u"policy-context:"_ustr)
        || !rPolicy.AuditChainRef.startsWith(u"audit-chain:"_ustr)
        || !rPolicy.EditionPolicyRef.startsWith(u"edition-policy:"_ustr)
        || !rPolicy.NoEgressRef.startsWith(u"localcloud-no-egress:"_ustr)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rPolicy.HashReference))
        return false;
    return true;
}

bool AIChatI18nManualRuntime::IsManualManifestShapeAllowed(
    const AIChatManualDocsManifest& rManifest)
{
    if (!IsManualManifestIdAllowed(rManifest.ManifestId)
        || rManifest.SchemaVersion != u"v3-manual-docs/0.1"_ustr
        || !IsTimestampAllowed(rManifest.CreatedAt)
        || rManifest.Manual.RootPath != u"docs/manual/"_ustr || !rManifest.Manual.Embedded
        || !rManifest.Manual.OnlineMirror || rManifest.Manual.HelpKey != u"?"_ustr
        || !rManifest.Manual.HelpMenuEntry || !rManifest.Manual.SearchEnabled
        || !rManifest.Manual.NoExternalDependency
        || !IsRequiredManualLocalesAllowed(rManifest.Locales.RequiredLocales)
        || !IsLaunchLocaleOrderAllowed(rManifest.Locales.LaunchLocales)
        || rManifest.Locales.FallbackLocale != u"en-US"_ustr
        || rManifest.Locales.FallbackForUntranslated != FallbackManualLocales()
        || !IsManualTopicOrderAllowed(rManifest.Topics)
        || !rManifest.Delivery.EmbeddedBundle || !rManifest.Delivery.OnlineMirrorSource
        || !rManifest.Delivery.OfflineReadable || rManifest.Delivery.RequiresPublicInternet
        || rManifest.Delivery.UpdateChannel != u"w8-self-host-or-release-bundle"_ustr
        || !ContainsString(rManifest.RequiredEvidence, u"manual-docs-manifest"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"embedded-help-open"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"online-mirror-sync"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"locale-coverage"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"evidence-record"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"v2-regression-green"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"policy-decision"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"audit-log-entry"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"edition-policy"_ustr)
        || !ContainsString(rManifest.RequiredEvidence, u"localcloud-no-egress"_ustr)
        || !EvidenceAllowed(rManifest.RequiredEvidence, IsManualRequiredEvidenceAllowed)
        || !HasValidEvidenceIds(rManifest.EvidenceIds) || !rManifest.Gates.BlocksGA
        || !rManifest.Gates.RequiresV2RegressionGreen
        || rManifest.Gates.RuntimeImplementation != u"metadata-runtime-active"_ustr
        || rManifest.Gates.HelpViewerRuntimeActive
        || rManifest.Gates.OnlineMirrorSyncRuntimeActive
        || rManifest.Gates.RemoteDocsServiceActive
        || !rManifest.TenantContextRef.startsWith(u"tenant-context:"_ustr)
        || !rManifest.PolicyContextRef.startsWith(u"policy-context:"_ustr)
        || !rManifest.AuditChainRef.startsWith(u"audit-chain:"_ustr)
        || !rManifest.LocalePolicyRef.startsWith(u"i18n-locale-policy:"_ustr)
        || !rManifest.EditionPolicyRef.startsWith(u"edition-policy:"_ustr)
        || !rManifest.NoEgressRef.startsWith(u"localcloud-no-egress:"_ustr)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rManifest.HashReference))
        return false;

    std::vector<OUString> aPaths;
    for (const AIChatManualTopic& rTopic : rManifest.Topics)
    {
        if (!IsManualTopicIdAllowed(rTopic.TopicId)
            || rTopic.TitleToken != u"manual."_ustr + rTopic.TopicId || !rTopic.Required
            || !rTopic.Embedded || !rTopic.OnlineMirror
            || !IsRequiredManualLocalesAllowed(rTopic.Locales)
            || !IsManualPathAllowed(rTopic.TopicId, u"zh-CN"_ustr, rTopic.PathByLocale.ZhCN)
            || !IsManualPathAllowed(rTopic.TopicId, u"en-US"_ustr, rTopic.PathByLocale.EnUS)
            || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rTopic.EvidenceId))
            return false;
        aPaths.push_back(rTopic.PathByLocale.ZhCN);
        aPaths.push_back(rTopic.PathByLocale.EnUS);
    }
    return !ContainsDuplicateString(aPaths);
}

bool AIChatI18nManualRuntime::IsManualOpenRecordShapeAllowed(
    const AIChatManualDocsManifest& rManifest, const AIChatI18nManualOpenRecord& rOpenRecord)
{
    if (!rOpenRecord.OpenId.startsWith(u"mop-"_ustr)
        || rOpenRecord.ManifestId != rManifest.ManifestId
        || !IsManualTopicIdAllowed(rOpenRecord.TopicId)
        || !ContainsString(RequiredManualLocales(), rOpenRecord.Locale)
        || !rOpenRecord.OfflineOpenSucceeded || rOpenRecord.RequiresPublicInternet
        || !rOpenRecord.EvidenceRequired
        || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rOpenRecord.EvidenceId)
        || !rOpenRecord.AuditLogRef.startsWith(u"audit-log-entry:"_ustr))
        return false;
    return IsManualPathAllowed(rOpenRecord.TopicId, rOpenRecord.Locale, rOpenRecord.PathRef);
}

OUString AIChatI18nManualRuntime::MakeLocalePolicyId(const OUString& rCreatedAt,
                                                     const OUString& rLocale)
{
    return u"i18n-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(u"i18n:"_ustr + rLocale + u":"_ustr
                                                         + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatI18nManualRuntime::MakeManualManifestId(const OUString& rCreatedAt)
{
    return u"manual-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(u"manual-docs:"_ustr + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatI18nManualRuntime::MakeLocalePolicyHashReference(
    const AIChatI18nLocalePolicy& rPolicy)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rPolicy.PolicyId + u":"_ustr + rPolicy.Locale.UiLocale + u":"_ustr
                 + rPolicy.AiOutput.OutputLanguage);
}

OUString AIChatI18nManualRuntime::MakeManualManifestHashReference(
    const AIChatManualDocsManifest& rManifest)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rManifest.ManifestId + u":"_ustr + rManifest.CreatedAt + u":"_ustr
                 + OUString::number(static_cast<sal_Int32>(rManifest.Topics.size())));
}

OUString AIChatI18nManualRuntime::MakeManualOpenId(
    const AIChatManualDocsManifest& rManifest,
    const AIChatI18nManualOpenRecord& rOpenRecord)
{
    return u"mop-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rManifest.ManifestId + u":"_ustr + rOpenRecord.TopicId + u":"_ustr
                 + rOpenRecord.Locale + u":"_ustr + rOpenRecord.EvidenceId)
                 .copy(0, 16);
}

AIChatI18nManualResult AIChatI18nManualRuntime::SaveLocalePolicy(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatI18nLocalePolicy& rPolicy) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);

    AIChatI18nLocalePolicy aPolicy = rPolicy;
    if (aPolicy.PolicyId.isEmpty())
        aPolicy.PolicyId = MakeLocalePolicyId(aPolicy.CreatedAt, aPolicy.Locale.UiLocale);
    aPolicy.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aPolicy.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aPolicy.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aPolicy.EditionPolicyRef.isEmpty())
        aPolicy.EditionPolicyRef = u"edition-policy:metadata-runtime-active"_ustr;
    if (aPolicy.NoEgressRef.isEmpty())
        aPolicy.NoEgressRef = u"localcloud-no-egress:default-deny"_ustr;
    if (aPolicy.HashReference.isEmpty())
        aPolicy.HashReference = MakeLocalePolicyHashReference(aPolicy);

    if (!IsLocalePolicyShapeAllowed(aPolicy))
        return MakeDeniedResult(u"invalid-i18n-locale-policy-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"i18n-locale-policy"_ustr,
        { aPolicy.PolicyId, aPolicy.SchemaVersion, aPolicy.CreatedAt,
          aPolicy.Locale.OsLocale, aPolicy.Locale.UiLocale,
          JoinFields(aPolicy.Locale.SupportedLaunchLocales), aPolicy.AiOutput.OutputLanguage,
          JoinFields(aPolicy.RequiredEvidence), JoinFields(aPolicy.EvidenceIds),
          aPolicy.TenantContextRef, aPolicy.PolicyContextRef, aPolicy.AuditChainRef,
          aPolicy.EditionPolicyRef, aPolicy.NoEgressRef, aPolicy.HashReference });
    if (!AppendUtf8Line(m_sI18nManualStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatI18nManualResult aResult;
    aResult.Success = true;
    aResult.LocalePolicy = aPolicy;
    aResult.Message
        = u"i18n-locale-policy-saved policy-id="_ustr + aPolicy.PolicyId
          + u" osLocale="_ustr + aPolicy.Locale.OsLocale + u" uiLocale="_ustr
          + aPolicy.Locale.UiLocale
          + u" launchLocales=zh-CN,en-US,ja-JP,zh-TW fallbackLocale=en-US"_ustr
          + u" uiFollowsSystemLocale=true usesExistingI18nPool=true silentSwitchAllowed=false"_ustr
          + u" aiOutputMatchesUiLocale=true outputLanguage="_ustr
          + aPolicy.AiOutput.OutputLanguage
          + u" langOverrideExplicitOnly=true persistsWithoutUserAction=false"_ustr
          + u" manualRequiredLocales=zh-CN,en-US evidenceRequired=true"_ustr
          + u" metadata-only=true runtimeImplementation=metadata-runtime-active"_ustr
          + u" i18npool-runtime=not-started prompt-language-runtime=not-started"_ustr
          + u" cloud-translation=not-started publicEgress=false"_ustr;
    return aResult;
}

AIChatI18nManualResult AIChatI18nManualRuntime::SaveManualManifest(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatI18nLocalePolicy& rLocalePolicy,
    const AIChatManualDocsManifest& rManifest) const
{
    AIChatI18nManualResult aBase = SaveLocalePolicy(rContext, rScope, rLocalePolicy);
    if (!aBase.Success)
        return aBase;

    AIChatManualDocsManifest aManifest = rManifest;
    if (aManifest.ManifestId.isEmpty())
        aManifest.ManifestId = MakeManualManifestId(aManifest.CreatedAt);
    aManifest.TenantContextRef = u"tenant-context:"_ustr + rContext.ContextId;
    aManifest.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aManifest.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    if (aManifest.LocalePolicyRef.isEmpty())
        aManifest.LocalePolicyRef = u"i18n-locale-policy:"_ustr + aBase.LocalePolicy.PolicyId;
    if (aManifest.EditionPolicyRef.isEmpty())
        aManifest.EditionPolicyRef = u"edition-policy:metadata-runtime-active"_ustr;
    if (aManifest.NoEgressRef.isEmpty())
        aManifest.NoEgressRef = u"localcloud-no-egress:default-deny"_ustr;
    if (aManifest.HashReference.isEmpty())
        aManifest.HashReference = MakeManualManifestHashReference(aManifest);

    if (!IsManualManifestShapeAllowed(aManifest))
        return MakeDeniedResult(u"invalid-manual-docs-manifest-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"manual-docs-manifest"_ustr,
        { aManifest.ManifestId, aManifest.SchemaVersion, aManifest.CreatedAt,
          aManifest.Manual.RootPath, aManifest.Manual.HelpKey,
          JoinFields(aManifest.Locales.RequiredLocales), JoinFields(aManifest.Locales.LaunchLocales),
          OUString::number(static_cast<sal_Int32>(aManifest.Topics.size())),
          JoinFields(aManifest.RequiredEvidence), JoinFields(aManifest.EvidenceIds),
          aManifest.TenantContextRef, aManifest.PolicyContextRef, aManifest.AuditChainRef,
          aManifest.LocalePolicyRef, aManifest.EditionPolicyRef, aManifest.NoEgressRef,
          aManifest.HashReference });
    if (!AppendUtf8Line(m_sI18nManualStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.ManualManifest = aManifest;
    aBase.Message
        = u"manual-docs-manifest-saved manifest-id="_ustr + aManifest.ManifestId
          + u" rootPath=docs/manual/ embedded=true onlineMirror=true helpKey=? helpMenuEntry=true"_ustr
          + u" searchEnabled=true noExternalDependency=true offlineReadable=true"_ustr
          + u" requiredLocales=zh-CN,en-US launchLocales=zh-CN,en-US,ja-JP,zh-TW"_ustr
          + u" fallbackLocale=en-US fallbackForUntranslated=ja-JP,zh-TW topics=8"_ustr
          + u" requiresPublicInternet=false updateChannel=w8-self-host-or-release-bundle"_ustr
          + u" locale-policy-ref="_ustr + aManifest.LocalePolicyRef
          + u" edition-policy-ref="_ustr + aManifest.EditionPolicyRef
          + u" no-egress-ref="_ustr + aManifest.NoEgressRef
          + u" metadata-only=true runtimeImplementation=metadata-runtime-active"_ustr
          + u" help-viewer-runtime=not-started online-mirror-sync-runtime=not-started"_ustr
          + u" remote-docs-service=not-started webview=false"_ustr;
    return aBase;
}

AIChatI18nManualResult AIChatI18nManualRuntime::RecordManualOpen(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatI18nLocalePolicy& rLocalePolicy, const AIChatManualDocsManifest& rManifest,
    const AIChatI18nManualOpenRecord& rOpenRecord) const
{
    AIChatI18nManualResult aBase = SaveManualManifest(rContext, rScope, rLocalePolicy, rManifest);
    if (!aBase.Success)
        return aBase;

    AIChatI18nManualOpenRecord aOpen = rOpenRecord;
    aOpen.ManifestId = aBase.ManualManifest.ManifestId;
    if (aOpen.OpenId.isEmpty())
        aOpen.OpenId = MakeManualOpenId(aBase.ManualManifest, aOpen);
    if (aOpen.AuditLogRef.isEmpty())
        aOpen.AuditLogRef = u"audit-log-entry:"_ustr + aOpen.EvidenceId;

    if (!IsManualOpenRecordShapeAllowed(aBase.ManualManifest, aOpen))
        return MakeDeniedResult(u"invalid-manual-open-record-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"manual-open"_ustr,
        { aOpen.OpenId, aOpen.ManifestId, aOpen.TopicId, aOpen.Locale, aOpen.PathRef,
          aOpen.EvidenceId, aOpen.AuditLogRef, u"offlineOpenSucceeded=true"_ustr,
          u"requiresPublicInternet=false"_ustr });
    if (!AppendUtf8Line(m_sI18nManualStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    aBase.OpenRecord = aOpen;
    aBase.Message
        = u"manual-open-recorded open-id="_ustr + aOpen.OpenId
          + u" manifest-id="_ustr + aOpen.ManifestId + u" topic-id="_ustr + aOpen.TopicId
          + u" locale="_ustr + aOpen.Locale + u" path-ref="_ustr + aOpen.PathRef
          + u" offlineOpenSucceeded=true requiresPublicInternet=false"_ustr
          + u" evidenceRequired=true evidence-id="_ustr + aOpen.EvidenceId
          + u" audit-log-ref="_ustr + aOpen.AuditLogRef
          + u" metadata-only=true raw-manual-body=false publicEgress=false"_ustr;
    return aBase;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
