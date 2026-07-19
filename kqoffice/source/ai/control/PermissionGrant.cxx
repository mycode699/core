/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (Wave D4: clarification + session permission UX).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "PermissionGrant.hxx"

#include <osl/mutex.hxx>

#include <set>

namespace kqoffice::ai::control
{

namespace
{

osl::Mutex& grantMutex()
{
    static osl::Mutex s_mutex;
    return s_mutex;
}

std::set<OUString>& sessionAllows()
{
    // process-lifetime store; cleared only via clearAllSessionAllows / revokeSession
    static std::set<OUString> s_allows;
    return s_allows;
}

} // namespace

bool PermissionGrant::isSessionAllowed(const OUString& actionId)
{
    if (actionId.isEmpty())
        return false;
    osl::MutexGuard g(grantMutex());
    return sessionAllows().find(actionId) != sessionAllows().end();
}

void PermissionGrant::grantSession(const OUString& actionId)
{
    if (actionId.isEmpty())
        return;
    osl::MutexGuard g(grantMutex());
    sessionAllows().insert(actionId);
}

void PermissionGrant::revokeSession(const OUString& actionId)
{
    if (actionId.isEmpty())
        return;
    osl::MutexGuard g(grantMutex());
    sessionAllows().erase(actionId);
}

void PermissionGrant::clearAllSessionAllows()
{
    osl::MutexGuard g(grantMutex());
    sessionAllows().clear();
}

std::vector<OUString> PermissionGrant::sessionAllowedActions()
{
    osl::MutexGuard g(grantMutex());
    return std::vector<OUString>(sessionAllows().begin(), sessionAllows().end());
}

void PermissionGrant::applyDecision(const OUString& actionId, PermissionDecision decision)
{
    if (decision == PermissionDecision::AllowSession)
        grantSession(actionId);
    // AllowOnce / Deny: no session store change
}

std::optional<PermissionDecision> PermissionGrant::tryAutoAllow(const OUString& actionId)
{
    if (isSessionAllowed(actionId))
        return PermissionDecision::AllowSession;
    return std::nullopt;
}

OUString PermissionGrant::decisionLabelZh(PermissionDecision decision)
{
    switch (decision)
    {
        case PermissionDecision::Deny:
            return u"拒绝"_ustr;
        case PermissionDecision::AllowOnce:
            return u"仅本次"_ustr;
        case PermissionDecision::AllowSession:
            return u"本轮对话均允许"_ustr;
    }
    return u"拒绝"_ustr;
}

ClarificationResult PermissionGrant::resolveHeadless(const ClarificationPrompt& prompt)
{
    ClarificationResult result;
    if (auto autoAllow = tryAutoAllow(prompt.actionId))
    {
        result.decision = *autoAllow;
        result.fromSessionCache = true;
        return result;
    }
    result.decision = PermissionDecision::Deny;
    result.fromSessionCache = false;
    return result;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
