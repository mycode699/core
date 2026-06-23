/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W8/M6: local cloud sync runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatLocalCloudSyncRuntime.hxx"

#include "AIChatKnowledgeIndexStore.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <unotools/pathoptions.hxx>

#include <algorithm>
#include <string_view>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral LOCAL_CLOUD_SYNC_DIR_NAME = u"kqoffice-v3-ai-local-cloud-sync";
constexpr OUStringLiteral LOCAL_CLOUD_SYNC_FILE_NAME = u"sync-messages.tsv";
constexpr sal_uInt64 MAX_LOCAL_CLOUD_SYNC_BYTES = 2 * 1024 * 1024;
constexpr sal_Int32 W8_SYNC_PORT = 17802;

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

OUString UnescapeField(std::u16string_view aValue)
{
    OUStringBuffer aBuffer;
    for (size_t i = 0; i < aValue.size(); ++i)
    {
        if (aValue[i] != u'\\' || i + 1 >= aValue.size())
        {
            aBuffer.append(aValue[i]);
            continue;
        }

        const char16_t cNext = aValue[++i];
        switch (cNext)
        {
            case u'n':
                aBuffer.append(u'\n');
                break;
            case u'r':
                aBuffer.append(u'\r');
                break;
            case u't':
                aBuffer.append(u'\t');
                break;
            case u'\\':
                aBuffer.append(u'\\');
                break;
            default:
                aBuffer.append(cNext);
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

std::vector<OUString> SplitFields(const OUString& rValue)
{
    std::vector<OUString> aValues;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rValue.getToken(0, ',', nIndex);
        if (!sField.isEmpty())
            aValues.push_back(sField);
    }
    return aValues;
}

bool ReadUtf8File(const OUString& rUrl, OString& rContent)
{
    osl::File aFile(rUrl);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_LOCAL_CLOUD_SYNC_BYTES)
    {
        aFile.close();
        return false;
    }

    std::vector<char> aBuffer(static_cast<size_t>(nSize));
    sal_uInt64 nRead = 0;
    if (nSize > 0 && aFile.read(aBuffer.data(), nSize, nRead) != osl::FileBase::E_None)
    {
        aFile.close();
        return false;
    }
    aFile.close();

    if (nRead != nSize)
        return false;

    rContent = OString(aBuffer.data(), static_cast<sal_Int32>(aBuffer.size()));
    return true;
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

bool ParseSyncMessageLine(const OUString& rLine, AIChatLocalCloudSyncMessage& rMessage)
{
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 31 || aFields[0].isEmpty())
        return false;

    rMessage.SyncMessageId = aFields[0];
    rMessage.SchemaVersion = aFields[1];
    rMessage.CreatedAt = aFields[2];
    rMessage.TenantId = aFields[3];
    rMessage.WorkspaceId = aFields[4];
    rMessage.Channel = aFields[5];
    rMessage.Direction = aFields[6];
    rMessage.Kind = aFields[7];
    rMessage.Payload.RefType = aFields[8];
    rMessage.Payload.RefId = aFields[9];
    rMessage.Payload.HashReference = aFields[10];
    rMessage.Payload.StoresDocumentContent = aFields[11] == u"true"_ustr;
    rMessage.Payload.ContainsRawPayload = aFields[12] == u"true"_ustr;
    rMessage.Transport.Mode = aFields[13];
    rMessage.Transport.EndpointClass = aFields[14];
    rMessage.Transport.Port = aFields[15].toInt32();
    rMessage.Transport.PublicEgress = aFields[16] == u"true"_ustr;
    rMessage.Transport.MTLSRequired = aFields[17] == u"true"_ustr;
    rMessage.Ordering.Sequence = aFields[18].toInt32();
    rMessage.Ordering.IdempotencyKey = aFields[19];
    rMessage.Ordering.AckRequired = aFields[20] == u"true"_ustr;
    rMessage.Boundary.ServiceMode = aFields[21];
    rMessage.Boundary.StoresDocumentContent = aFields[22] == u"true"_ustr;
    rMessage.Boundary.HashOnly = aFields[23] == u"true"_ustr;
    rMessage.Boundary.DefaultNoPublicEgress = aFields[24] == u"true"_ustr;
    rMessage.RequiredEvidence = SplitFields(aFields[25]);
    rMessage.EvidenceIds = SplitFields(aFields[26]);
    rMessage.AuditLogRef = aFields[27];
    rMessage.PolicyContextRef = aFields[28];
    rMessage.AuditChainRef = aFields[29];
    rMessage.TenantContextRef = aFields[30];
    return true;
}

OUString SerializeSyncMessageLine(const AIChatLocalCloudSyncMessage& rMessage)
{
    return EscapeField(rMessage.SyncMessageId) + u"\t"_ustr + EscapeField(rMessage.SchemaVersion)
           + u"\t"_ustr + EscapeField(rMessage.CreatedAt) + u"\t"_ustr
           + EscapeField(rMessage.TenantId) + u"\t"_ustr + EscapeField(rMessage.WorkspaceId)
           + u"\t"_ustr + EscapeField(rMessage.Channel) + u"\t"_ustr
           + EscapeField(rMessage.Direction) + u"\t"_ustr + EscapeField(rMessage.Kind)
           + u"\t"_ustr + EscapeField(rMessage.Payload.RefType) + u"\t"_ustr
           + EscapeField(rMessage.Payload.RefId) + u"\t"_ustr
           + EscapeField(rMessage.Payload.HashReference) + u"\t"_ustr
           + EscapeField(rMessage.Payload.StoresDocumentContent ? u"true"_ustr : u"false"_ustr)
           + u"\t"_ustr
           + EscapeField(rMessage.Payload.ContainsRawPayload ? u"true"_ustr : u"false"_ustr)
           + u"\t"_ustr + EscapeField(rMessage.Transport.Mode) + u"\t"_ustr
           + EscapeField(rMessage.Transport.EndpointClass) + u"\t"_ustr
           + EscapeField(OUString::number(rMessage.Transport.Port)) + u"\t"_ustr
           + EscapeField(rMessage.Transport.PublicEgress ? u"true"_ustr : u"false"_ustr)
           + u"\t"_ustr
           + EscapeField(rMessage.Transport.MTLSRequired ? u"true"_ustr : u"false"_ustr)
           + u"\t"_ustr + EscapeField(OUString::number(rMessage.Ordering.Sequence))
           + u"\t"_ustr + EscapeField(rMessage.Ordering.IdempotencyKey) + u"\t"_ustr
           + EscapeField(rMessage.Ordering.AckRequired ? u"true"_ustr : u"false"_ustr)
           + u"\t"_ustr + EscapeField(rMessage.Boundary.ServiceMode) + u"\t"_ustr
           + EscapeField(rMessage.Boundary.StoresDocumentContent ? u"true"_ustr : u"false"_ustr)
           + u"\t"_ustr + EscapeField(rMessage.Boundary.HashOnly ? u"true"_ustr : u"false"_ustr)
           + u"\t"_ustr
           + EscapeField(rMessage.Boundary.DefaultNoPublicEgress ? u"true"_ustr : u"false"_ustr)
           + u"\t"_ustr + EscapeField(JoinFields(rMessage.RequiredEvidence)) + u"\t"_ustr
           + EscapeField(JoinFields(rMessage.EvidenceIds)) + u"\t"_ustr
           + EscapeField(rMessage.AuditLogRef) + u"\t"_ustr
           + EscapeField(rMessage.PolicyContextRef) + u"\t"_ustr
           + EscapeField(rMessage.AuditChainRef) + u"\t"_ustr
           + EscapeField(rMessage.TenantContextRef) + u"\n"_ustr;
}

OUString ExpectedPayloadRefTypeForKind(const OUString& rKind)
{
    if (rKind == u"evidence-sync"_ustr)
        return u"evidence-record"_ustr;
    if (rKind == u"diff-summary-sync"_ustr)
        return u"companion-diff-summary"_ustr;
    if (rKind == u"approval-decision-sync"_ustr)
        return u"companion-approval-request"_ustr;
    if (rKind == u"task-state-sync"_ustr)
        return u"agent-task-state"_ustr;
    return OUString();
}

OUString RequiredEvidenceForKind(const OUString& rKind)
{
    if (rKind == u"diff-summary-sync"_ustr)
        return u"companion-diff-summary"_ustr;
    if (rKind == u"approval-decision-sync"_ustr)
        return u"companion-approval-request"_ustr;
    if (rKind == u"task-state-sync"_ustr)
        return u"agent-task-state"_ustr;
    return OUString();
}
}

