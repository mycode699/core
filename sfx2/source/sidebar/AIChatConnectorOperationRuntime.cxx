/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W2/M4: connector read-only operations).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatConnectorOperationRuntime.hxx"

#include "AIChatSourceProvenance.hxx"

#include <comphelper/hash.hxx>
#include <rtl/string.hxx>

#include <algorithm>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
bool Contains(const std::vector<OUString>& rValues, const OUString& rValue)
{
    return std::find(rValues.begin(), rValues.end(), rValue) != rValues.end();
}

OUString HashMetadata(const OUString& rText)
{
    const OString sText = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    const std::vector<unsigned char> aHash = comphelper::Hash::calculateHash(
        sText.getStr(), sText.getLength(), comphelper::HashType::SHA256);
    return OUString::createFromAscii(comphelper::hashToString(aHash));
}
}

bool AIChatConnectorOperationRuntime::IsWriteAction(const OUString& rAction)
{
    return rAction == u"write"_ustr || rAction == u"create"_ustr || rAction == u"update"_ustr
           || rAction == u"delete"_ustr || rAction == u"patch"_ustr
           || rAction == u"writeback"_ustr || rAction.indexOf(u":write"_ustr) >= 0
           || rAction.startsWith(u"write:"_ustr);
}

bool AIChatConnectorOperationRuntime::IsReadActionAllowed(
    const AIChatConnectorManifest& rManifest, const AIChatConnectorOperationRequest& rRequest)
{
    return rRequest.Action == u"read"_ustr
           && AIChatConnectorManifestLoader::IsReadOnlyOperationsEnvelope(rManifest)
           && Contains(rManifest.AllowedActions, u"read"_ustr) && !rManifest.Writeback
           && !rManifest.WriteScopesAllowed
           && rManifest.RuntimeWriteImplementation == u"not-started"_ustr
           && rManifest.EvidenceEmit && rManifest.EvidenceCategory == u"data-fetch"_ustr;
}

OUString AIChatConnectorOperationRuntime::MakeOperationId(
    const AIChatConnectorManifest& rManifest, const AIChatConnectorOperationRequest& rRequest)
{
    const OUString sSeed = rManifest.Id + u"|"_ustr + rRequest.Action + u"|"_ustr
                           + rRequest.QueryRef + u"|"_ustr + rRequest.ScopeRef;
    return u"connector-op:"_ustr + rManifest.Id + u":"_ustr + HashMetadata(sSeed).copy(0, 16);
}

OUString AIChatConnectorOperationRuntime::MakeConnectorEvidenceId(const OUString& rOperationId)
{
    return u"evidence:connector-fetch:"_ustr + rOperationId;
}

OUString AIChatConnectorOperationRuntime::MakeConnectorHashReference(
    const AIChatConnectorManifest& rManifest, const AIChatConnectorOperationRequest& rRequest)
{
    const OUString sSeed = rManifest.ManifestSha256 + u"|"_ustr + rManifest.Id + u"|"_ustr
                           + rRequest.Action + u"|"_ustr + rRequest.QueryRef + u"|"_ustr
                           + rRequest.ScopeRef;
    return u"sha256:"_ustr + HashMetadata(sSeed);
}

AIChatConnectorOperationResult AIChatConnectorOperationRuntime::ExecuteReadOnly(
    const AIChatConnectorManifest& rManifest, const AIChatConnectorOperationRequest& rRequest) const
{
    AIChatConnectorOperationResult aResult;
    aResult.ConnectorId = rManifest.Id;

    if (rRequest.ConnectorId != rManifest.Id)
    {
        aResult.Message = u"connector-operation-denied reason=connector-id-mismatch"_ustr;
        return aResult;
    }
    if (rRequest.Action.isEmpty())
    {
        aResult.Message = u"connector-operation-denied reason=missing-action"_ustr;
        return aResult;
    }
    if (IsWriteAction(rRequest.Action))
    {
        aResult.Message = u"connector-operation-denied reason=write-action-forbidden action="_ustr
                          + rRequest.Action + u" connector-writeback=false data-write=false"_ustr;
        return aResult;
    }
    if (!IsReadActionAllowed(rManifest, rRequest))
    {
        aResult.Message
            = u"connector-operation-denied reason=read-only-envelope-invalid connector-writeback=false data-write=false"_ustr;
        return aResult;
    }
    if (rManifest.RequiresTenantPolicy && rRequest.TenantPolicyRef.isEmpty())
    {
        aResult.Message = u"connector-operation-denied reason=tenant-policy-required"_ustr;
        return aResult;
    }
    if (!rRequest.UserApproved)
    {
        aResult.Message = u"connector-operation-denied reason=user-approval-required"_ustr;
        return aResult;
    }
    if (rRequest.QueryRef.isEmpty())
    {
        aResult.Message = u"connector-operation-denied reason=missing-query-reference"_ustr;
        return aResult;
    }

    aResult.OperationId = MakeOperationId(rManifest, rRequest);
    aResult.EvidenceId = MakeConnectorEvidenceId(aResult.OperationId);
    aResult.HashReference = MakeConnectorHashReference(rManifest, rRequest);
    aResult.CitationId = AIChatSourceProvenance::MakeCitationId(aResult.OperationId);

    aResult.RegistryEntry.ObjectId = aResult.OperationId;
    aResult.RegistryEntry.Type = u"connector-result"_ustr;
    aResult.RegistryEntry.SourceSurface
        = rRequest.CallerSurface.isEmpty() ? u"connector-runtime"_ustr : rRequest.CallerSurface;
    aResult.RegistryEntry.State = u"registered"_ustr;
    aResult.RegistryEntry.EvidenceId = aResult.EvidenceId;
    aResult.RegistryEntry.HashReference = aResult.HashReference;
    aResult.RegistryEntry.OpenTarget = u"sidebar-preview"_ustr;
    aResult.RegistryEntry.PreviewMode = u"metadata-summary"_ustr;

    AIChatContentRegistry aRegistry;
    if (!aRegistry.RegisterObject(aResult.RegistryEntry))
    {
        aResult.Message = u"connector-operation-failed reason=registry-write-failed"_ustr;
        return aResult;
    }

    AIChatSourceProvenance aProvenance;
    AIChatSourceProvenanceEntry aSource;
    aSource.SourceId = AIChatSourceProvenance::MakeSourceId(aResult.OperationId);
    aSource.SourceType = u"connector-result"_ustr;
    aSource.CitationId = aResult.CitationId;
    aSource.EvidenceId = aResult.EvidenceId;
    aSource.HashReference = aResult.HashReference;
    aSource.SourceSurface = aResult.RegistryEntry.SourceSurface;
    aSource.OpenTarget = aResult.RegistryEntry.OpenTarget;
    aSource.SpanReference = u"span:connector-metadata:"_ustr + rManifest.Id;
    aSource.ReviewId = OUString();
    aProvenance.RegisterSource(aSource);

    aResult.Success = true;
    aResult.Message = u"connector-operation-complete connector-id="_ustr + rManifest.Id
                      + u" action=read operation-id="_ustr + aResult.OperationId
                      + u" type=connector-result evidence-category=data-fetch evidence-id="_ustr
                      + aResult.EvidenceId
                      + u" registry=true provenance=true open-target=sidebar-preview"_ustr
                      + u" metadata-only=true raw-payload=false network-started=false"_ustr
                      + u" connector-writeback=false data-write=false main-document-mutation=false"_ustr;
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
