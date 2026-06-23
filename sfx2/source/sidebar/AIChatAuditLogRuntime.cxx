/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W4/M6: audit log runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatAuditLogRuntime.hxx"

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
constexpr OUStringLiteral AUDIT_LOG_DIR_NAME = u"kqoffice-v3-ai-audit-log";
constexpr OUStringLiteral AUDIT_LOG_FILE_NAME = u"audit.tsv";
constexpr sal_uInt64 MAX_AUDIT_LOG_BYTES = 2 * 1024 * 1024;
constexpr sal_Unicode APPROVAL_ENTRY_SEPARATOR = u',';
constexpr sal_Unicode APPROVAL_FIELD_SEPARATOR = u'~';

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

OUString EncodeApprovals(const std::vector<AIChatAuditApprovalEntry>& rApprovals)
{
    OUStringBuffer aBuffer;
    for (size_t i = 0; i < rApprovals.size(); ++i)
    {
        if (i > 0)
            aBuffer.append(APPROVAL_ENTRY_SEPARATOR);
        aBuffer.append(rApprovals[i].ApproverId);
        aBuffer.append(APPROVAL_FIELD_SEPARATOR);
        aBuffer.append(rApprovals[i].Decision);
        aBuffer.append(APPROVAL_FIELD_SEPARATOR);
        aBuffer.append(rApprovals[i].Timestamp);
        aBuffer.append(APPROVAL_FIELD_SEPARATOR);
        aBuffer.append(rApprovals[i].EvidenceId);
    }
    return aBuffer.makeStringAndClear();
}

std::vector<AIChatAuditApprovalEntry> DecodeApprovals(const OUString& rValue)
{
    std::vector<AIChatAuditApprovalEntry> aApprovals;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sEncoded = rValue.getToken(0, APPROVAL_ENTRY_SEPARATOR, nIndex);
        if (sEncoded.isEmpty())
            continue;
        sal_Int32 nApprovalIndex = 0;
        AIChatAuditApprovalEntry aEntry;
        aEntry.ApproverId = sEncoded.getToken(0, APPROVAL_FIELD_SEPARATOR, nApprovalIndex);
        aEntry.Decision = sEncoded.getToken(0, APPROVAL_FIELD_SEPARATOR, nApprovalIndex);
        aEntry.Timestamp = sEncoded.getToken(0, APPROVAL_FIELD_SEPARATOR, nApprovalIndex);
        aEntry.EvidenceId = sEncoded.getToken(0, APPROVAL_FIELD_SEPARATOR, nApprovalIndex);
        aApprovals.push_back(aEntry);
    }
    return aApprovals;
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

bool ReadUtf8File(const OUString& rUrl, OString& rContent)
{
    osl::File aFile(rUrl);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_AUDIT_LOG_BYTES)
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

bool ParseAuditLine(const OUString& rLine, AIChatAuditLogEntry& rEntry)
{
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 23 || aFields[0].isEmpty())
        return false;

    rEntry.AuditId = aFields[0];
    rEntry.SchemaVersion = aFields[1];
    rEntry.Timestamp = aFields[2];
    rEntry.TenantId = aFields[3];
    rEntry.WorkspaceId = aFields[4];
    rEntry.ActorId = aFields[5];
    rEntry.ActorRole = aFields[6];
    rEntry.ActionType = aFields[7];
    rEntry.TargetRef = aFields[8];
    rEntry.ServiceMode = aFields[9];
    rEntry.DataClass = aFields[10];
    rEntry.PublicEgress = aFields[11] == u"true"_ustr;
    rEntry.EvidenceId = aFields[12];
    rEntry.PolicyDecision = aFields[13];
    rEntry.ApprovalChain = DecodeApprovals(aFields[14]);
    rEntry.StoresDocumentContent = aFields[15] == u"true"_ustr;
    rEntry.PromptStorage = aFields[16];
    rEntry.HashAlgorithm = aFields[17];
    rEntry.PreviousHash = aFields[18];
    rEntry.EntryHash = aFields[19];
    rEntry.PolicyContextRef = aFields[20];
    rEntry.AuditChainRef = aFields[21];
    rEntry.HashReference = aFields[22];
    return true;
}
}