AIChatLocalCloudSyncRuntime::AIChatLocalCloudSyncRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + LOCAL_CLOUD_SYNC_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sSyncMessageUrl = m_sStorageRootUrl + u"/"_ustr + LOCAL_CLOUD_SYNC_FILE_NAME;
}

bool AIChatLocalCloudSyncRuntime::IsSyncMessageIdAllowed(const OUString& rSyncMessageId)
{
    return rSyncMessageId.startsWith(u"sync-"_ustr) && IsLowerHex(rSyncMessageId.copy(5), 16);
}

bool AIChatLocalCloudSyncRuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return AIChatAuditLogRuntime::IsTimestampAllowed(rTimestamp);
}

bool AIChatLocalCloudSyncRuntime::IsChannelAllowed(const OUString& rChannel)
{
    return rChannel == u"desktop-to-sync"_ustr || rChannel == u"sync-to-companion"_ustr
           || rChannel == u"companion-to-sync"_ustr || rChannel == u"sync-to-desktop"_ustr;
}

bool AIChatLocalCloudSyncRuntime::IsDirectionAllowed(const OUString& rDirection)
{
    return rDirection == u"upload"_ustr || rDirection == u"download"_ustr
           || rDirection == u"ack"_ustr;
}

bool AIChatLocalCloudSyncRuntime::IsKindAllowed(const OUString& rKind)
{
    return rKind == u"evidence-sync"_ustr || rKind == u"diff-summary-sync"_ustr
           || rKind == u"approval-decision-sync"_ustr || rKind == u"task-state-sync"_ustr;
}

