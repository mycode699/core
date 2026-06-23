/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W4/M6: tenant context runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatTenantContextRuntime.hxx"

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
constexpr OUStringLiteral TENANT_CONTEXT_DIR_NAME = u"kqoffice-v3-ai-tenant-context";
constexpr OUStringLiteral TENANT_CONTEXT_FILE_NAME = u"contexts.tsv";
constexpr sal_uInt64 MAX_TENANT_CONTEXT_BYTES = 512 * 1024;

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

OUString EncodeUsers(const std::vector<AIChatTenantUser>& rUsers)
{
    OUStringBuffer aBuffer;
    for (size_t i = 0; i < rUsers.size(); ++i)
    {
        if (i > 0)
            aBuffer.append(u","_ustr);
        aBuffer.append(rUsers[i].UserId);
        aBuffer.append(u":"_ustr);
        aBuffer.append(rUsers[i].Role);
        aBuffer.append(u":"_ustr);
        aBuffer.append(rUsers[i].Status);
    }
    return aBuffer.makeStringAndClear();
}

std::vector<AIChatTenantUser> DecodeUsers(const OUString& rValue)
{
    std::vector<AIChatTenantUser> aUsers;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sEncoded = rValue.getToken(0, ',', nIndex);
        if (sEncoded.isEmpty())
            continue;
        sal_Int32 nUserIndex = 0;
        AIChatTenantUser aUser;
        aUser.UserId = sEncoded.getToken(0, ':', nUserIndex);
        aUser.Role = sEncoded.getToken(0, ':', nUserIndex);
        aUser.Status = sEncoded.getToken(0, ':', nUserIndex);
        aUsers.push_back(aUser);
    }
    return aUsers;
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

