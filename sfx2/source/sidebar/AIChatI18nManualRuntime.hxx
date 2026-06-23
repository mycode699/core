/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: i18n/manual runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatEditionPolicyRuntime.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatI18nLocaleState
{
    OUString OsLocale;
    OUString UiLocale;
    std::vector<OUString> SupportedLaunchLocales;
    OUString FallbackLocale;
};

struct AIChatI18nUiPolicy
{
    bool FollowsSystemLocale = true;
    bool UsesExistingI18nPool = true;
    bool SilentSwitchAllowed = false;
};

struct AIChatI18nInlineOverride
{
    bool Enabled = true;
    OUString CommandPattern;
    bool ExplicitOnly = true;
    bool PersistsWithoutUserAction = false;
};

struct AIChatI18nAiOutputPolicy
{
    OUString DefaultLocale;
    bool MatchesUiLocale = true;
    AIChatI18nInlineOverride InlineOverride;
    OUString OutputLanguage;
    bool EvidenceRequired = true;
};

struct AIChatI18nManualBaseline
{
    bool Embedded = true;
    bool OnlineMirror = true;
    std::vector<OUString> RequiredLocales;
};

struct AIChatI18nGateState
{
    bool BlocksGA = true;
    bool RequiresV2RegressionGreen = true;
    OUString RuntimeImplementation;
    bool I18nPoolRuntimeActive = false;
    bool PromptLanguageRuntimeActive = false;
    bool CloudTranslationActive = false;
};

struct AIChatI18nLocalePolicy
{
    OUString PolicyId;
    OUString SchemaVersion;
    OUString CreatedAt;
    AIChatI18nLocaleState Locale;
    AIChatI18nUiPolicy Ui;
    AIChatI18nAiOutputPolicy AiOutput;
    AIChatI18nManualBaseline Manual;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    AIChatI18nGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString EditionPolicyRef;
    OUString NoEgressRef;
    OUString HashReference;
};

struct AIChatManualTopicPath
{
    OUString ZhCN;
    OUString EnUS;
};

struct AIChatManualTopic
{
    OUString TopicId;
    OUString TitleToken;
    bool Required = true;
    bool Embedded = true;
    bool OnlineMirror = true;
    std::vector<OUString> Locales;
    AIChatManualTopicPath PathByLocale;
    OUString EvidenceId;
};

struct AIChatManualPolicy
{
    OUString RootPath;
    bool Embedded = true;
    bool OnlineMirror = true;
    OUString HelpKey;
    bool HelpMenuEntry = true;
    bool SearchEnabled = true;
    bool NoExternalDependency = true;
};

struct AIChatManualLocales
{
    std::vector<OUString> RequiredLocales;
    std::vector<OUString> LaunchLocales;
    OUString FallbackLocale;
    std::vector<OUString> FallbackForUntranslated;
};

struct AIChatManualDelivery
{
    bool EmbeddedBundle = true;
    bool OnlineMirrorSource = true;
    bool OfflineReadable = true;
    bool RequiresPublicInternet = false;
    OUString UpdateChannel;
};

struct AIChatManualGateState
{
    bool BlocksGA = true;
    bool RequiresV2RegressionGreen = true;
    OUString RuntimeImplementation;
    bool HelpViewerRuntimeActive = false;
    bool OnlineMirrorSyncRuntimeActive = false;
    bool RemoteDocsServiceActive = false;
};

struct AIChatManualDocsManifest
{
    OUString ManifestId;
    OUString SchemaVersion;
    OUString CreatedAt;
    AIChatManualPolicy Manual;
    AIChatManualLocales Locales;
    std::vector<AIChatManualTopic> Topics;
    AIChatManualDelivery Delivery;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    AIChatManualGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString LocalePolicyRef;
    OUString EditionPolicyRef;
    OUString NoEgressRef;
    OUString HashReference;
};

struct AIChatI18nManualOpenRecord
{
    OUString OpenId;
    OUString ManifestId;
    OUString TopicId;
    OUString Locale;
    OUString PathRef;
    bool OfflineOpenSucceeded = false;
    bool RequiresPublicInternet = false;
    bool EvidenceRequired = true;
    OUString EvidenceId;
    OUString AuditLogRef;
};

struct AIChatI18nManualResult
{
    bool Success = false;
    AIChatI18nLocalePolicy LocalePolicy;
    AIChatManualDocsManifest ManualManifest;
    AIChatI18nManualOpenRecord OpenRecord;
    OUString Message;
};

class AIChatI18nManualRuntime final
{
public:
    AIChatI18nManualRuntime();

    const OUString& GetI18nManualStoreUrl() const { return m_sI18nManualStoreUrl; }

    AIChatI18nManualResult SaveLocalePolicy(const AIChatTenantContext& rContext,
                                            const AIChatTenantActionScope& rScope,
                                            const AIChatI18nLocalePolicy& rPolicy) const;
    AIChatI18nManualResult SaveManualManifest(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatI18nLocalePolicy& rLocalePolicy,
        const AIChatManualDocsManifest& rManifest) const;
    AIChatI18nManualResult RecordManualOpen(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatI18nLocalePolicy& rLocalePolicy,
        const AIChatManualDocsManifest& rManifest,
        const AIChatI18nManualOpenRecord& rOpenRecord) const;

    static bool IsLocalePolicyIdAllowed(const OUString& rPolicyId);
    static bool IsManualManifestIdAllowed(const OUString& rManifestId);
    static bool IsTimestampAllowed(const OUString& rTimestamp);
    static bool IsLocaleAllowed(const OUString& rLocale);
    static bool IsLaunchLocaleOrderAllowed(const std::vector<OUString>& rLocales);
    static bool IsRequiredManualLocalesAllowed(const std::vector<OUString>& rLocales);
    static bool IsAiOutputLanguageAllowed(const OUString& rLocale,
                                          const OUString& rOutputLanguage);
    static bool IsI18nRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsManualRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsManualTopicIdAllowed(const OUString& rTopicId);
    static bool IsManualTopicOrderAllowed(const std::vector<AIChatManualTopic>& rTopics);
    static bool IsManualPathAllowed(const OUString& rTopicId, const OUString& rLocale,
                                    const OUString& rPath);
    static bool IsLocalePolicyShapeAllowed(const AIChatI18nLocalePolicy& rPolicy);
    static bool IsManualManifestShapeAllowed(const AIChatManualDocsManifest& rManifest);
    static bool IsManualOpenRecordShapeAllowed(const AIChatManualDocsManifest& rManifest,
                                               const AIChatI18nManualOpenRecord& rOpenRecord);
    static OUString MakeLocalePolicyId(const OUString& rCreatedAt, const OUString& rLocale);
    static OUString MakeManualManifestId(const OUString& rCreatedAt);
    static OUString MakeLocalePolicyHashReference(const AIChatI18nLocalePolicy& rPolicy);
    static OUString MakeManualManifestHashReference(const AIChatManualDocsManifest& rManifest);
    static OUString MakeManualOpenId(const AIChatManualDocsManifest& rManifest,
                                     const AIChatI18nManualOpenRecord& rOpenRecord);

private:
    OUString m_sStorageRootUrl;
    OUString m_sI18nManualStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