bool AIChatLocalCloudSyncRuntime::IsPayloadRefTypeAllowed(const OUString& rRefType)
{
    return rRefType == u"evidence-record"_ustr || rRefType == u"companion-diff-summary"_ustr
           || rRefType == u"companion-approval-request"_ustr
           || rRefType == u"agent-task-state"_ustr;
}

bool AIChatLocalCloudSyncRuntime::IsPayloadRefIdAllowed(const OUString& rRefId)
{
    const sal_Int32 nDash = rRefId.indexOf('-');
    if (nDash <= 0)
        return false;
    const OUString sPrefix = rRefId.copy(0, nDash);
    return (sPrefix == u"ev"_ustr || sPrefix == u"cds"_ustr || sPrefix == u"car"_ustr
            || sPrefix == u"agt"_ustr)
           && IsLowerHex(rRefId.copy(nDash + 1), 16);
}

bool AIChatLocalCloudSyncRuntime::IsPayloadShapeAllowed(
    const AIChatLocalCloudSyncPayloadRef& rPayload)
{
    return IsPayloadRefTypeAllowed(rPayload.RefType) && IsPayloadRefIdAllowed(rPayload.RefId)
           && AIChatTenantContextRuntime::IsHashReferenceAllowed(rPayload.HashReference)
           && !rPayload.StoresDocumentContent && !rPayload.ContainsRawPayload;
}

bool AIChatLocalCloudSyncRuntime::IsTransportModeAllowed(const OUString& rMode)
{
    return rMode == u"local-socket"_ustr || rMode == u"lan-grpc"_ustr;
}

bool AIChatLocalCloudSyncRuntime::IsEndpointClassAllowed(const OUString& rEndpointClass)
{
    return rEndpointClass == u"loopback"_ustr || rEndpointClass == u"private-lan"_ustr;
}

bool AIChatLocalCloudSyncRuntime::IsTransportShapeAllowed(
    const AIChatLocalCloudSyncTransport& rTransport)
{
    if (!IsTransportModeAllowed(rTransport.Mode)
        || !IsEndpointClassAllowed(rTransport.EndpointClass) || rTransport.Port != W8_SYNC_PORT
        || rTransport.PublicEgress || !rTransport.MTLSRequired)
        return false;

    if (rTransport.Mode == u"local-socket"_ustr)
        return rTransport.EndpointClass == u"loopback"_ustr;
    if (rTransport.Mode == u"lan-grpc"_ustr)
        return rTransport.EndpointClass == u"private-lan"_ustr;
    return false;
}

bool AIChatLocalCloudSyncRuntime::IsOrderingShapeAllowed(
    const AIChatLocalCloudSyncOrdering& rOrdering)
{
    return rOrdering.Sequence >= 1 && rOrdering.Sequence <= 2147483647
           && rOrdering.IdempotencyKey.startsWith(u"idem-"_ustr)
           && IsLowerHex(rOrdering.IdempotencyKey.copy(5), 16) && rOrdering.AckRequired;
}

