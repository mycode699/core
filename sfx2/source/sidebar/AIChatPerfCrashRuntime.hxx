/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W9/M7: perf/crash runtime).
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

struct AIChatPerfColdStartTarget
{
    sal_Int32 TargetMs = 2000;
    OUString Measurement;
    OUString PackageRoute;
};

struct AIChatPerfFirstTokenTarget
{
    sal_Int32 TargetMs = 800;
    OUString Trigger;
    OUString Provider;
    OUString Model;
};

struct AIChatPerfRetrievalTarget
{
    sal_Int32 TargetMs = 200;
    sal_Int32 CorpusDocuments = 10000;
    sal_Int32 TopK = 5;
    OUString Index;
};

struct AIChatPerfGateState
{
    bool BlocksGA = true;
    OUString RuntimeImplementation;
    bool RuntimeSamplerActive = false;
    bool ModelDownloadActive = false;
    bool ExternalMetricUploadActive = false;
    bool BackgroundProbeActive = false;
};

struct AIChatPerfBaselineTarget
{
    OUString TargetId;
    OUString Wave;
    OUString Category;
    OUString Platform;
    OUString MeasurementMode;
    AIChatPerfColdStartTarget ColdStart;
    AIChatPerfFirstTokenTarget FirstToken;
    AIChatPerfRetrievalTarget Retrieval;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    bool BlocksGA = true;
    AIChatPerfGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString DistributionRecoveryRef;
    OUString NoEgressRef;
    OUString ReleaseEvidenceRef;
    OUString HashReference;
};

struct AIChatPerfSampleRecord
{
    OUString SampleId;
    OUString TargetId;
    OUString Platform;
    sal_Int32 ColdStartMs = 2000;
    sal_Int32 FirstTokenMs = 800;
    sal_Int32 RetrievalMs = 200;
    OUString PackageRoute;
    OUString Provider;
    OUString Model;
    sal_Int32 CorpusDocuments = 10000;
    sal_Int32 TopK = 5;
    OUString SystemProfileRef;
    OUString LocalProviderProofRef;
    OUString KnowledgeIndexSampleRef;
    bool PublicEgress = false;
    bool HiddenModelDownload = false;
    OUString EvidenceId;
    OUString AuditLogRef;
};

struct AIChatCrashTriggerTarget
{
    OUString Mode;
    OUString ProcessState;
};

struct AIChatUnsavedEditTarget
{
    OUString DocumentId;
    OUString EditKind;
    sal_Int32 MinCharacters = 1;
    bool IncludesUnsavedState = true;
};

struct AIChatAutosaveTarget
{
    sal_Int32 IntervalSeconds = 30;
    OUString Storage;
    bool PublicEgress = false;
};

struct AIChatRecoveryDialogTarget
{
    OUString Dialog;
    sal_Int32 MaxDialogSeconds = 30;
    bool OneClickRestore = true;
    OUString DiffExpected;
    OUString DataLossTolerance;
};

struct AIChatCrashRecoveryScenarioTarget
{
    OUString DocumentSurface;
    AIChatCrashTriggerTarget CrashTrigger;
    AIChatUnsavedEditTarget UnsavedEdit;
    AIChatAutosaveTarget Autosave;
    AIChatRecoveryDialogTarget Recovery;
};

struct AIChatCrashGateState
{
    bool BlocksGA = true;
    OUString RuntimeImplementation;
    bool SigkillRunnerActive = false;
    bool AutosaveEngineRuntimeActive = false;
    bool RecoveryDialogRuntimeActive = false;
    bool CloudRecoveryRuntimeActive = false;
    bool MainDocumentWriteRuntimeActive = false;
};

struct AIChatCrashRecoveryTarget
{
    OUString TargetId;
    OUString Wave;
    OUString Category;
    OUString Platform;
    OUString MeasurementMode;
    AIChatCrashRecoveryScenarioTarget Scenario;
    std::vector<OUString> RequiredEvidence;
    std::vector<OUString> EvidenceIds;
    bool BlocksGA = true;
    AIChatCrashGateState Gates;
    OUString TenantContextRef;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString DistributionRecoveryRef;
    OUString NoEgressRef;
    OUString ReleaseEvidenceRef;
    OUString HashReference;
};

