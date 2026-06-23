/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W2/M4: connector auth flow guards).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatConnectorAuthFlowRuntime.hxx"

namespace sfx2::sidebar
{

bool AIChatConnectorAuthFlowRuntime::IsAuthFlowRuntimeAllowed(
    const AIChatConnectorManifest& rManifest)
{
    return AIChatConnectorManifestLoader::IsAuthFlowAllowed(rManifest)
           && !rManifest.EmbeddedWebView
           && rManifest.RuntimeAuthImplementation == u"not-started"_ustr;
}

bool AIChatConnectorAuthFlowRuntime::IsRefreshRuntimeAllowed(
    const AIChatConnectorManifest& rManifest)
{
    return AIChatConnectorManifestLoader::IsRefreshPolicyAllowed(rManifest)
           && !rManifest.BackgroundRefresh && !rManifest.StoresRefreshToken
           && rManifest.RuntimeRefreshImplementation == u"not-started"_ustr;
}

OUString AIChatConnectorAuthFlowRuntime::ResolveRequiredUserAction(
    const AIChatConnectorManifest& rManifest)
{
    if (rManifest.AuthType == u"oauth2"_ustr)
        return u"open-system-browser-loopback"_ustr;
    if (rManifest.AuthType == u"api-key"_ustr)
        return u"manual-secret-entry"_ustr;
    return u"not-applicable"_ustr;
}

OUString AIChatConnectorAuthFlowRuntime::ResolveAuthSurface(
    const AIChatConnectorManifest& rManifest)
{
    if (rManifest.AuthType == u"oauth2"_ustr)
        return u"system-browser"_ustr;
    if (rManifest.AuthType == u"api-key"_ustr)
        return u"native-secret-entry"_ustr;
    return u"none"_ustr;
}

OUString AIChatConnectorAuthFlowRuntime::ResolveTokenStoragePosture(
    const AIChatConnectorManifest& rManifest)
{
    if (rManifest.AuthType == u"none"_ustr)
        return u"token-storage=none secret-stored=false credential-redacted=true"_ustr;
    return u"token-storage="_ustr + rManifest.TokenStorage
           + u" secret-stored=false credential-redacted=true secret-material-present=false"_ustr;
}

OUString AIChatConnectorAuthFlowRuntime::ResolveRefreshPosture(
    const AIChatConnectorManifest& rManifest)
{
    return u"refresh-strategy="_ustr + rManifest.RefreshStrategy
           + u" background-refresh=false stores-refresh-token=false refresh-runtime=not-started"_ustr;
}

AIChatConnectorAuthFlowResult AIChatConnectorAuthFlowRuntime::PrepareAuthFlow(
    const AIChatConnectorManifest& rManifest, const AIChatConnectorAuthFlowRequest& rRequest) const
{
    AIChatConnectorAuthFlowResult aResult;
    aResult.ConnectorId = rManifest.Id;
    aResult.AuthType = rManifest.AuthType;

    if (rRequest.ConnectorId != rManifest.Id)
    {
        aResult.Message = u"connector-auth-denied reason=connector-id-mismatch"_ustr;
        return aResult;
    }
    if (!IsAuthFlowRuntimeAllowed(rManifest))
    {
        aResult.Message
            = u"connector-auth-denied reason=auth-flow-disallowed embedded-webview=false auth-runtime=not-started credential-redacted=true"_ustr;
        return aResult;
    }
    if (!IsRefreshRuntimeAllowed(rManifest))
    {
        aResult.Message
            = u"connector-auth-denied reason=refresh-policy-disallowed background-refresh=false stores-refresh-token=false refresh-runtime=not-started"_ustr;
        return aResult;
    }
    if (rManifest.RequiresTenantPolicy && rRequest.TenantPolicyRef.isEmpty())
    {
        aResult.Message = u"connector-auth-denied reason=tenant-policy-required"_ustr;
        return aResult;
    }

    aResult.RequiredUserAction = ResolveRequiredUserAction(rManifest);
    aResult.AuthSurface = ResolveAuthSurface(rManifest);
    aResult.CallbackBinding = rManifest.AuthCallback;
    aResult.TokenStoragePosture = ResolveTokenStoragePosture(rManifest);
    aResult.RefreshPosture = ResolveRefreshPosture(rManifest);
    aResult.EvidenceCategory = u"auth"_ustr;

    if (rManifest.AuthType != u"none"_ustr)
    {
        if (!rRequest.UserApproved)
        {
            aResult.Message = u"connector-auth-pending reason=user-approval-required action="_ustr
                              + aResult.RequiredUserAction
                              + u" credential-redacted=true secret-material-present=false"_ustr;
            return aResult;
        }
        if (rRequest.UserAction != aResult.RequiredUserAction)
        {
            aResult.Message = u"connector-auth-denied reason=unexpected-user-action expected="_ustr
                              + aResult.RequiredUserAction + u" actual="_ustr
                              + rRequest.UserAction;
            return aResult;
        }
    }

    aResult.Success = true;
    aResult.Message = u"connector-auth-ready connector-id="_ustr + rManifest.Id
                      + u" auth-type="_ustr + rManifest.AuthType + u" action="_ustr
                      + aResult.RequiredUserAction + u" surface="_ustr + aResult.AuthSurface
                      + u" callback="_ustr + aResult.CallbackBinding
                      + u" embedded-webview=false auth-runtime=not-started"_ustr
                      + u" "_ustr + aResult.TokenStoragePosture + u" "_ustr
                      + aResult.RefreshPosture
                      + u" evidence-category=auth metadata-only=true raw-secret=false raw-token=false"_ustr
                      + u" system-browser-started=false loopback-listener-started=false"_ustr
                      + u" background-refresh=false stores-refresh-token=false network-started=false"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