bool IsSlug(const OUString& rValue, sal_Int32 nMinLength, sal_Int32 nMaxLength)
{
    if (rValue.getLength() < nMinLength || rValue.getLength() > nMaxLength)
        return false;
    const sal_Unicode cFirst = rValue[0];
    if (!(cFirst >= u'a' && cFirst <= u'z'))
        return false;
    for (sal_Int32 i = 1; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        if (!((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-'))
            return false;
    }
    return true;
}

bool IsUserSlug(const OUString& rValue)
{
    if (rValue.getLength() < 3 || rValue.getLength() > 80)
        return false;
    const sal_Unicode cFirst = rValue[0];
    if (!(cFirst >= u'a' && cFirst <= u'z'))
        return false;
    for (sal_Int32 i = 1; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        if (!((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-'
              || c == u'_' || c == u'.'))
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

bool HasActiveUser(const AIChatTenantContext& rContext, const OUString& rUserId,
                   const OUString& rRole)
{
    return std::any_of(rContext.Users.begin(), rContext.Users.end(),
                       [&rUserId, &rRole](const AIChatTenantUser& rUser) {
                           return rUser.UserId == rUserId && rUser.Role == rRole
                                  && rUser.Status == u"active"_ustr;
                       });
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
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_TENANT_CONTEXT_BYTES)
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

bool ParseContextLine(const OUString& rLine, AIChatTenantContext& rContext)
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

    rContext.ContextId = aFields[0];
    rContext.SchemaVersion = aFields[1];
    rContext.CreatedAt = aFields[2];
    rContext.TenantId = aFields[3];
    rContext.TenantNameHash = aFields[4];
    rContext.TenantPlan = aFields[5];
    rContext.WorkspaceId = aFields[6];
    rContext.WorkspaceNameHash = aFields[7];
    rContext.WorkspaceDataClasses = SplitFields(aFields[8]);
    rContext.Users = DecodeUsers(aFields[9]);
    rContext.DefaultServiceMode = aFields[10];
    rContext.AllowedServiceModes = SplitFields(aFields[11]);
    rContext.PublicEgressDefault = aFields[12] == u"false"_ustr ? false : true;
    rContext.TenantIsolation = aFields[13] == u"true"_ustr;
    rContext.LocalOnlyAdminPanel = aFields[14] == u"true"_ustr;
    rContext.AuditAppendOnly = aFields[15] == u"true"_ustr;
    rContext.AuditHashChainRequired = aFields[16] == u"true"_ustr;
    rContext.AuditSink = aFields[17];
    rContext.AuditSinkPort = aFields[18].toInt32();
    rContext.DocumentBinding = aFields[19];
    rContext.DocumentHashReference = aFields[20];
    return true;
}
}

AIChatTenantContextRuntime::AIChatTenantContextRuntime()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + TENANT_CONTEXT_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sContextUrl = m_sStorageRootUrl + u"/"_ustr + TENANT_CONTEXT_FILE_NAME;
}

bool AIChatTenantContextRuntime::IsContextIdAllowed(const OUString& rContextId)
{
    return rContextId.startsWith(u"tctx-"_ustr) && IsLowerHex(rContextId.copy(5), 16);
}

bool AIChatTenantContextRuntime::IsTenantIdAllowed(const OUString& rTenantId)
{
    return IsSlug(rTenantId, 3, 64);
}

bool AIChatTenantContextRuntime::IsWorkspaceIdAllowed(const OUString& rWorkspaceId)
{
    return IsSlug(rWorkspaceId, 3, 64);
}

bool AIChatTenantContextRuntime::IsUserIdAllowed(const OUString& rUserId)
{
    return IsUserSlug(rUserId);
}

bool AIChatTenantContextRuntime::IsUserRoleAllowed(const OUString& rRole)
{
    return rRole == u"user"_ustr || rRole == u"admin"_ustr || rRole == u"service"_ustr;
}

bool AIChatTenantContextRuntime::IsUserStatusAllowed(const OUString& rStatus)
{
    return rStatus == u"active"_ustr || rStatus == u"suspended"_ustr;
}

bool AIChatTenantContextRuntime::IsTenantPlanAllowed(const OUString& rPlan)
{
    return rPlan == u"personal"_ustr || rPlan == u"team"_ustr || rPlan == u"enterprise"_ustr;
}

bool AIChatTenantContextRuntime::IsDataClassAllowed(const OUString& rDataClass)
{
    return rDataClass == u"public"_ustr || rDataClass == u"internal"_ustr
           || rDataClass == u"confidential"_ustr || rDataClass == u"secret"_ustr;
}

bool AIChatTenantContextRuntime::IsServiceModeAllowed(const OUString& rServiceMode)
{
    return rServiceMode == u"offline"_ustr || rServiceMode == u"private"_ustr;
}

bool AIChatTenantContextRuntime::IsAuditSinkAllowed(const OUString& rSink)
{
    return rSink == u"local-file"_ustr || rSink == u"local-sink-server"_ustr;
}

bool AIChatTenantContextRuntime::IsDocumentBindingAllowed(const OUString& rDocumentBinding)
{
    return rDocumentBinding.startsWith(u"doc-"_ustr) && rDocumentBinding.getLength() >= 20;
}

bool AIChatTenantContextRuntime::IsDocumentHashReferenceAllowed(
    const OUString& rDocumentHashReference)
{
    return rDocumentHashReference.startsWith(u"sha256:"_ustr)
           && IsLowerHex(rDocumentHashReference.copy(7), 64);
}

bool AIChatTenantContextRuntime::IsTargetTypeAllowed(const OUString& rTargetType)
{
    return rTargetType == u"chat"_ustr || rTargetType == u"provider"_ustr
           || rTargetType == u"connector"_ustr || rTargetType == u"knowledge-index"_ustr
           || rTargetType == u"agent-step"_ustr || rTargetType == u"patch-apply"_ustr
           || rTargetType == u"audit"_ustr || rTargetType == u"companion"_ustr
           || rTargetType == u"local-cloud"_ustr || rTargetType == u"starter-pack"_ustr
           || rTargetType == u"edition-policy"_ustr || rTargetType == u"i18n-manual"_ustr
           || rTargetType == u"distribution-update"_ustr
           || rTargetType == u"error-recovery-ux"_ustr
           || rTargetType == u"perf-baseline"_ustr
           || rTargetType == u"crash-recovery"_ustr
           || rTargetType == u"release-ga-checklist"_ustr;
}

bool AIChatTenantContextRuntime::IsSurfaceAllowed(const OUString& rSurface)
{
    return rSurface == u"chat"_ustr || rSurface == u"provider"_ustr
           || rSurface == u"connector"_ustr || rSurface == u"knowledge-index"_ustr
           || rSurface == u"agent"_ustr || rSurface == u"apply-plan"_ustr
           || rSurface == u"diff-review"_ustr || rSurface == u"audit"_ustr
           || rSurface == u"companion"_ustr || rSurface == u"local-cloud"_ustr
           || rSurface == u"starter-pack"_ustr || rSurface == u"edition-policy"_ustr
           || rSurface == u"i18n-manual"_ustr || rSurface == u"distribution-update"_ustr
           || rSurface == u"error-recovery-ux"_ustr || rSurface == u"perf-baseline"_ustr
           || rSurface == u"crash-recovery"_ustr
           || rSurface == u"release-ga-checklist"_ustr || rSurface == u"admin"_ustr;
}

bool AIChatTenantContextRuntime::IsEvidenceIdAllowed(const OUString& rEvidenceId)
{
    return rEvidenceId.startsWith(u"ev-"_ustr) && IsLowerHex(rEvidenceId.copy(3), 16);
}

bool AIChatTenantContextRuntime::IsHashReferenceAllowed(const OUString& rHashReference)
{
    return IsDocumentHashReferenceAllowed(rHashReference);
}

bool AIChatTenantContextRuntime::IsContextShapeAllowed(const AIChatTenantContext& rContext)
{
    if (!IsContextIdAllowed(rContext.ContextId)
        || rContext.SchemaVersion != u"v3-tenant-context/0.1"_ustr
        || !IsTenantIdAllowed(rContext.TenantId) || rContext.TenantNameHash.isEmpty()
        || !IsTenantPlanAllowed(rContext.TenantPlan)
        || !IsWorkspaceIdAllowed(rContext.WorkspaceId) || rContext.WorkspaceNameHash.isEmpty()
        || rContext.WorkspaceDataClasses.empty()
        || ContainsDuplicateString(rContext.WorkspaceDataClasses) || rContext.Users.empty()
        || !IsServiceModeAllowed(rContext.DefaultServiceMode)
        || !ContainsString(rContext.AllowedServiceModes, rContext.DefaultServiceMode)
        || rContext.AllowedServiceModes.empty()
        || ContainsDuplicateString(rContext.AllowedServiceModes) || rContext.PublicEgressDefault
        || !rContext.TenantIsolation || !rContext.LocalOnlyAdminPanel
        || !rContext.AuditAppendOnly || !rContext.AuditHashChainRequired
        || !IsAuditSinkAllowed(rContext.AuditSink) || rContext.AuditSinkPort != 17803
        || !IsDocumentBindingAllowed(rContext.DocumentBinding)
        || !IsDocumentHashReferenceAllowed(rContext.DocumentHashReference))
        return false;

    for (const OUString& rDataClass : rContext.WorkspaceDataClasses)
    {
        if (!IsDataClassAllowed(rDataClass))
            return false;
    }
    for (const OUString& rServiceMode : rContext.AllowedServiceModes)
    {
        if (!IsServiceModeAllowed(rServiceMode))
            return false;
    }
    for (const AIChatTenantUser& rUser : rContext.Users)
    {
        if (!IsUserIdAllowed(rUser.UserId) || !IsUserRoleAllowed(rUser.Role)
            || !IsUserStatusAllowed(rUser.Status))
            return false;
    }
    return true;
}

OUString AIChatTenantContextRuntime::MakeContextId(const OUString& rTenantId,
                                                   const OUString& rWorkspaceId,
                                                   const OUString& rDocumentBinding)
{
    return u"tctx-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rTenantId + u":"_ustr + rWorkspaceId
                                                         + u":"_ustr + rDocumentBinding)
                 .copy(0, 16);
}

OUString AIChatTenantContextRuntime::MakePolicyContextRef(
    const AIChatTenantContext& rContext)
{
    return u"policy-context:"_ustr + rContext.ContextId;
}

OUString AIChatTenantContextRuntime::MakeAuditChainRef(const AIChatTenantContext& rContext)
{
    return u"audit-chain:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rContext.ContextId + u":"_ustr
                                                         + rContext.DocumentHashReference)
                 .copy(0, 16);
}

OUString AIChatTenantContextRuntime::MakeTenantEvidenceId(
    const AIChatTenantContext& rContext)
{
    return u"ev-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rContext.ContextId + u":tenant-context"_ustr)
                 .copy(0, 16);
}

OUString AIChatTenantContextRuntime::MakeTenantHashReference(
    const AIChatTenantContext& rContext)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rContext.ContextId + u":"_ustr + rContext.TenantId + u":"_ustr
                 + rContext.WorkspaceId + u":"_ustr + rContext.DocumentHashReference);
}

