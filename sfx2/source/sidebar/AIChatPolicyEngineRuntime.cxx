/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W4/M6: policy engine runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatPolicyEngineRuntime.hxx"

#include "AIChatKnowledgeIndexStore.hxx"

#include <algorithm>

namespace sfx2::sidebar
{
namespace
{
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

bool IsAnyRoleOrMatches(const std::vector<OUString>& rRoles, const OUString& rRole)
{
    return ContainsString(rRoles, u"any"_ustr) || ContainsString(rRoles, rRole);
}

bool IsSlugTail(const OUString& rValue, sal_Int32 nStart)
{
    for (sal_Int32 i = nStart; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        if (!((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-'))
            return false;
    }
    return true;
}
}

bool AIChatPolicyEngineRuntime::IsRuleIdAllowed(const OUString& rRuleId)
{
    return rRuleId.startsWith(u"pol-"_ustr) && rRuleId.getLength() >= 7
           && rRuleId.getLength() <= 84 && IsSlugTail(rRuleId, 4);
}

bool AIChatPolicyEngineRuntime::IsPhaseAllowed(const OUString& rPhase)
{
    return rPhase == u"pre-flight"_ustr || rPhase == u"post-evidence"_ustr;
}

bool AIChatPolicyEngineRuntime::IsEffectAllowed(const OUString& rEffect)
{
    return rEffect == u"allow"_ustr || rEffect == u"deny"_ustr
           || rEffect == u"require-approval"_ustr || rEffect == u"require-evidence"_ustr;
}

bool AIChatPolicyEngineRuntime::IsPolicyTargetTypeAllowed(const OUString& rTargetType)
{
    return rTargetType == u"chat"_ustr || rTargetType == u"provider"_ustr
           || rTargetType == u"connector"_ustr || rTargetType == u"kb-query"_ustr
           || rTargetType == u"agent-step"_ustr || rTargetType == u"patch-apply"_ustr
           || rTargetType == u"companion"_ustr || rTargetType == u"local-cloud"_ustr
           || rTargetType == u"starter-pack"_ustr || rTargetType == u"edition-policy"_ustr
           || rTargetType == u"i18n-manual"_ustr
           || rTargetType == u"distribution-update"_ustr
           || rTargetType == u"error-recovery-ux"_ustr
           || rTargetType == u"perf-baseline"_ustr
           || rTargetType == u"crash-recovery"_ustr
           || rTargetType == u"release-ga-checklist"_ustr;
}

bool AIChatPolicyEngineRuntime::IsReasonCodeAllowed(const OUString& rReasonCode)
{
    if (rReasonCode.getLength() < 4 || rReasonCode.getLength() > 80)
        return false;
    const sal_Unicode cFirst = rReasonCode[0];
    return cFirst >= u'a' && cFirst <= u'z' && IsSlugTail(rReasonCode, 1);
}

bool AIChatPolicyEngineRuntime::IsEvidenceRequirementAllowed(const OUString& rEvidence)
{
    return rEvidence == u"policy-decision"_ustr || rEvidence == u"audit-log-entry"_ustr
           || rEvidence == u"evidence-record"_ustr || rEvidence == u"user-approval"_ustr;
}

bool AIChatPolicyEngineRuntime::IsRuleShapeAllowed(const AIChatPolicyRule& rRule)
{
    if (!IsRuleIdAllowed(rRule.RuleId) || rRule.SchemaVersion != u"v3-policy-rule/0.1"_ustr
        || rRule.Priority < 1 || rRule.Priority > 1000 || !IsPhaseAllowed(rRule.Phase)
        || !IsEffectAllowed(rRule.Effect) || rRule.Tenants.empty()
        || rRule.ServiceModes.empty() || rRule.ActorRoles.empty() || rRule.TargetTypes.empty()
        || rRule.DataClasses.empty() || rRule.RequiredEvidence.empty()
        || ContainsDuplicateString(rRule.Tenants)
        || ContainsDuplicateString(rRule.ServiceModes)
        || ContainsDuplicateString(rRule.ActorRoles)
        || ContainsDuplicateString(rRule.TargetTypes)
        || ContainsDuplicateString(rRule.DataClasses)
        || ContainsDuplicateString(rRule.RequiredEvidence) || !rRule.AuditLogRequired
        || !rRule.EmitsAuditLog || !rRule.EvidenceRecordRequired
        || !IsReasonCodeAllowed(rRule.ReasonCode)
        || !ContainsString(rRule.RequiredEvidence, u"policy-decision"_ustr)
        || !ContainsString(rRule.RequiredEvidence, u"audit-log-entry"_ustr)
        || !ContainsString(rRule.RequiredEvidence, u"evidence-record"_ustr))
        return false;

    for (const OUString& rTenant : rRule.Tenants)
    {
        if (!AIChatTenantContextRuntime::IsTenantIdAllowed(rTenant))
            return false;
    }
    for (const OUString& rServiceMode : rRule.ServiceModes)
    {
        if (!AIChatTenantContextRuntime::IsServiceModeAllowed(rServiceMode))
            return false;
    }
    for (const OUString& rRole : rRule.ActorRoles)
    {
        if (rRole != u"any"_ustr && !AIChatTenantContextRuntime::IsUserRoleAllowed(rRole))
            return false;
    }
    for (const OUString& rTargetType : rRule.TargetTypes)
    {
        if (!IsPolicyTargetTypeAllowed(rTargetType))
            return false;
    }
    for (const OUString& rDataClass : rRule.DataClasses)
    {
        if (!AIChatTenantContextRuntime::IsDataClassAllowed(rDataClass))
            return false;
    }
    for (const OUString& rEvidence : rRule.RequiredEvidence)
    {
        if (!IsEvidenceRequirementAllowed(rEvidence))
            return false;
    }

    if (rRule.Effect == u"deny"_ustr)
        return rRule.BlocksAction && !rRule.ApprovalRequired;
    if (rRule.Effect == u"allow"_ustr)
        return !rRule.BlocksAction && !rRule.ApprovalRequired;
    if (rRule.Effect == u"require-approval"_ustr)
        return rRule.BlocksAction && rRule.ApprovalRequired
               && ContainsString(rRule.RequiredEvidence, u"user-approval"_ustr);
    if (rRule.Effect == u"require-evidence"_ustr)
        return rRule.Phase == u"post-evidence"_ustr && !rRule.BlocksAction
               && !rRule.ApprovalRequired;
    return false;
}

OUString AIChatPolicyEngineRuntime::NormalizeTargetType(const OUString& rTargetType)
{
    if (rTargetType == u"knowledge-index"_ustr)
        return u"kb-query"_ustr;
    if (rTargetType == u"audit"_ustr)
        return u"chat"_ustr;
    return rTargetType;
}

bool AIChatPolicyEngineRuntime::RuleMatchesScope(const AIChatPolicyRule& rRule,
                                                 const AIChatTenantActionScope& rScope)
{
    return ContainsString(rRule.Tenants, rScope.TenantId)
           && ContainsString(rRule.ServiceModes, rScope.ServiceMode)
           && IsAnyRoleOrMatches(rRule.ActorRoles, rScope.UserRole)
           && ContainsString(rRule.TargetTypes, NormalizeTargetType(rScope.TargetType))
           && ContainsString(rRule.DataClasses, rScope.DataClass);
}

OUString AIChatPolicyEngineRuntime::MakePolicyDecisionEvidenceId(
    const AIChatPolicyRule& rRule, const AIChatTenantActionScope& rScope)
{
    return u"ev-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rRule.RuleId + u":"_ustr
                                                         + rScope.TenantId + u":"_ustr
                                                         + rScope.WorkspaceId + u":"_ustr
                                                         + rScope.TargetType)
                 .copy(0, 16);
}

OUString AIChatPolicyEngineRuntime::MakePolicyDecisionHashReference(
    const AIChatPolicyRule& rRule, const AIChatTenantActionScope& rScope)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rRule.RuleId + u":"_ustr + rRule.Effect + u":"_ustr + rRule.Phase
                 + u":"_ustr + rScope.HashReference);
}

AIChatPolicyDecision AIChatPolicyEngineRuntime::EvaluateRule(
    const AIChatTenantContext& rContext, const AIChatTenantActionScope& rScope,
    const AIChatPolicyRule& rRule) const
{
    AIChatPolicyDecision aDecision;
    aDecision.RuleId = rRule.RuleId;
    aDecision.Effect = rRule.Effect;
    aDecision.Phase = rRule.Phase;
    aDecision.AuditLogRequired = true;

    AIChatTenantContextRuntime aTenantRuntime;
    const AIChatTenantContextResult aScopeResult
        = aTenantRuntime.ValidateActionScope(rContext, rScope);
    if (!aScopeResult.Success)
    {
        aDecision.Decision = u"deny"_ustr;
        aDecision.BlocksAction = true;
        aDecision.ReasonCode = u"tenant-scope-invalid"_ustr;
        aDecision.Message
            = u"policy-decision-denied reason=tenant-scope-invalid"_ustr
              + u" fail-closed-user-visible=true policy-preflight=true"_ustr
              + u" publicEgressDefault=false no-public-egress=true"_ustr
              + u" metadata-only=true main-document-mutation=false"_ustr;
        return aDecision;
    }

    if (!IsRuleShapeAllowed(rRule))
    {
        aDecision.Decision = u"deny"_ustr;
        aDecision.BlocksAction = true;
        aDecision.ReasonCode = u"invalid-policy-rule"_ustr;
        aDecision.PolicyContextRef = aScopeResult.PolicyContextRef;
        aDecision.AuditChainRef = aScopeResult.AuditChainRef;
        aDecision.Message
            = u"policy-decision-denied reason=invalid-policy-rule"_ustr
              + u" fail-closed-user-visible=true auditLogRequired=true"_ustr
              + u" metadata-only=true raw-policy=false"_ustr;
        return aDecision;
    }

    if (!RuleMatchesScope(rRule, rScope))
    {
        aDecision.Decision = u"allow"_ustr;
        aDecision.PolicyContextRef = aScopeResult.PolicyContextRef;
        aDecision.AuditChainRef = aScopeResult.AuditChainRef;
        aDecision.EvidenceId = MakePolicyDecisionEvidenceId(rRule, rScope);
        aDecision.HashReference = MakePolicyDecisionHashReference(rRule, rScope);
        aDecision.Message
            = u"policy-decision-not-applicable rule-id="_ustr + rRule.RuleId
              + u" decision=allow reason=rule-scope-mismatch"_ustr
              + u" auditLogRequired=true evidenceRecordRequired=true"_ustr
              + u" metadata-only=true no-public-egress=true"_ustr;
        aDecision.Success = true;
        return aDecision;
    }

    aDecision.Success = true;
    aDecision.PolicyContextRef = aScopeResult.PolicyContextRef;
    aDecision.AuditChainRef = aScopeResult.AuditChainRef;
    aDecision.EvidenceId = MakePolicyDecisionEvidenceId(rRule, rScope);
    aDecision.HashReference = MakePolicyDecisionHashReference(rRule, rScope);
    aDecision.BlocksAction = rRule.BlocksAction;
    aDecision.ApprovalRequired = rRule.ApprovalRequired;
    aDecision.ReasonCode = rRule.ReasonCode;
    aDecision.Decision = rRule.Effect;

    if (!aDecision.EvidenceId.startsWith(u"ev-"_ustr)
        || !IsLowerHex(aDecision.EvidenceId.copy(3), 16)
        || !aDecision.HashReference.startsWith(u"sha256:"_ustr))
    {
        aDecision.Success = false;
        aDecision.Decision = u"deny"_ustr;
        aDecision.BlocksAction = true;
        aDecision.Message
            = u"policy-decision-denied reason=decision-evidence-invalid"_ustr
              + u" fail-closed-user-visible=true"_ustr;
        return aDecision;
    }

    aDecision.Message = u"policy-decision-evaluated rule-id="_ustr + rRule.RuleId
                        + u" phase="_ustr + rRule.Phase + u" effect="_ustr
                        + rRule.Effect + u" decision="_ustr + aDecision.Decision
                        + u" blocksAction="_ustr
                        + (aDecision.BlocksAction ? u"true"_ustr : u"false"_ustr)
                        + u" approvalRequired="_ustr
                        + (aDecision.ApprovalRequired ? u"true"_ustr : u"false"_ustr)
                        + u" reason-code="_ustr + aDecision.ReasonCode
                        + u" policy-context-ref="_ustr + aDecision.PolicyContextRef
                        + u" audit-chain-ref="_ustr + aDecision.AuditChainRef
                        + u" evidence-id="_ustr + aDecision.EvidenceId
                        + u" hash-reference="_ustr + aDecision.HashReference
                        + u" policy-decision=true auditLogRequired=true"_ustr
                        + u" evidenceRecordRequired=true emitsAuditLog=true"_ustr
                        + u" no-public-egress=true publicEgressDefault=false"_ustr
                        + u" metadata-only=true raw-policy=false raw-prompt=false raw-document=false"_ustr
                        + u" yaml-parser-runtime=not-started audit-log-runtime=not-started"_ustr;
    return aDecision;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