AIChatAuditLogRuntime::AIChatAuditLogRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + AUDIT_LOG_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sAuditLogUrl = m_sStorageRootUrl + u"/"_ustr + AUDIT_LOG_FILE_NAME;
}

bool AIChatAuditLogRuntime::IsAuditIdAllowed(const OUString& rAuditId)
{
    return rAuditId.startsWith(u"aud-"_ustr) && IsLowerHex(rAuditId.copy(4), 16);
}

bool AIChatAuditLogRuntime::IsTimestampAllowed(const OUString& rTimestamp)
{
    return rTimestamp.getLength() == 20 && rTimestamp[4] == u'-' && rTimestamp[7] == u'-'
           && rTimestamp[10] == u'T' && rTimestamp[13] == u':' && rTimestamp[16] == u':'
           && rTimestamp[19] == u'Z';
}

bool AIChatAuditLogRuntime::IsActionTypeAllowed(const OUString& rActionType)
{
    return rActionType == u"chat"_ustr || rActionType == u"patch-apply"_ustr
           || rActionType == u"connector-fetch"_ustr || rActionType == u"kb-query"_ustr
           || rActionType == u"agent-step"_ustr;
}

bool AIChatAuditLogRuntime::IsPolicyDecisionAllowed(const OUString& rPolicyDecision)
{
    return rPolicyDecision == u"allow"_ustr || rPolicyDecision == u"deny"_ustr
           || rPolicyDecision == u"require-approval"_ustr;
}

bool AIChatAuditLogRuntime::IsApprovalDecisionAllowed(const OUString& rDecision)
{
    return rDecision == u"approved"_ustr || rDecision == u"rejected"_ustr;
}

bool AIChatAuditLogRuntime::IsPromptStorageAllowed(const OUString& rPromptStorage)
{
    return rPromptStorage == u"none"_ustr || rPromptStorage == u"hash-only"_ustr;
}

bool AIChatAuditLogRuntime::IsChainHashAllowed(const OUString& rHash)
{
    return rHash == u"GENESIS"_ustr || IsLowerHex(rHash, 64);
}

OUString AIChatAuditLogRuntime::NormalizeActionType(const OUString& rTargetType)
{
    if (rTargetType == u"connector"_ustr)
        return u"connector-fetch"_ustr;
    if (rTargetType == u"knowledge-index"_ustr)
        return u"kb-query"_ustr;
    if (rTargetType == u"provider"_ustr || rTargetType == u"audit"_ustr
        || rTargetType == u"companion"_ustr)
        return u"chat"_ustr;
    return rTargetType;
}

OUString AIChatAuditLogRuntime::NormalizePolicyDecision(const OUString& rDecision)
{
    if (rDecision == u"require-evidence"_ustr)
        return u"allow"_ustr;
    return rDecision;
}

bool AIChatAuditLogRuntime::IsEntryShapeAllowed(const AIChatAuditLogEntry& rEntry)
{
    if (!IsAuditIdAllowed(rEntry.AuditId)
        || rEntry.SchemaVersion != u"v3-audit-log-entry/0.1"_ustr
        || !IsTimestampAllowed(rEntry.Timestamp)
        || !AIChatTenantContextRuntime::IsTenantIdAllowed(rEntry.TenantId)
        || !AIChatTenantContextRuntime::IsWorkspaceIdAllowed(rEntry.WorkspaceId)
        || !AIChatTenantContextRuntime::IsUserIdAllowed(rEntry.ActorId)
        || !AIChatTenantContextRuntime::IsUserRoleAllowed(rEntry.ActorRole)
        || !IsActionTypeAllowed(rEntry.ActionType) || rEntry.TargetRef.isEmpty()
        || rEntry.TargetRef.getLength() > 160
        || !AIChatTenantContextRuntime::IsServiceModeAllowed(rEntry.ServiceMode)
        || !AIChatTenantContextRuntime::IsDataClassAllowed(rEntry.DataClass)
        || rEntry.PublicEgress || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rEntry.EvidenceId)
        || !IsPolicyDecisionAllowed(rEntry.PolicyDecision) || rEntry.StoresDocumentContent
        || !IsPromptStorageAllowed(rEntry.PromptStorage) || rEntry.HashAlgorithm != u"sha256"_ustr
        || !IsChainHashAllowed(rEntry.PreviousHash) || !IsLowerHex(rEntry.EntryHash, 64)
        || !rEntry.PolicyContextRef.startsWith(u"policy-context:"_ustr)
        || !rEntry.AuditChainRef.startsWith(u"audit-chain:"_ustr)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rEntry.HashReference))
        return false;

    if (rEntry.PolicyDecision == u"require-approval"_ustr && rEntry.ApprovalChain.empty())
        return false;
    for (const AIChatAuditApprovalEntry& rApproval : rEntry.ApprovalChain)
    {
        if (!AIChatTenantContextRuntime::IsUserIdAllowed(rApproval.ApproverId)
            || !IsApprovalDecisionAllowed(rApproval.Decision)
            || !IsTimestampAllowed(rApproval.Timestamp)
            || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rApproval.EvidenceId))
            return false;
    }
    return true;
}