AIChatTenantContextResult AIChatTenantContextRuntime::SaveContext(
    const AIChatTenantContext& rContext) const
{
    AIChatTenantContextResult aResult;
    aResult.Context = rContext;
    if (!IsContextShapeAllowed(rContext))
    {
        aResult.Message
            = u"tenant-context-failed reason=invalid-context-shape"_ustr
              + u" tenantIsolation=true publicEgressDefault=false"_ustr
              + u" localOnlyAdminPanel=true auditHashChainRequired=true"_ustr
              + u" metadata-only=true raw-tenant-payload=false"_ustr;
        return aResult;
    }

    const OUString sLine
        = EscapeField(rContext.ContextId) + u"\t"_ustr + EscapeField(rContext.SchemaVersion)
          + u"\t"_ustr + EscapeField(rContext.CreatedAt) + u"\t"_ustr
          + EscapeField(rContext.TenantId) + u"\t"_ustr + EscapeField(rContext.TenantNameHash)
          + u"\t"_ustr + EscapeField(rContext.TenantPlan) + u"\t"_ustr
          + EscapeField(rContext.WorkspaceId) + u"\t"_ustr
          + EscapeField(rContext.WorkspaceNameHash) + u"\t"_ustr
          + EscapeField(JoinFields(rContext.WorkspaceDataClasses)) + u"\t"_ustr
          + EscapeField(EncodeUsers(rContext.Users)) + u"\t"_ustr
          + EscapeField(rContext.DefaultServiceMode) + u"\t"_ustr
          + EscapeField(JoinFields(rContext.AllowedServiceModes)) + u"\t"_ustr
          + EscapeField(rContext.PublicEgressDefault ? u"true"_ustr : u"false"_ustr)
          + u"\t"_ustr + EscapeField(rContext.TenantIsolation ? u"true"_ustr : u"false"_ustr)
          + u"\t"_ustr
          + EscapeField(rContext.LocalOnlyAdminPanel ? u"true"_ustr : u"false"_ustr)
          + u"\t"_ustr + EscapeField(rContext.AuditAppendOnly ? u"true"_ustr : u"false"_ustr)
          + u"\t"_ustr
          + EscapeField(rContext.AuditHashChainRequired ? u"true"_ustr : u"false"_ustr)
          + u"\t"_ustr + EscapeField(rContext.AuditSink) + u"\t"_ustr
          + EscapeField(OUString::number(rContext.AuditSinkPort)) + u"\t"_ustr
          + EscapeField(rContext.DocumentBinding) + u"\t"_ustr
          + EscapeField(rContext.DocumentHashReference) + u"\t"_ustr
          + EscapeField(MakePolicyContextRef(rContext)) + u"\t"_ustr
          + EscapeField(MakeAuditChainRef(rContext)) + u"\n"_ustr;

    if (!AppendUtf8Line(m_sContextUrl, sLine))
    {
        aResult.Message = u"tenant-context-failed reason=context-write-failed"_ustr;
        return aResult;
    }

    aResult.Success = true;
    aResult.PolicyContextRef = MakePolicyContextRef(rContext);
    aResult.AuditChainRef = MakeAuditChainRef(rContext);
    aResult.EvidenceId = MakeTenantEvidenceId(rContext);
    aResult.HashReference = MakeTenantHashReference(rContext);
    aResult.Message
        = u"tenant-context-saved context-id="_ustr + rContext.ContextId
          + u" tenant-id="_ustr + rContext.TenantId
          + u" workspace-id="_ustr + rContext.WorkspaceId
          + u" document-binding="_ustr + rContext.DocumentBinding
          + u" policy-context-ref="_ustr + aResult.PolicyContextRef
          + u" audit-chain-ref="_ustr + aResult.AuditChainRef
          + u" tenantIsolation=true localOnlyAdminPanel=true"_ustr
          + u" publicEgressDefault=false allowedServiceModes=offline,private"_ustr
          + u" auditAppendOnly=true auditHashChainRequired=true auditSinkPort=17803"_ustr
          + u" metadata-only=true raw-prompt=false raw-document=false raw-policy=false"_ustr
          + u" policy-engine-runtime=not-started audit-sink-runtime=not-started"_ustr;
    return aResult;
}

