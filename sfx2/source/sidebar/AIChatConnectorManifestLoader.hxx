/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W2/M4: connector manifest loader).
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

struct AIChatConnectorScope
{
    OUString Name;
    OUString DataClass;
};

struct AIChatConnectorManifest
{
    OUString Id;
    OUString Version;
    OUString DisplayName;
    OUString TrustSource;
    OUString Publisher;
    OUString ManifestSha256;
    OUString ReviewState;
    OUString InstallScope;
    bool SignatureRequired = false;
    bool AllowUnsigned = true;
    OUString OperationMode;
    std::vector<OUString> AllowedActions;
    bool Writeback = true;
    bool WriteScopesAllowed = true;
    OUString RuntimeWriteImplementation;
    OUString AuthType;
    std::vector<OUString> AuthScopes;
    OUString TokenStorage;
    OUString AuthFlowStrategy;
    bool EmbeddedWebView = true;
    OUString AuthCallback;
    OUString RuntimeAuthImplementation;
    OUString RefreshStrategy;
    bool BackgroundRefresh = true;
    bool StoresRefreshToken = true;
    OUString RuntimeRefreshImplementation;
    std::vector<OUString> ServiceModes;
    std::vector<AIChatConnectorScope> Scopes;
    bool EvidenceEmit = false;
    OUString EvidenceCategory;
    bool RequiresTenantPolicy = false;
    sal_Int32 CacheTTLSeconds = 300;
};

struct AIChatConnectorManifestLoadResult
{
    bool Success = false;
    AIChatConnectorManifest Manifest;
    OUString Message;
};

class AIChatConnectorManifestLoader final
{
public:
    AIChatConnectorManifestLoadResult LoadFromFile(const OUString& rFileUrl) const;
    AIChatConnectorManifestLoadResult LoadFromString(const OUString& rJson) const;
    AIChatConnectorManifestLoadResult Validate(const AIChatConnectorManifest& rManifest) const;

    static bool IsValidConnectorId(const OUString& rId);
    static bool IsValidPublisher(const OUString& rPublisher);
    static bool IsValidManifestSha256(const OUString& rManifestSha256);
    static bool IsValidTrustEnvelope(const AIChatConnectorManifest& rManifest);
    static bool IsReadOnlyOperationsEnvelope(const AIChatConnectorManifest& rManifest);
    static bool IsAuthFlowAllowed(const AIChatConnectorManifest& rManifest);
    static bool IsRefreshPolicyAllowed(const AIChatConnectorManifest& rManifest);
    static bool RequiresTenantPolicyForScopes(const AIChatConnectorManifest& rManifest);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