OUString AIChatAuditLogRuntime::MakeAuditId(const AIChatTenantActionScope& rScope,
                                            const AIChatPolicyDecision& rDecision,
                                            const OUString& rTimestamp)
{
    return u"aud-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rScope.TenantId + u":"_ustr + rScope.WorkspaceId + u":"_ustr
                 + rScope.TargetType + u":"_ustr + rDecision.RuleId + u":"_ustr + rTimestamp)
                 .copy(0, 16);
}

OUString AIChatAuditLogRuntime::MakeEntryHash(const AIChatAuditLogEntry& rEntry)
{
    const OUString sSeed
        = rEntry.AuditId + u":"_ustr + rEntry.SchemaVersion + u":"_ustr + rEntry.Timestamp
          + u":"_ustr + rEntry.TenantId + u":"_ustr + rEntry.WorkspaceId + u":"_ustr
          + rEntry.ActorId + u":"_ustr + rEntry.ActorRole + u":"_ustr + rEntry.ActionType
          + u":"_ustr + rEntry.TargetRef + u":"_ustr + rEntry.ServiceMode + u":"_ustr
          + rEntry.DataClass + u":"_ustr + (rEntry.PublicEgress ? u"true"_ustr : u"false"_ustr)
          + u":"_ustr + rEntry.EvidenceId + u":"_ustr + rEntry.PolicyDecision + u":"_ustr
          + EncodeApprovals(rEntry.ApprovalChain) + u":"_ustr
          + (rEntry.StoresDocumentContent ? u"true"_ustr : u"false"_ustr) + u":"_ustr
          + rEntry.PromptStorage + u":"_ustr + rEntry.HashAlgorithm + u":"_ustr
          + rEntry.PreviousHash + u":"_ustr + rEntry.PolicyContextRef + u":"_ustr
          + rEntry.AuditChainRef + u":"_ustr + rEntry.HashReference;
    return AIChatKnowledgeIndexStore::MakeMetadataHash(sSeed);
}

std::vector<AIChatAuditLogEntry> AIChatAuditLogRuntime::LoadEntries() const
{
    std::vector<AIChatAuditLogEntry> aEntries;
    OString sContent;
    if (!ReadUtf8File(m_sAuditLogUrl, sContent) || sContent.isEmpty())
        return aEntries;

    const OUString sText = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    sal_Int32 nLineIndex = 0;
    while (nLineIndex >= 0)
    {
        const OUString sLine = sText.getToken(0, '\n', nLineIndex);
        AIChatAuditLogEntry aEntry;
        if (ParseAuditLine(sLine, aEntry))
            aEntries.push_back(aEntry);
    }
    return aEntries;
}