AIChatTenantContextResult AIChatTenantContextRuntime::LoadContext(
    const OUString& rContextId) const
{
    AIChatTenantContextResult aResult;
    if (!IsContextIdAllowed(rContextId))
    {
        aResult.Message = u"tenant-context-failed reason=invalid-context-id"_ustr;
        return aResult;
    }

    OString sContent;
    if (!ReadUtf8File(m_sContextUrl, sContent))
    {
        aResult.Message = u"tenant-context-failed reason=context-store-unreadable"_ustr;
        return aResult;
    }

    const OUString sText = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    sal_Int32 nLineIndex = 0;
    while (nLineIndex >= 0)
    {
        const OUString sLine = sText.getToken(0, '\n', nLineIndex);
        AIChatTenantContext aContext;
        if (ParseContextLine(sLine, aContext) && aContext.ContextId == rContextId
            && IsContextShapeAllowed(aContext))
        {
            aResult.Success = true;
            aResult.Context = aContext;
            aResult.PolicyContextRef = MakePolicyContextRef(aContext);
            aResult.AuditChainRef = MakeAuditChainRef(aContext);
            aResult.EvidenceId = MakeTenantEvidenceId(aContext);
            aResult.HashReference = MakeTenantHashReference(aContext);
            aResult.Message = u"tenant-context-loaded context-id="_ustr + aContext.ContextId
                              + u" metadata-only=true document-bound=true"_ustr;
        }
    }

    if (!aResult.Success)
        aResult.Message = u"tenant-context-failed reason=context-not-found-or-invalid"_ustr;
    return aResult;
}

