/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W2/M4: connector auth flow guards).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatConnectorManifestLoader.hxx"

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatConnectorAuthFlowRequest
{
    OUString ConnectorId;
    OUString UserAction;
    OUString TenantPolicyRef;
    bool UserApproved = false;
};

struct AIChatConnectorAuthFlowResult
{
    bool Success = false;
    OUString ConnectorId;
    OUString AuthType;
    OUString RequiredUserAction;
    OUString AuthSurface;
    OUString CallbackBinding;
    OUString TokenStoragePosture;
    OUString RefreshPosture;
    OUString EvidenceCategory;
    OUString Message;
};

class AIChatConnectorAuthFlowRuntime final
{
public:
    AIChatConnectorAuthFlowResult
    PrepareAuthFlow(const AIChatConnectorManifest& rManifest,
                    const AIChatConnectorAuthFlowRequest& rRequest) const;

    static bool IsAuthFlowRuntimeAllowed(const AIChatConnectorManifest& rManifest);
    static bool IsRefreshRuntimeAllowed(const AIChatConnectorManifest& rManifest);
    static OUString ResolveRequiredUserAction(const AIChatConnectorManifest& rManifest);
    static OUString ResolveAuthSurface(const AIChatConnectorManifest& rManifest);
    static OUString ResolveTokenStoragePosture(const AIChatConnectorManifest& rManifest);
    static OUString ResolveRefreshPosture(const AIChatConnectorManifest& rManifest);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