AIChatAuditLogResult AIChatAuditLogRuntime::ValidateHashChain() const
{
    AIChatAuditLogResult aResult;
    OString sContent;
    if (!ReadUtf8File(m_sAuditLogUrl, sContent) || sContent.isEmpty())
    {
        aResult.Success = true;
        aResult.Message = u"audit-log-chain-valid entries=0 append-only=true hash-chain=true metadata-only=true"_ustr
                          + u" local-audit-sink-runtime=not-started public-egress=false"_ustr;
        return aResult;
    }

    const OUString sText = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    std::vector<AIChatAuditLogEntry> aEntries;
    sal_Int32 nLineIndex = 0;
    while (nLineIndex >= 0)
    {
        const OUString sLine = sText.getToken(0, '\n', nLineIndex);
        if (sLine.isEmpty())
            continue;

        AIChatAuditLogEntry aEntry;
        if (!ParseAuditLine(sLine, aEntry))
        {
            aResult.Message
                = u"audit-log-chain-failed reason=tampered-or-invalid-entry"_ustr
                  + u" append-only=true hash-chain=true fail-closed-user-visible=true"_ustr
                  + u" metadata-only=true storesDocumentContent=false"_ustr;
            return aResult;
        }
        aEntries.push_back(aEntry);
    }

    OUString sPreviousHash = u"GENESIS"_ustr;
    for (const AIChatAuditLogEntry& rEntry : aEntries)
    {
        if (!IsEntryShapeAllowed(rEntry) || rEntry.PreviousHash != sPreviousHash
            || rEntry.EntryHash != MakeEntryHash(rEntry))
        {
            aResult.Message
                = u"audit-log-chain-failed reason=tampered-or-invalid-entry"_ustr
                  + u" append-only=true hash-chain=true fail-closed-user-visible=true"_ustr
                  + u" metadata-only=true storesDocumentContent=false"_ustr;
            return aResult;
        }
        sPreviousHash = rEntry.EntryHash;
        aResult.Entry = rEntry;
    }

    aResult.Success = true;
    aResult.Message = u"audit-log-chain-valid entries="_ustr
                      + OUString::number(static_cast<sal_Int32>(aEntries.size()))
                      + u" append-only=true hash-chain=true metadata-only=true"_ustr
                      + u" local-audit-sink-runtime=not-started public-egress=false"_ustr;
    return aResult;
}