bool AIChatLocalCloudSyncRuntime::IsBoundaryShapeAllowed(
    const AIChatLocalCloudSyncBoundary& rBoundary)
{
    return AIChatTenantContextRuntime::IsServiceModeAllowed(rBoundary.ServiceMode)
           && !rBoundary.StoresDocumentContent && rBoundary.HashOnly
           && rBoundary.DefaultNoPublicEgress;
}

bool AIChatLocalCloudSyncRuntime::IsRequiredEvidenceAllowed(const OUString& rEvidence)
{
    return rEvidence == u"localcloud-sync-message"_ustr || rEvidence == u"evidence-record"_ustr
           || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"companion-diff-summary"_ustr
           || rEvidence == u"companion-approval-request"_ustr
           || rEvidence == u"agent-task-state"_ustr;
}

bool AIChatLocalCloudSyncRuntime::IsMessageShapeAllowed(
    const AIChatLocalCloudSyncMessage& rMessage)
{
    if (!IsSyncMessageIdAllowed(rMessage.SyncMessageId)
        || rMessage.SchemaVersion != u"v3-sync-message/0.1"_ustr
        || !IsTimestampAllowed(rMessage.CreatedAt)
        || !AIChatTenantContextRuntime::IsTenantIdAllowed(rMessage.TenantId)
        || !AIChatTenantContextRuntime::IsWorkspaceIdAllowed(rMessage.WorkspaceId)
        || !IsChannelAllowed(rMessage.Channel) || !IsDirectionAllowed(rMessage.Direction)
        || !IsKindAllowed(rMessage.Kind) || !IsPayloadShapeAllowed(rMessage.Payload)
        || rMessage.Payload.RefType != ExpectedPayloadRefTypeForKind(rMessage.Kind)
        || !IsTransportShapeAllowed(rMessage.Transport)
        || !IsOrderingShapeAllowed(rMessage.Ordering)
        || !IsBoundaryShapeAllowed(rMessage.Boundary)
        || !rMessage.AuditLogRef.startsWith(u"audit-log-entry:"_ustr)
        || !rMessage.PolicyContextRef.startsWith(u"policy-context:"_ustr)
        || !rMessage.AuditChainRef.startsWith(u"audit-chain:"_ustr)
        || !rMessage.TenantContextRef.startsWith(u"tenant-context:"_ustr))
        return false;

    if (!ContainsString(rMessage.RequiredEvidence, u"localcloud-sync-message"_ustr)
        || !ContainsString(rMessage.RequiredEvidence, u"evidence-record"_ustr)
        || !ContainsString(rMessage.RequiredEvidence, u"audit-log-entry"_ustr))
        return false;

    const OUString sKindEvidence = RequiredEvidenceForKind(rMessage.Kind);
    if (!sKindEvidence.isEmpty() && !ContainsString(rMessage.RequiredEvidence, sKindEvidence))
        return false;

    for (const OUString& rRequired : rMessage.RequiredEvidence)
    {
        if (!IsRequiredEvidenceAllowed(rRequired))
            return false;
    }
    if (rMessage.EvidenceIds.empty())
        return false;
    for (const OUString& rEvidenceId : rMessage.EvidenceIds)
    {
        if (!AIChatTenantContextRuntime::IsEvidenceIdAllowed(rEvidenceId))
            return false;
    }

    if (rMessage.Boundary.ServiceMode == u"offline"_ustr)
        return rMessage.Transport.Mode == u"local-socket"_ustr
               && rMessage.Transport.EndpointClass == u"loopback"_ustr;
    if (rMessage.Boundary.ServiceMode == u"private"_ustr)
        return rMessage.Transport.Mode == u"lan-grpc"_ustr
               && rMessage.Transport.EndpointClass == u"private-lan"_ustr;
    return false;
}

OUString AIChatLocalCloudSyncRuntime::MakeSyncMessageId(
    const AIChatTenantActionScope& rScope, const OUString& rKind, sal_Int32 nSequence,
    const OUString& rCreatedAt)
{
    return u"sync-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rScope.TenantId + u":"_ustr + rScope.WorkspaceId + u":"_ustr
                 + rScope.UserId + u":"_ustr + rKind + u":"_ustr + OUString::number(nSequence)
                 + u":"_ustr + rCreatedAt)
                 .copy(0, 16);
}