struct AIChatCrashRecoverySampleRecord
{
    OUString SampleId;
    OUString TargetId;
    OUString Platform;
    OUString DocumentSurface;
    OUString DocumentId;
    OUString AutosaveSnapshotRef;
    OUString SigkillMarkerRef;
    OUString RecoveryDialogRef;
    OUString RestoreAppliedRef;
    OUString DiffZeroProofRef;
    sal_Int32 AutosaveIntervalSeconds = 30;
    sal_Int32 RecoveryDialogSeconds = 30;
    bool OneClickRestore = true;
    OUString DiffExpected;
    OUString DataLossTolerance;
    bool MainDocumentChanged = false;
    bool PublicEgress = false;
    OUString EvidenceId;
    OUString AuditLogRef;
};

struct AIChatPerfCrashResult
{
    bool Success = false;
    AIChatPerfBaselineTarget PerfTarget;
    AIChatPerfSampleRecord PerfSample;
    AIChatCrashRecoveryTarget CrashTarget;
    AIChatCrashRecoverySampleRecord CrashSample;
    OUString Message;
};

class AIChatPerfCrashRuntime final
{
public:
    AIChatPerfCrashRuntime();

    const OUString& GetPerfCrashStoreUrl() const { return m_sPerfCrashStoreUrl; }

    AIChatPerfCrashResult SavePerfBaselineTarget(const AIChatTenantContext& rContext,
                                                 const AIChatTenantActionScope& rScope,
                                                 const AIChatPerfBaselineTarget& rTarget) const;
    AIChatPerfCrashResult RecordPerfSample(const AIChatTenantContext& rContext,
                                           const AIChatTenantActionScope& rScope,
                                           const AIChatPerfBaselineTarget& rTarget,
                                           const AIChatPerfSampleRecord& rRecord) const;
    AIChatPerfCrashResult SaveCrashRecoveryTarget(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatCrashRecoveryTarget& rTarget) const;
    AIChatPerfCrashResult RecordCrashRecoverySample(
        const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
        const AIChatCrashRecoveryTarget& rTarget,
        const AIChatCrashRecoverySampleRecord& rRecord) const;

    static bool IsPerfTargetIdAllowed(const OUString& rTargetId);
    static bool IsCrashTargetIdAllowed(const OUString& rTargetId);
    static bool IsPlatformAllowed(const OUString& rPlatform);
    static bool IsPackageRouteAllowedForPlatform(const OUString& rPlatform,
                                                 const OUString& rRoute);
    static bool IsPerfRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsCrashRequiredEvidenceAllowed(const OUString& rEvidence);
    static bool IsDocumentSurfaceAllowedForPlatform(const OUString& rPlatform,
                                                    const OUString& rSurface);
    static bool IsEditKindAllowedForSurface(const OUString& rSurface,
                                            const OUString& rEditKind);
    static bool IsPerfTargetShapeAllowed(const AIChatPerfBaselineTarget& rTarget);
    static bool IsPerfSampleRecordShapeAllowed(const AIChatPerfBaselineTarget& rTarget,
                                               const AIChatPerfSampleRecord& rRecord);
    static bool IsCrashRecoveryTargetShapeAllowed(const AIChatCrashRecoveryTarget& rTarget);
    static bool IsCrashRecoverySampleRecordShapeAllowed(
        const AIChatCrashRecoveryTarget& rTarget,
        const AIChatCrashRecoverySampleRecord& rRecord);
    static OUString MakePerfTargetId(const OUString& rPlatform);
    static OUString MakeCrashTargetId(const OUString& rPlatform, const OUString& rSurface);
    static OUString MakePerfTargetHashReference(const AIChatPerfBaselineTarget& rTarget);
    static OUString MakeCrashTargetHashReference(const AIChatCrashRecoveryTarget& rTarget);
    static OUString MakePerfSampleId(const AIChatPerfBaselineTarget& rTarget,
                                     const AIChatPerfSampleRecord& rRecord);
    static OUString MakeCrashSampleId(const AIChatCrashRecoveryTarget& rTarget,
                                      const AIChatCrashRecoverySampleRecord& rRecord);

private:
    OUString m_sStorageRootUrl;
    OUString m_sPerfCrashStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
