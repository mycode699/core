/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W4/M6: tenant context runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatTenantUser
{
    OUString UserId;
    OUString Role;
    OUString Status;
};

struct AIChatTenantContext
{
    OUString ContextId;
    OUString SchemaVersion;
    OUString CreatedAt;
    OUString TenantId;
    OUString TenantNameHash;
    OUString TenantPlan;
    OUString WorkspaceId;
    OUString WorkspaceNameHash;
    std::vector<OUString> WorkspaceDataClasses;
    std::vector<AIChatTenantUser> Users;
    OUString DefaultServiceMode;
    std::vector<OUString> AllowedServiceModes;
    bool PublicEgressDefault = false;
    bool TenantIsolation = true;
    bool LocalOnlyAdminPanel = true;
    bool AuditAppendOnly = true;
    bool AuditHashChainRequired = true;
    OUString AuditSink;
    sal_Int32 AuditSinkPort = 17803;
    OUString DocumentBinding;
    OUString DocumentHashReference;
};

struct AIChatTenantActionScope
{
    OUString TenantId;
    OUString WorkspaceId;
    OUString UserId;
    OUString UserRole;
    OUString DocumentBinding;
    OUString DocumentHashReference;
    OUString TargetType;
    OUString DataClass;
    OUString ServiceMode;
    OUString Surface;
    OUString EvidenceId;
    OUString HashReference;
};

struct AIChatTenantContextResult
{
    bool Success = false;
    AIChatTenantContext Context;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString EvidenceId;
    OUString HashReference;
    OUString Message;
};

class AIChatTenantContextRuntime final
{
public:
    AIChatTenantContextRuntime();

    AIChatTenantContextResult SaveContext(const AIChatTenantContext& rContext) const;
    AIChatTenantContextResult LoadContext(const OUString& rContextId) const;
    AIChatTenantContextResult ValidateActionScope(const AIChatTenantContext& rContext,
                                                  const AIChatTenantActionScope& rScope) const;

    static bool IsContextIdAllowed(const OUString& rContextId);
    static bool IsTenantIdAllowed(const OUString& rTenantId);
    static bool IsWorkspaceIdAllowed(const OUString& rWorkspaceId);
    static bool IsUserIdAllowed(const OUString& rUserId);
    static bool IsUserRoleAllowed(const OUString& rRole);
    static bool IsUserStatusAllowed(const OUString& rStatus);
    static bool IsTenantPlanAllowed(const OUString& rPlan);
    static bool IsDataClassAllowed(const OUString& rDataClass);
    static bool IsServiceModeAllowed(const OUString& rServiceMode);
    static bool IsAuditSinkAllowed(const OUString& rSink);
    static bool IsDocumentBindingAllowed(const OUString& rDocumentBinding);
    static bool IsDocumentHashReferenceAllowed(const OUString& rDocumentHashReference);
    static bool IsTargetTypeAllowed(const OUString& rTargetType);
    static bool IsSurfaceAllowed(const OUString& rSurface);
    static bool IsEvidenceIdAllowed(const OUString& rEvidenceId);
    static bool IsHashReferenceAllowed(const OUString& rHashReference);
    static bool IsContextShapeAllowed(const AIChatTenantContext& rContext);
    static OUString MakeContextId(const OUString& rTenantId, const OUString& rWorkspaceId,
                                  const OUString& rDocumentBinding);
    static OUString MakePolicyContextRef(const AIChatTenantContext& rContext);
    static OUString MakeAuditChainRef(const AIChatTenantContext& rContext);
    static OUString MakeTenantEvidenceId(const AIChatTenantContext& rContext);
    static OUString MakeTenantHashReference(const AIChatTenantContext& rContext);

private:
    OUString m_sStorageRootUrl;
    OUString m_sContextUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