OUString AIChatLocalCloudSyncRuntime::MakeIdempotencyKey(
    const AIChatLocalCloudSyncMessage& rMessage)
{
    return u"idem-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rMessage.SyncMessageId + u":"_ustr + rMessage.Payload.RefId + u":"_ustr
                 + OUString::number(rMessage.Ordering.Sequence))
                 .copy(0, 16);
}

OUString AIChatLocalCloudSyncRuntime::MakeAuditLogRef(const AIChatAuditLogEntry& rAuditEntry)
{
    return u"audit-log-entry:"_ustr + rAuditEntry.AuditId + u":"_ustr + rAuditEntry.EntryHash;
}

OUString AIChatLocalCloudSyncRuntime::MakeTenantContextRef(const AIChatTenantContext& rContext)
{
    return u"tenant-context:"_ustr + rContext.ContextId;
}

std::vector<AIChatLocalCloudSyncMessage> AIChatLocalCloudSyncRuntime::LoadMessages() const
{
    std::vector<AIChatLocalCloudSyncMessage> aMessages;
    OString sContent;
    if (!ReadUtf8File(m_sSyncMessageUrl, sContent) || sContent.isEmpty())
        return aMessages;

    const OUString sText = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    sal_Int32 nLineIndex = 0;
    while (nLineIndex >= 0)
    {
        const OUString sLine = sText.getToken(0, '\n', nLineIndex);
        if (sLine.isEmpty())
            continue;

        AIChatLocalCloudSyncMessage aMessage;
        if (ParseSyncMessageLine(sLine, aMessage) && IsMessageShapeAllowed(aMessage))
            aMessages.push_back(aMessage);
    }
    return aMessages;
}

AIChatLocalCloudSyncResult AIChatLocalCloudSyncRuntime::AppendMessage(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatLocalCloudSyncMessage& rMessage) const
{
    AIChatLocalCloudSyncResult aResult;

    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    if (!aScopeResult.Success
        || (rScope.TargetType != u"companion"_ustr && rScope.TargetType != u"local-cloud"_ustr)
        || (rScope.Surface != u"companion"_ustr && rScope.Surface != u"local-cloud"_ustr))
    {
        aResult.Message
            = u"local-cloud-sync-denied reason=tenant-scope-invalid"_ustr
              + u" fail-closed-user-visible=true no-public-egress=true"_ustr
              + u" metadata-only=true socket-listener-runtime=not-started"_ustr;
        return aResult;
    }

    AIChatLocalCloudSyncMessage aRecord = rMessage;
    if (aRecord.SyncMessageId.isEmpty())
        aRecord.SyncMessageId
            = MakeSyncMessageId(rScope, aRecord.Kind, aRecord.Ordering.Sequence, aRecord.CreatedAt);
    if (aRecord.Ordering.IdempotencyKey.isEmpty())
        aRecord.Ordering.IdempotencyKey = MakeIdempotencyKey(aRecord);
    aRecord.TenantId = rContext.TenantId;
    aRecord.WorkspaceId = rContext.WorkspaceId;
    aRecord.PolicyContextRef = AIChatTenantContextRuntime::MakePolicyContextRef(rContext);
    aRecord.AuditChainRef = AIChatTenantContextRuntime::MakeAuditChainRef(rContext);
    aRecord.TenantContextRef = MakeTenantContextRef(rContext);

    if (!IsMessageShapeAllowed(aRecord))
    {
        aResult.Message
            = u"local-cloud-sync-denied reason=invalid-sync-message-shape"_ustr
              + u" hash-only=true storesDocumentContent=false containsRawPayload=false"_ustr
              + u" ackRequired=true auditLogRequired=true evidenceRecordRequired=true"_ustr
              + u" publicEgress=false defaultNoPublicEgress=true fail-closed-user-visible=true"_ustr;
        return aResult;
    }

    if (!AppendUtf8Line(m_sSyncMessageUrl, SerializeSyncMessageLine(aRecord)))
    {
        aResult.Message = u"local-cloud-sync-denied reason=store-write-failed"_ustr;
        return aResult;
    }

    aResult.Success = true;
    aResult.MessageRecord = aRecord;
    aResult.Message
        = u"local-cloud-sync-message-appended sync-id="_ustr + aRecord.SyncMessageId
          + u" tenant="_ustr + aRecord.TenantId + u" workspace="_ustr + aRecord.WorkspaceId
          + u" channel="_ustr + aRecord.Channel + u" direction="_ustr + aRecord.Direction
          + u" kind="_ustr + aRecord.Kind + u" ref-type="_ustr + aRecord.Payload.RefType
          + u" ref-id="_ustr + aRecord.Payload.RefId
          + u" hash-reference="_ustr + aRecord.Payload.HashReference
          + u" audit-log-ref="_ustr + aRecord.AuditLogRef
          + u" policy-context-ref="_ustr + aRecord.PolicyContextRef
          + u" audit-chain-ref="_ustr + aRecord.AuditChainRef
          + u" tenant-context-ref="_ustr + aRecord.TenantContextRef
          + u" sequence="_ustr + OUString::number(aRecord.Ordering.Sequence)
          + u" idempotency-key="_ustr + aRecord.Ordering.IdempotencyKey
          + u" localcloud-sync-message=true append-only=true metadata-only=true"_ustr
          + u" local-socket-or-private-lan=true loopback-private-lan-default=true"_ustr
          + u" publicEgress=false defaultNoPublicEgress=true optInCloudEgress=false"_ustr
          + u" storesDocumentContent=false containsRawPayload=false hashOnly=true"_ustr
          + u" ackRequired=true mTLSRequired=true port=17802"_ustr
          + u" socket-listener-runtime=not-started cloud-service-runtime=not-started"_ustr
          + u" background-daemon-runtime=not-started remote-account-sync=not-started"_ustr
          + u" companion-protocol-runtime=not-started admin-ui-runtime=not-started"_ustr;
    return aResult;
}