AIChatTenantContextResult AIChatTenantContextRuntime::ValidateActionScope(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope) const
{
    AIChatTenantContextResult aResult;
    aResult.Context = rContext;
    if (!IsContextShapeAllowed(rContext))
    {
        aResult.Message = u"tenant-scope-denied reason=invalid-context-shape"_ustr;
        return aResult;
    }
    if (rScope.TenantId != rContext.TenantId || rScope.WorkspaceId != rContext.WorkspaceId)
    {
        aResult.Message
            = u"tenant-scope-denied reason=tenant-or-workspace-mismatch"_ustr
              + u" tenantIsolation=true fail-closed-user-visible=true"_ustr;
        return aResult;
    }
    if (rScope.DocumentBinding != rContext.DocumentBinding
        || rScope.DocumentHashReference != rContext.DocumentHashReference)
    {
        aResult.Message
            = u"tenant-scope-denied reason=document-binding-mismatch"_ustr
              + u" cross-document-restore=false fail-closed-user-visible=true"_ustr;
        return aResult;
    }
    if (!HasActiveUser(rContext, rScope.UserId, rScope.UserRole))
    {
        aResult.Message
            = u"tenant-scope-denied reason=user-not-active-or-role-mismatch"_ustr
              + u" localOnlyAdminPanel=true fail-closed-user-visible=true"_ustr;
        return aResult;
    }
    if (!IsTargetTypeAllowed(rScope.TargetType) || !IsSurfaceAllowed(rScope.Surface))
    {
        aResult.Message = u"tenant-scope-denied reason=unsupported-target-or-surface"_ustr;
        return aResult;
    }
    if (!ContainsString(rContext.WorkspaceDataClasses, rScope.DataClass)
        || !IsDataClassAllowed(rScope.DataClass))
    {
        aResult.Message
            = u"tenant-scope-denied reason=data-class-outside-workspace"_ustr
              + u" tenantIsolation=true fail-closed-user-visible=true"_ustr;
        return aResult;
    }
    if (!ContainsString(rContext.AllowedServiceModes, rScope.ServiceMode)
        || !IsServiceModeAllowed(rScope.ServiceMode))
    {
        aResult.Message
            = u"tenant-scope-denied reason=service-mode-outside-tenant-boundary"_ustr
              + u" publicEgressDefault=false no-public-egress=true"_ustr;
        return aResult;
    }
    if (!IsEvidenceIdAllowed(rScope.EvidenceId) || !IsHashReferenceAllowed(rScope.HashReference))
    {
        aResult.Message
            = u"tenant-scope-denied reason=missing-evidence-or-hash"_ustr
              + u" auditLogRequired=true evidenceRecordRequired=true"_ustr;
        return aResult;
    }

    aResult.Success = true;
    aResult.PolicyContextRef = MakePolicyContextRef(rContext);
    aResult.AuditChainRef = MakeAuditChainRef(rContext);
    aResult.EvidenceId = rScope.EvidenceId;
    aResult.HashReference = rScope.HashReference;
    aResult.Message
        = u"tenant-scope-allowed context-id="_ustr + rContext.ContextId
          + u" target-type="_ustr + rScope.TargetType + u" surface="_ustr + rScope.Surface
          + u" tenant-id="_ustr + rContext.TenantId
          + u" workspace-id="_ustr + rContext.WorkspaceId
          + u" user-id="_ustr + rScope.UserId + u" user-role="_ustr + rScope.UserRole
          + u" data-class="_ustr + rScope.DataClass + u" service-mode="_ustr
          + rScope.ServiceMode + u" policy-context-ref="_ustr + aResult.PolicyContextRef
          + u" audit-chain-ref="_ustr + aResult.AuditChainRef
          + u" policy-preflight=true post-evidence-required=true"_ustr
          + u" auditLogRequired=true evidenceRecordRequired=true"_ustr
          + u" tenantIsolation=true publicEgressDefault=false no-public-egress=true"_ustr
          + u" metadata-only=true raw-prompt=false raw-document=false raw-connector=false"_ustr
          + u" policy-engine-runtime=not-started audit-log-runtime=not-started"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
