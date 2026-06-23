/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W2/M4: connector read-only operations).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatConnectorManifestLoader.hxx"
#include "AIChatContentRegistry.hxx"

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatConnectorOperationRequest
{
    OUString ConnectorId;
    OUString Action;
    OUString QueryRef;
    OUString ScopeRef;
    OUString TenantPolicyRef;
    OUString CallerSurface;
    bool UserApproved = false;
};

struct AIChatConnectorOperationResult
{
    bool Success = false;
    AIChatContentRegistryEntry RegistryEntry;
    OUString ConnectorId;
    OUString OperationId;
    OUString EvidenceId;
    OUString CitationId;
    OUString HashReference;
    OUString Message;
};

class AIChatConnectorOperationRuntime final
{
public:
    AIChatConnectorOperationResult
    ExecuteReadOnly(const AIChatConnectorManifest& rManifest,
                    const AIChatConnectorOperationRequest& rRequest) const;

    static bool IsReadActionAllowed(const AIChatConnectorManifest& rManifest,
                                    const AIChatConnectorOperationRequest& rRequest);
    static bool IsWriteAction(const OUString& rAction);
    static OUString MakeOperationId(const AIChatConnectorManifest& rManifest,
                                    const AIChatConnectorOperationRequest& rRequest);
    static OUString MakeConnectorEvidenceId(const OUString& rOperationId);
    static OUString MakeConnectorHashReference(const AIChatConnectorManifest& rManifest,
                                               const AIChatConnectorOperationRequest& rRequest);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