AIChatLocalCloudSyncResult AIChatLocalCloudSyncRuntime::AppendAcknowledgement(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatLocalCloudSyncMessage& rSourceMessage, const OUString& rCreatedAt,
    sal_Int32 nSequence, const OUString& rEvidenceId) const
{
    if (!AIChatTenantContextRuntime::IsEvidenceIdAllowed(rEvidenceId))
    {
        AIChatLocalCloudSyncResult aResult;
        aResult.Message
            = u"local-cloud-sync-denied reason=invalid-ack-evidence"_ustr
              + u" ackRequired=true evidenceRecordRequired=true fail-closed-user-visible=true"_ustr;
        return aResult;
    }

    AIChatLocalCloudSyncMessage aAck;
    aAck.SchemaVersion = u"v3-sync-message/0.1"_ustr;
    aAck.CreatedAt = rCreatedAt;
    aAck.TenantId = rContext.TenantId;
    aAck.WorkspaceId = rContext.WorkspaceId;
    aAck.Channel = rSourceMessage.Channel == u"desktop-to-sync"_ustr ? u"sync-to-desktop"_ustr
                                                                      : u"sync-to-companion"_ustr;
    aAck.Direction = u"ack"_ustr;
    aAck.Kind = rSourceMessage.Kind;
    aAck.Payload = rSourceMessage.Payload;
    aAck.Transport.Mode = rSourceMessage.Transport.Mode;
    aAck.Transport.EndpointClass = rSourceMessage.Transport.EndpointClass;
    aAck.Transport.Port = W8_SYNC_PORT;
    aAck.Transport.PublicEgress = false;
    aAck.Transport.MTLSRequired = true;
    aAck.Ordering.Sequence = nSequence;
    aAck.Ordering.AckRequired = true;
    aAck.Boundary = rSourceMessage.Boundary;
    aAck.Boundary.StoresDocumentContent = false;
    aAck.Boundary.HashOnly = true;
    aAck.Boundary.DefaultNoPublicEgress = true;
    aAck.RequiredEvidence = rSourceMessage.RequiredEvidence;
    aAck.EvidenceIds = rSourceMessage.EvidenceIds;
    if (!ContainsString(aAck.EvidenceIds, rEvidenceId))
        aAck.EvidenceIds.push_back(rEvidenceId);
    aAck.AuditLogRef = rSourceMessage.AuditLogRef;
    aAck.SyncMessageId = MakeSyncMessageId(rScope, aAck.Kind, nSequence, rCreatedAt);
    aAck.Ordering.IdempotencyKey = MakeIdempotencyKey(aAck);

    return AppendMessage(rContext, rScope, aAck);
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
