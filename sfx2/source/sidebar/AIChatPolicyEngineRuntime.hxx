/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W4/M6: policy engine runtime).
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

struct AIChatPolicyRule
{
    OUString RuleId;
    OUString SchemaVersion;
    sal_Int32 Priority = 0;
    OUString Phase;
    OUString Effect;
    std::vector<OUString> Tenants;
    std::vector<OUString> ServiceModes;
    std::vector<OUString> ActorRoles;
    std::vector<OUString> TargetTypes;
    std::vector<OUString> DataClasses;
    bool BlocksAction = false;
    bool ApprovalRequired = false;
    bool AuditLogRequired = true;
    OUString ReasonCode;
    std::vector<OUString> RequiredEvidence;
    bool EmitsAuditLog = true;
    bool EvidenceRecordRequired = true;
};

struct AIChatPolicyDecision
{
    bool Success = false;
    OUString RuleId;
    OUString Effect;
    OUString Phase;
    OUString Decision;
    bool BlocksAction = false;
    bool ApprovalRequired = false;
    bool AuditLogRequired = true;
    OUString ReasonCode;
    OUString PolicyContextRef;
    OUString AuditChainRef;
    OUString EvidenceId;
    OUString HashReference;
    OUString Message;
};

class AIChatPolicyEngineRuntime final
{
public:
    AIChatPolicyDecision EvaluateRule(const AIChatTenantContext& rContext,
                                      const AIChatTenantActionScope& rScope,
                                      const AIChatPolicyRule& rRule) const;

    static bool IsRuleIdAllowed(const OUString& rRuleId);
    static bool IsPhaseAllowed(const OUString& rPhase);
    static bool IsEffectAllowed(const OUString& rEffect);
    static bool IsPolicyTargetTypeAllowed(const OUString& rTargetType);
    static bool IsReasonCodeAllowed(const OUString& rReasonCode);
    static bool IsEvidenceRequirementAllowed(const OUString& rEvidence);
    static bool IsRuleShapeAllowed(const AIChatPolicyRule& rRule);
    static bool RuleMatchesScope(const AIChatPolicyRule& rRule,
                                 const AIChatTenantActionScope& rScope);
    static OUString NormalizeTargetType(const OUString& rTargetType);
    static OUString MakePolicyDecisionEvidenceId(const AIChatPolicyRule& rRule,
                                                 const AIChatTenantActionScope& rScope);
    static OUString MakePolicyDecisionHashReference(const AIChatPolicyRule& rRule,
                                                    const AIChatTenantActionScope& rScope);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
