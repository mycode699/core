/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W7/M6: companion approval runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatCompanionApprovalRuntime.hxx"

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
constexpr OUStringLiteral COMPANION_APPROVAL_DIR_NAME = u"kqoffice-v3-ai-companion-approval";
constexpr OUStringLiteral COMPANION_APPROVAL_FILE_NAME = u"companion-approval.tsv";
constexpr sal_Int32 W8_PUSH_GATEWAY_PORT = 17801;

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

bool HasOnlyAllowedEvidence(const std::vector<OUString>& rRequired)
{
    for (const OUString& rEvidence : rRequired)
    {
        if (!AIChatCompanionApprovalRuntime::IsRequiredEvidenceAllowed(rEvidence))
            return false;
    }
    return true;
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

bool HasBaseRefs(const OUString& rAuditLogRef, const OUString& rTenantContextRef,
                 const OUString& rSyncMessageRef)
{
    return rAuditLogRef.startsWith(u"audit-log-entry:"_ustr)
           && rTenantContextRef.startsWith(u"tenant-context:"_ustr)
           && rSyncMessageRef.startsWith(u"sync-message:"_ustr);
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

AIChatCompanionApprovalResult MakeDeniedResult(const OUString& rReason)
{
    AIChatCompanionApprovalResult aResult;
    aResult.Message
        = u"companion-approval-denied reason="_ustr + rReason
          + u" fail-closed-user-visible=true metadata-only=true"_ustr
          + u" storesDocumentContent=false mobileMayEdit=false allowApprovalOffline=false"_ustr
          + u" no-document-mutation=true no-apply-plan-execution=true"_ustr;
    return aResult;
}

bool ScopeAllowed(const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope)
{
    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    return aScopeResult.Success && rScope.TargetType == u"companion"_ustr
           && rScope.Surface == u"companion"_ustr;
}
}

AIChatCompanionApprovalRuntime::AIChatCompanionApprovalRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + COMPANION_APPROVAL_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sCompanionStoreUrl = m_sStorageRootUrl + u"/"_ustr + COMPANION_APPROVAL_FILE_NAME;
}

bool AIChatCompanionApprovalRuntime::IsCompanionIdAllowed(const OUString& rId,
                                                          const OUString& rPrefix)
{
    const OUString sPrefix = rPrefix + u"-"_ustr;
    return rId.startsWith(sPrefix)
           && IsLowerHex(rId.copy(rPrefix.getLength() + 1), 16);
}

bool AIChatCompanionApprovalRuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return AIChatAuditLogRuntime::IsTimestampAllowed(rTimestamp);
}