AIChatAuditLogResult AIChatAuditLogRuntime::AppendPolicyDecision(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatPolicyDecision& rDecision, const OUString& rTimestamp,
    const std::vector<AIChatAuditApprovalEntry>& rApprovalChain) const
{
    AIChatAuditLogResult aResult;
    if (!rDecision.Success || !AIChatTenantContextRuntime::IsContextShapeAllowed(rContext)
        || !AIChatTenantContextRuntime::IsEvidenceIdAllowed(rDecision.EvidenceId)
        || !AIChatTenantContextRuntime::IsHashReferenceAllowed(rDecision.HashReference)
        || !IsTimestampAllowed(rTimestamp))
    {
        aResult.Message
            = u"audit-log-append-failed reason=invalid-policy-decision-or-context"_ustr
              + u" append-only=true evidence-required=true fail-closed-user-visible=true"_ustr
              + u" metadata-only=true storesDocumentContent=false"_ustr;
        return aResult;
    }

    AIChatAuditLogEntry aEntry;
    aEntry.AuditId = MakeAuditId(rScope, rDecision, rTimestamp);
    aEntry.SchemaVersion = u"v3-audit-log-entry/0.1"_ustr;
    aEntry.Timestamp = rTimestamp;
    aEntry.TenantId = rContext.TenantId;
    aEntry.WorkspaceId = rContext.WorkspaceId;
    aEntry.ActorId = rScope.UserId;
    aEntry.ActorRole = rScope.UserRole;
    aEntry.ActionType = NormalizeActionType(rScope.TargetType);
    aEntry.TargetRef = rScope.Surface;
    aEntry.ServiceMode = rScope.ServiceMode;
    aEntry.DataClass = rScope.DataClass;
    aEntry.PublicEgress = false;
    aEntry.EvidenceId = rDecision.EvidenceId;
    aEntry.PolicyDecision = NormalizePolicyDecision(rDecision.Decision);
    aEntry.ApprovalChain = rApprovalChain;
    aEntry.StoresDocumentContent = false;
    aEntry.PromptStorage = rScope.TargetType == u"connector"_ustr ? u"none"_ustr : u"hash-only"_ustr;
    aEntry.HashAlgorithm = u"sha256"_ustr;
    const std::vector<AIChatAuditLogEntry> aExistingEntries = LoadEntries();
    aEntry.PreviousHash = aExistingEntries.empty() ? u"GENESIS"_ustr : aExistingEntries.back().EntryHash;
    aEntry.PolicyContextRef = rDecision.PolicyContextRef;
    aEntry.AuditChainRef = rDecision.AuditChainRef;
    aEntry.HashReference = rDecision.HashReference;
    aEntry.EntryHash = MakeEntryHash(aEntry);

    if (!IsEntryShapeAllowed(aEntry))
    {
        aResult.Message
            = u"audit-log-append-failed reason=invalid-audit-entry-shape"_ustr
              + u" ev-shape-required=true storesDocumentContent=false"_ustr
              + u" schema-collapse=false metadata-only=true"_ustr;
        return aResult;
    }
    if (!aExistingEntries.empty())
    {
        const AIChatAuditLogResult aChain = ValidateHashChain();
        if (!aChain.Success)
        {
            aResult.Message
                = u"audit-log-append-failed reason=existing-chain-invalid"_ustr
                  + u" tamper-guard=true fail-closed-user-visible=true"_ustr;
            return aResult;
        }
    }

    const OUString sLine
        = EscapeField(aEntry.AuditId) + u"\t"_ustr + EscapeField(aEntry.SchemaVersion)
          + u"\t"_ustr + EscapeField(aEntry.Timestamp) + u"\t"_ustr
          + EscapeField(aEntry.TenantId) + u"\t"_ustr + EscapeField(aEntry.WorkspaceId)
          + u"\t"_ustr + EscapeField(aEntry.ActorId) + u"\t"_ustr
          + EscapeField(aEntry.ActorRole) + u"\t"_ustr + EscapeField(aEntry.ActionType)
          + u"\t"_ustr + EscapeField(aEntry.TargetRef) + u"\t"_ustr
          + EscapeField(aEntry.ServiceMode) + u"\t"_ustr + EscapeField(aEntry.DataClass)
          + u"\t"_ustr + EscapeField(aEntry.PublicEgress ? u"true"_ustr : u"false"_ustr)
          + u"\t"_ustr + EscapeField(aEntry.EvidenceId) + u"\t"_ustr
          + EscapeField(aEntry.PolicyDecision) + u"\t"_ustr
          + EscapeField(EncodeApprovals(aEntry.ApprovalChain)) + u"\t"_ustr
          + EscapeField(aEntry.StoresDocumentContent ? u"true"_ustr : u"false"_ustr)
          + u"\t"_ustr + EscapeField(aEntry.PromptStorage) + u"\t"_ustr
          + EscapeField(aEntry.HashAlgorithm) + u"\t"_ustr + EscapeField(aEntry.PreviousHash)
          + u"\t"_ustr + EscapeField(aEntry.EntryHash) + u"\t"_ustr
          + EscapeField(aEntry.PolicyContextRef) + u"\t"_ustr
          + EscapeField(aEntry.AuditChainRef) + u"\t"_ustr
          + EscapeField(aEntry.HashReference) + u"\n"_ustr;
    if (!AppendUtf8Line(m_sAuditLogUrl, sLine))
    {
        aResult.Message = u"audit-log-append-failed reason=store-write-failed"_ustr;
        return aResult;
    }

    aResult.Success = true;
    aResult.Entry = aEntry;
    aResult.Message
        = u"audit-log-entry-appended audit-id="_ustr + aEntry.AuditId
          + u" tenant="_ustr + aEntry.TenantId + u" workspace="_ustr + aEntry.WorkspaceId
          + u" actor-id="_ustr + aEntry.ActorId + u" action-type="_ustr + aEntry.ActionType
          + u" policy-decision="_ustr + aEntry.PolicyDecision
          + u" evidence-id="_ustr + aEntry.EvidenceId
          + u" previous-hash="_ustr + aEntry.PreviousHash
          + u" entry-hash="_ustr + aEntry.EntryHash
          + u" append-only=true hash-chain=true evidence-linked=true"_ustr
          + u" storesDocumentContent=false promptStorage="_ustr + aEntry.PromptStorage
          + u" schema-collapse=false metadata-only=true public-egress=false"_ustr
          + u" local-audit-sink-runtime=not-started gdpr-delete-runtime=not-started"_ustr
          + u" admin-ui-runtime=not-started"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