bool AIChatCompanionApprovalRuntime::IsDesktopAppAllowed(const OUString& rDesktopApp)
{
    return rDesktopApp == u"writer"_ustr || rDesktopApp == u"calc"_ustr
           || rDesktopApp == u"impress"_ustr || rDesktopApp == u"start-center"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsDevicePlatformAllowed(const OUString& rPlatform)
{
    return rPlatform == u"ios"_ustr || rPlatform == u"android"_ustr || rPlatform == u"pwa"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsPairingTransportAllowed(const OUString& rTransport)
{
    return rTransport == u"lan-grpc"_ustr || rTransport == u"enterprise-https"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsApprovalTransportAllowed(const OUString& rTransport)
{
    return rTransport == u"lan-push"_ustr || rTransport == u"enterprise-push"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsSurfaceAllowed(const OUString& rSurface)
{
    return rSurface == u"writer"_ustr || rSurface == u"calc"_ustr || rSurface == u"impress"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsSummaryKindAllowed(const OUString& rKind)
{
    return rKind == u"paragraph"_ustr || rKind == u"cell"_ustr || rKind == u"slide"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsActionKindAllowedForSurface(const OUString& rSurface,
                                                                   const OUString& rActionKind)
{
    return (rSurface == u"writer"_ustr && rActionKind == u"ParagraphAction"_ustr)
           || (rSurface == u"calc"_ustr && rActionKind == u"CellAction"_ustr)
           || (rSurface == u"impress"_ustr && rActionKind == u"SlideElementAction"_ustr);
}

bool AIChatCompanionApprovalRuntime::IsRiskLevelAllowed(const OUString& rRiskLevel)
{
    return rRiskLevel == u"low"_ustr || rRiskLevel == u"medium"_ustr
           || rRiskLevel == u"high"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsSnippetStorageAllowed(const OUString& rSnippetStorage)
{
    return rSnippetStorage == u"hash-only"_ustr || rSnippetStorage == u"redacted-summary"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsRequestStateAllowed(const OUString& rState)
{
    return rState == u"awaiting-review"_ustr || rState == u"per-step-review"_ustr
           || rState == u"failure-recovery"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsApprovalActionAllowed(const OUString& rAction)
{
    return rAction == u"approve"_ustr || rAction == u"reject"_ustr || rAction == u"rollback"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsDecisionAllowed(const OUString& rDecision)
{
    return rDecision == u"approved"_ustr || rDecision == u"rejected"_ustr
           || rDecision == u"rollback-requested"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"companion-pairing-token"_ustr
           || rEvidence == u"user-pin-confirmation"_ustr || rEvidence == u"device-binding"_ustr
           || rEvidence == u"companion-diff-summary"_ustr || rEvidence == u"shadow-doc-diff"_ustr
           || rEvidence == u"apply-plan-runtime-validated"_ustr
           || rEvidence == u"companion-approval-request"_ustr
           || rEvidence == u"user-approval"_ustr
           || rEvidence == u"biometric-confirmation"_ustr
           || rEvidence == u"audit-log-entry"_ustr || rEvidence == u"evidence-record"_ustr;
}

bool AIChatCompanionApprovalRuntime::IsDataBoundaryAllowed(
    const AIChatCompanionDataBoundary& rBoundary)
{
    return !rBoundary.StoresDocumentContent && rBoundary.StoresDiffSummaryOnly
           && !rBoundary.AllowApprovalOffline;
}

bool AIChatCompanionApprovalRuntime::IsPairingTokenShapeAllowed(
    const AIChatCompanionPairingToken& rToken)
{
    if (!IsCompanionIdAllowed(rToken.PairingId, u"cpt"_ustr)
        || rToken.SchemaVersion != u"v3-companion-pairing-token/0.1"_ustr
        || !IsTimestampAllowed(rToken.CreatedAt) || !IsTimestampAllowed(rToken.ExpiresAt)
        || !AIChatTenantContextRuntime::IsTenantIdAllowed(rToken.TenantId)
        || !AIChatTenantContextRuntime::IsWorkspaceIdAllowed(rToken.WorkspaceId)
        || !IsCompanionIdAllowed(rToken.DesktopInstanceId, u"desk"_ustr)
        || !IsDesktopAppAllowed(rToken.DesktopApp)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rToken.LanEndpointHash)
        || !IsCompanionIdAllowed(rToken.ProposedDeviceId, u"dev"_ustr)
        || !IsDevicePlatformAllowed(rToken.DevicePlatform) || !rToken.BindingRequired
        || !IsPairingTransportAllowed(rToken.TransportMode) || rToken.Port != W8_PUSH_GATEWAY_PORT
        || (rToken.PublicEgress && !rToken.CloudPushOptIn)
        || (rToken.TransportMode == u"lan-grpc"_ustr
            && (rToken.PublicEgress || !rToken.MdnsRequired || rToken.CloudPushOptIn))
        || rToken.TtlSeconds < 60 || rToken.TtlSeconds > 600 || rToken.SessionTtlHours != 24
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rToken.TokenHash)
        || rToken.StoresSecret || !rToken.PinConfirmationRequired
        || !rToken.BiometricEnrollmentRequired || !rToken.MTLSRequired || !rToken.Revocable
        || !IsDataBoundaryAllowed(rToken.DataBoundary)
        || !ContainsString(rToken.RequiredEvidence, u"companion-pairing-token"_ustr)
        || !ContainsString(rToken.RequiredEvidence, u"user-pin-confirmation"_ustr)
        || !ContainsString(rToken.RequiredEvidence, u"device-binding"_ustr)
        || !ContainsString(rToken.RequiredEvidence, u"audit-log-entry"_ustr)
        || !HasOnlyAllowedEvidence(rToken.RequiredEvidence) || !HasValidEvidenceIds(rToken.EvidenceIds)
        || !HasBaseRefs(rToken.AuditLogRef, rToken.TenantContextRef, rToken.SyncMessageRef))
        return false;
    return true;
}

bool AIChatCompanionApprovalRuntime::IsDiffSummaryShapeAllowed(
    const AIChatCompanionDiffSummary& rSummary)
{
    if (!IsCompanionIdAllowed(rSummary.SummaryId, u"cds"_ustr)
        || rSummary.SchemaVersion != u"v3-companion-diff-summary/0.1"_ustr
        || !IsTimestampAllowed(rSummary.CreatedAt)
        || !IsCompanionIdAllowed(rSummary.TaskId, u"agt"_ustr)
        || !IsCompanionIdAllowed(rSummary.StepResultId, u"agsr"_ustr)
        || !AIChatTenantContextRuntime::IsTenantIdAllowed(rSummary.TenantId)
        || !AIChatTenantContextRuntime::IsWorkspaceIdAllowed(rSummary.WorkspaceId)
        || !IsSurfaceAllowed(rSummary.Surface) || !IsSummaryKindAllowed(rSummary.SummaryKind)
        || !IsCompanionIdAllowed(rSummary.ApplyPlanRef, u"aprt"_ustr)
        || !IsActionKindAllowedForSurface(rSummary.Surface, rSummary.ActionKind)
        || rSummary.ActionCount < 1 || rSummary.ActionCount > 25
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rSummary.PreviewHash)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rSummary.HumanSummaryHash)
        || rSummary.StoresDocumentContent || rSummary.MobileParsesApplyPlan
        || !IsRiskLevelAllowed(rSummary.RiskLevel) || !rSummary.RequiresApproval
        || !IsSnippetStorageAllowed(rSummary.SnippetStorage) || rSummary.ContainsOriginalText
        || rSummary.CacheMode != u"diff-summary-plus-evidence"_ustr || !rSummary.ViewOnly
        || rSummary.CanEdit || rSummary.OfflineApproval
        || !IsDataBoundaryAllowed(rSummary.DataBoundary) || rSummary.ChangedObjectRefs.empty()
        || !ContainsString(rSummary.RequiredEvidence, u"companion-diff-summary"_ustr)
        || !ContainsString(rSummary.RequiredEvidence, u"shadow-doc-diff"_ustr)
        || !ContainsString(rSummary.RequiredEvidence, u"apply-plan-runtime-validated"_ustr)
        || !ContainsString(rSummary.RequiredEvidence, u"evidence-record"_ustr)
        || !ContainsString(rSummary.RequiredEvidence, u"audit-log-entry"_ustr)
        || !HasOnlyAllowedEvidence(rSummary.RequiredEvidence)
        || !HasValidEvidenceIds(rSummary.EvidenceIds)
        || !HasBaseRefs(rSummary.AuditLogRef, rSummary.TenantContextRef, rSummary.SyncMessageRef))
        return false;

    for (const OUString& rObjectRef : rSummary.ChangedObjectRefs)
    {
        if (rObjectRef.isEmpty() || rObjectRef.getLength() > 160)
            return false;
    }
    return true;
}

bool AIChatCompanionApprovalRuntime::IsApprovalRequestShapeAllowed(
    const AIChatCompanionApprovalRequest& rRequest)
{
    if (!IsCompanionIdAllowed(rRequest.RequestId, u"car"_ustr)
        || rRequest.SchemaVersion != u"v3-companion-approval-request/0.1"_ustr
        || !IsTimestampAllowed(rRequest.CreatedAt) || !IsTimestampAllowed(rRequest.ExpiresAt)
        || !IsCompanionIdAllowed(rRequest.TaskId, u"agt"_ustr)
        || !IsCompanionIdAllowed(rRequest.SummaryId, u"cds"_ustr)
        || !IsCompanionIdAllowed(rRequest.PairingId, u"cpt"_ustr)
        || !AIChatTenantContextRuntime::IsTenantIdAllowed(rRequest.TenantId)
        || !AIChatTenantContextRuntime::IsWorkspaceIdAllowed(rRequest.WorkspaceId)
        || !AIChatTenantContextRuntime::IsUserIdAllowed(rRequest.ActorId)
        || rRequest.ActorRole != u"user"_ustr || rRequest.Channel != u"companion"_ustr
        || !IsRequestStateAllowed(rRequest.RequestState) || rRequest.AvailableActions.size() < 2
        || !rRequest.RequiresOnline || !IsApprovalTransportAllowed(rRequest.TransportMode)
        || rRequest.LocalGatewayPort != W8_PUSH_GATEWAY_PORT
        || (rRequest.PublicEgress && !rRequest.CloudPushOptIn)
        || (rRequest.TransportMode == u"lan-push"_ustr
            && (rRequest.PublicEgress || rRequest.CloudPushOptIn))
        || !rRequest.BiometricRequired || !rRequest.SecondConfirmRequired
        || rRequest.MobileMayEdit || !rRequest.DecisionWritesAudit
        || !IsDataBoundaryAllowed(rRequest.DataBoundary) || !rRequest.AuditLogRequired
        || !rRequest.AuditEvidenceRequired
        || !ContainsString(rRequest.RequiredEvidence, u"companion-approval-request"_ustr)
        || !ContainsString(rRequest.RequiredEvidence, u"user-approval"_ustr)
        || !ContainsString(rRequest.RequiredEvidence, u"biometric-confirmation"_ustr)
        || !ContainsString(rRequest.RequiredEvidence, u"audit-log-entry"_ustr)
        || !ContainsString(rRequest.RequiredEvidence, u"evidence-record"_ustr)
        || !HasOnlyAllowedEvidence(rRequest.RequiredEvidence)
        || !HasValidEvidenceIds(rRequest.EvidenceIds)
        || !HasBaseRefs(rRequest.AuditLogRef, rRequest.TenantContextRef, rRequest.SyncMessageRef)
        || !rRequest.ReviewItemRef.startsWith(u"review:"_ustr))
        return false;

    for (const OUString& rAction : rRequest.AvailableActions)
    {
        if (!IsApprovalActionAllowed(rAction))
            return false;
    }
    return true;
}

bool AIChatCompanionApprovalRuntime::IsApprovalDecisionShapeAllowed(
    const AIChatCompanionApprovalRequest& rRequest,
    const AIChatCompanionApprovalDecision& rDecision)
{
    return IsCompanionIdAllowed(rDecision.DecisionId, u"cad"_ustr)
           && rDecision.RequestId == rRequest.RequestId && rDecision.SummaryId == rRequest.SummaryId
           && rDecision.PairingId == rRequest.PairingId && IsTimestampAllowed(rDecision.CreatedAt)
           && rDecision.ActorId == rRequest.ActorId && IsDecisionAllowed(rDecision.Decision)
           && AIChatTenantContextRuntime::IsEvidenceIdAllowed(rDecision.EvidenceId)
           && AIChatTenantContextRuntime::IsHashReferenceAllowed(rDecision.HashReference)
           && rDecision.AuditLogRef == rRequest.AuditLogRef
           && rDecision.SyncMessageRef == rRequest.SyncMessageRef
           && !rDecision.AppliesDocumentChange && !rDecision.MobileMayEdit
           && rDecision.BiometricConfirmed && rDecision.SecondConfirmCompleted;
}

OUString AIChatCompanionApprovalRuntime::MakePairingId(const AIChatTenantActionScope& rScope,
                                                       const OUString& rCreatedAt)
{
    return u"cpt-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rScope.TenantId + u":"_ustr
                                                         + rScope.WorkspaceId + u":pairing:"_ustr
                                                         + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatCompanionApprovalRuntime::MakeSummaryId(const AIChatTenantActionScope& rScope,
                                                       const OUString& rTaskId,
                                                       const OUString& rCreatedAt)
{
    return u"cds-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rScope.TenantId + u":"_ustr
                                                         + rScope.WorkspaceId + u":"_ustr + rTaskId
                                                         + u":summary:"_ustr + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatCompanionApprovalRuntime::MakeRequestId(const AIChatTenantActionScope& rScope,
                                                       const OUString& rSummaryId,
                                                       const OUString& rCreatedAt)
{
    return u"car-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rScope.UserId + u":"_ustr + rSummaryId
                                                         + u":approval:"_ustr + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatCompanionApprovalRuntime::MakeDecisionId(
    const AIChatCompanionApprovalRequest& rRequest,
    const AIChatCompanionApprovalDecision& rDecision)
{
    return u"cad-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rRequest.RequestId + u":"_ustr
                                                         + rDecision.Decision + u":"_ustr
                                                         + rDecision.CreatedAt)
                 .copy(0, 16);
}

AIChatCompanionApprovalResult AIChatCompanionApprovalRuntime::PublishPairingToken(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatCompanionPairingToken& rToken) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);
    if (!IsPairingTokenShapeAllowed(rToken) || rToken.TenantId != rContext.TenantId
        || rToken.WorkspaceId != rContext.WorkspaceId)
        return MakeDeniedResult(u"invalid-pairing-token-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"pairing-token"_ustr,
        { rToken.PairingId, rToken.SchemaVersion, rToken.CreatedAt, rToken.ExpiresAt,
          rToken.TenantId, rToken.WorkspaceId, rToken.DesktopInstanceId, rToken.DesktopApp,
          rToken.LanEndpointHash, rToken.ProposedDeviceId, rToken.DevicePlatform,
          rToken.TransportMode, OUString::number(rToken.Port), rToken.TokenHash,
          JoinFields(rToken.RequiredEvidence), JoinFields(rToken.EvidenceIds), rToken.AuditLogRef,
          rToken.TenantContextRef, rToken.SyncMessageRef });
    if (!AppendUtf8Line(m_sCompanionStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatCompanionApprovalResult aResult;
    aResult.Success = true;
    aResult.ObjectId = rToken.PairingId;
    aResult.Message
        = u"companion-pairing-token-published pairing-id="_ustr + rToken.PairingId
          + u" tenant="_ustr + rToken.TenantId + u" workspace="_ustr + rToken.WorkspaceId
          + u" device-id="_ustr + rToken.ProposedDeviceId
          + u" transport="_ustr + rToken.TransportMode
          + u" ttlSeconds="_ustr + OUString::number(rToken.TtlSeconds)
          + u" sessionTtlHours=24 device-binding=true pin-confirmation=true"_ustr
          + u" biometric-enrollment=true mTLSRequired=true revocable=true"_ustr
          + u" storesSecret=false storesDocumentContent=false storesDiffSummaryOnly=true"_ustr
          + u" allowApprovalOffline=false metadata-only=true auditLogRequired=true"_ustr
          + u" sync-message-ref="_ustr + rToken.SyncMessageRef
          + u" companion-server-runtime=not-started pairing-listener-runtime=not-started"_ustr
          + u" push-gateway-runtime=not-started apns-fcm-bridge=not-started"_ustr;
    return aResult;
}

AIChatCompanionApprovalResult AIChatCompanionApprovalRuntime::PublishDiffSummary(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatCompanionDiffSummary& rSummary) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);
    if (!IsDiffSummaryShapeAllowed(rSummary) || rSummary.TenantId != rContext.TenantId
        || rSummary.WorkspaceId != rContext.WorkspaceId)
        return MakeDeniedResult(u"invalid-diff-summary-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"diff-summary"_ustr,
        { rSummary.SummaryId, rSummary.SchemaVersion, rSummary.CreatedAt, rSummary.TaskId,
          rSummary.StepResultId, rSummary.TenantId, rSummary.WorkspaceId, rSummary.Surface,
          rSummary.SummaryKind, rSummary.ApplyPlanRef, rSummary.ActionKind,
          OUString::number(rSummary.ActionCount), rSummary.PreviewHash,
          rSummary.HumanSummaryHash, rSummary.RiskLevel, rSummary.SnippetStorage,
          JoinFields(rSummary.ChangedObjectRefs), JoinFields(rSummary.RequiredEvidence),
          JoinFields(rSummary.EvidenceIds), rSummary.AuditLogRef, rSummary.TenantContextRef,
          rSummary.SyncMessageRef });
    if (!AppendUtf8Line(m_sCompanionStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatCompanionApprovalResult aResult;
    aResult.Success = true;
    aResult.ObjectId = rSummary.SummaryId;
    aResult.Message
        = u"companion-diff-summary-published summary-id="_ustr + rSummary.SummaryId
          + u" task-id="_ustr + rSummary.TaskId + u" surface="_ustr + rSummary.Surface
          + u" action-kind="_ustr + rSummary.ActionKind
          + u" preview-hash="_ustr + rSummary.PreviewHash
          + u" viewOnly=true canEdit=false mobileParsesApplyPlan=false"_ustr
          + u" storesDocumentContent=false containsOriginalText=false hashAlgorithm=sha256"_ustr
          + u" requiresApproval=true no-document-mutation=true no-apply-plan-execution=true"_ustr
          + u" metadata-only=true evidence-linked=true sync-message-ref="_ustr
          + rSummary.SyncMessageRef;
    return aResult;
}

AIChatCompanionApprovalResult AIChatCompanionApprovalRuntime::PublishApprovalRequest(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatCompanionApprovalRequest& rRequest) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);
    if (!IsApprovalRequestShapeAllowed(rRequest) || rRequest.TenantId != rContext.TenantId
        || rRequest.WorkspaceId != rContext.WorkspaceId || rRequest.ActorId != rScope.UserId)
        return MakeDeniedResult(u"invalid-approval-request-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"approval-request"_ustr,
        { rRequest.RequestId, rRequest.SchemaVersion, rRequest.CreatedAt, rRequest.ExpiresAt,
          rRequest.TaskId, rRequest.SummaryId, rRequest.PairingId, rRequest.TenantId,
          rRequest.WorkspaceId, rRequest.ActorId, rRequest.RequestState,
          JoinFields(rRequest.AvailableActions), rRequest.TransportMode,
          OUString::number(rRequest.LocalGatewayPort), rRequest.PublicEgress ? u"true"_ustr : u"false"_ustr,
          rRequest.CloudPushOptIn ? u"true"_ustr : u"false"_ustr,
          JoinFields(rRequest.RequiredEvidence), JoinFields(rRequest.EvidenceIds),
          rRequest.AuditLogRef, rRequest.TenantContextRef, rRequest.SyncMessageRef,
          rRequest.ReviewItemRef });
    if (!AppendUtf8Line(m_sCompanionStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatCompanionApprovalResult aResult;
    aResult.Success = true;
    aResult.ObjectId = rRequest.RequestId;
    aResult.Message
        = u"companion-approval-request-published request-id="_ustr + rRequest.RequestId
          + u" summary-id="_ustr + rRequest.SummaryId + u" pairing-id="_ustr + rRequest.PairingId
          + u" actor-id="_ustr + rRequest.ActorId + u" channel=companion"_ustr
          + u" request-state="_ustr + rRequest.RequestState
          + u" requiresOnline=true biometricRequired=true secondConfirmRequired=true"_ustr
          + u" mobileMayEdit=false decisionWritesAudit=true allowApprovalOffline=false"_ustr
          + u" review-item-ref="_ustr + rRequest.ReviewItemRef
          + u" auditLogRequired=true evidenceRequired=true metadata-only=true"_ustr
          + u" no-document-mutation=true no-apply-plan-execution=true"_ustr
          + u" companion-server-runtime=not-started push-gateway-runtime=not-started"_ustr;
    return aResult;
}

AIChatCompanionApprovalResult AIChatCompanionApprovalRuntime::RecordApprovalDecision(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatCompanionApprovalRequest& rRequest,
    const AIChatCompanionApprovalDecision& rDecision) const
{
    if (!ScopeAllowed(rContext, rScope))
        return MakeDeniedResult(u"tenant-scope-invalid"_ustr);
    if (!IsApprovalRequestShapeAllowed(rRequest)
        || !IsApprovalDecisionShapeAllowed(rRequest, rDecision))
        return MakeDeniedResult(u"invalid-approval-decision-shape"_ustr);

    const OUString sLine = MakeStoreLine(
        u"approval-decision"_ustr,
        { rDecision.DecisionId, rDecision.RequestId, rDecision.SummaryId, rDecision.PairingId,
          rDecision.CreatedAt, rDecision.ActorId, rDecision.Decision, rDecision.EvidenceId,
          rDecision.HashReference, rDecision.AuditLogRef, rDecision.SyncMessageRef,
          rDecision.BiometricConfirmed ? u"true"_ustr : u"false"_ustr,
          rDecision.SecondConfirmCompleted ? u"true"_ustr : u"false"_ustr });
    if (!AppendUtf8Line(m_sCompanionStoreUrl, sLine))
        return MakeDeniedResult(u"store-write-failed"_ustr);

    AIChatCompanionApprovalResult aResult;
    aResult.Success = true;
    aResult.ObjectId = rDecision.DecisionId;
    aResult.Message
        = u"companion-approval-decision-recorded decision-id="_ustr + rDecision.DecisionId
          + u" request-id="_ustr + rDecision.RequestId
          + u" decision="_ustr + rDecision.Decision
          + u" evidence-id="_ustr + rDecision.EvidenceId
          + u" hash-reference="_ustr + rDecision.HashReference
          + u" audit-log-ref="_ustr + rDecision.AuditLogRef
          + u" sync-message-ref="_ustr + rDecision.SyncMessageRef
          + u" biometricConfirmed=true secondConfirmCompleted=true"_ustr
          + u" appliesDocumentChange=false mobileMayEdit=false"_ustr
          + u" audit-written=true metadata-only=true no-document-mutation=true"_ustr
          + u" no-apply-plan-execution=true remote-transport-runtime=not-started"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
