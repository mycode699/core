/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W2/M4: connector manifest loader).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatConnectorManifestLoader.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <sstream>
#include <string>

namespace sfx2::sidebar
{
namespace
{
constexpr sal_uInt64 MAX_CONNECTOR_MANIFEST_BYTES = 256 * 1024;

OUString ReadString(const boost::property_tree::ptree& rTree, const char* pPath)
{
    return OUString::fromUtf8(rTree.get<std::string>(pPath, std::string()));
}

bool ReadBool(const boost::property_tree::ptree& rTree, const char* pPath, bool bDefault)
{
    return rTree.get<bool>(pPath, bDefault);
}

sal_Int32 ReadInt(const boost::property_tree::ptree& rTree, const char* pPath, sal_Int32 nDefault)
{
    return rTree.get<sal_Int32>(pPath, nDefault);
}

std::vector<OUString> ReadStringArray(const boost::property_tree::ptree& rTree,
                                      const char* pPath)
{
    std::vector<OUString> aValues;
    const auto aChild = rTree.get_child_optional(pPath);
    if (!aChild)
        return aValues;
    for (const auto& rItem : *aChild)
        aValues.push_back(OUString::fromUtf8(rItem.second.get_value<std::string>()));
    return aValues;
}

std::vector<AIChatConnectorScope> ReadScopes(const boost::property_tree::ptree& rTree)
{
    std::vector<AIChatConnectorScope> aScopes;
    const auto aChild = rTree.get_child_optional("scopes");
    if (!aChild)
        return aScopes;

    for (const auto& rItem : *aChild)
    {
        AIChatConnectorScope aScope;
        aScope.Name = ReadString(rItem.second, "name");
        aScope.DataClass = ReadString(rItem.second, "dataClass");
        aScopes.push_back(aScope);
    }
    return aScopes;
}

bool IsLowerAscii(sal_Unicode c)
{
    return c >= u'a' && c <= u'z';
}

bool IsDigit(sal_Unicode c)
{
    return c >= u'0' && c <= u'9';
}

bool IsLowerHex(sal_Unicode c)
{
    return IsDigit(c) || (c >= u'a' && c <= u'f');
}

bool Contains(const std::vector<OUString>& rValues, const OUString& rValue)
{
    return std::find(rValues.begin(), rValues.end(), rValue) != rValues.end();
}

bool HasWriteScope(const std::vector<OUString>& rScopes)
{
    return std::any_of(rScopes.begin(), rScopes.end(), [](const OUString& rScope) {
        return rScope.indexOf(u":write"_ustr) >= 0 || rScope.startsWith(u"write:"_ustr);
    });
}

bool HasWriteConnectorScope(const std::vector<AIChatConnectorScope>& rScopes)
{
    return std::any_of(rScopes.begin(), rScopes.end(), [](const AIChatConnectorScope& rScope) {
        return rScope.Name.startsWith(u"write:"_ustr);
    });
}
}

AIChatConnectorManifestLoadResult
AIChatConnectorManifestLoader::LoadFromFile(const OUString& rFileUrl) const
{
    AIChatConnectorManifestLoadResult aResult;
    if (rFileUrl.isEmpty())
    {
        aResult.Message = u"connector-manifest-load-failed reason=missing-file-url"_ustr;
        return aResult;
    }

    osl::File aFile(rFileUrl);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
    {
        aResult.Message = u"connector-manifest-load-failed reason=file-open-failed"_ustr;
        return aResult;
    }

    sal_uInt64 nSize = 0;
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_CONNECTOR_MANIFEST_BYTES)
    {
        aFile.close();
        aResult.Message = u"connector-manifest-load-failed reason=file-size-invalid"_ustr;
        return aResult;
    }

    std::vector<char> aBuffer(static_cast<size_t>(nSize));
    sal_uInt64 nRead = 0;
    if (nSize > 0
        && aFile.read(aBuffer.data(), nSize, nRead) != osl::FileBase::E_None)
    {
        aFile.close();
        aResult.Message = u"connector-manifest-load-failed reason=file-read-failed"_ustr;
        return aResult;
    }
    aFile.close();

    if (nRead != nSize)
    {
        aResult.Message = u"connector-manifest-load-failed reason=file-read-short"_ustr;
        return aResult;
    }

    return LoadFromString(OStringToOUString(OString(aBuffer.data(), static_cast<sal_Int32>(nSize)),
                                           RTL_TEXTENCODING_UTF8));
}

AIChatConnectorManifestLoadResult
AIChatConnectorManifestLoader::LoadFromString(const OUString& rJson) const
{
    AIChatConnectorManifestLoadResult aResult;
    try
    {
        std::stringstream aStream(OUStringToOString(rJson, RTL_TEXTENCODING_UTF8).getStr());
        boost::property_tree::ptree aTree;
        boost::property_tree::read_json(aStream, aTree);

        AIChatConnectorManifest aManifest;
        aManifest.Id = ReadString(aTree, "id");
        aManifest.Version = ReadString(aTree, "version");
        aManifest.DisplayName = ReadString(aTree, "displayName");
        aManifest.TrustSource = ReadString(aTree, "trust.source");
        aManifest.Publisher = ReadString(aTree, "trust.publisher");
        aManifest.ManifestSha256 = ReadString(aTree, "trust.manifestSha256");
        aManifest.ReviewState = ReadString(aTree, "trust.reviewState");
        aManifest.InstallScope = ReadString(aTree, "trust.installScope");
        aManifest.SignatureRequired = ReadBool(aTree, "trust.signatureRequired", false);
        aManifest.AllowUnsigned = ReadBool(aTree, "trust.allowUnsigned", true);
        aManifest.OperationMode = ReadString(aTree, "operations.mode");
        aManifest.AllowedActions = ReadStringArray(aTree, "operations.allowedActions");
        aManifest.Writeback = ReadBool(aTree, "operations.writeback", true);
        aManifest.WriteScopesAllowed = ReadBool(aTree, "operations.writeScopesAllowed", true);
        aManifest.RuntimeWriteImplementation
            = ReadString(aTree, "operations.runtimeWriteImplementation");
        aManifest.AuthType = ReadString(aTree, "auth.type");
        aManifest.AuthScopes = ReadStringArray(aTree, "auth.scopes");
        aManifest.TokenStorage = ReadString(aTree, "auth.tokenStorage");
        aManifest.AuthFlowStrategy = ReadString(aTree, "auth.flow.strategy");
        aManifest.EmbeddedWebView = ReadBool(aTree, "auth.flow.embeddedWebView", true);
        aManifest.AuthCallback = ReadString(aTree, "auth.flow.callback");
        aManifest.RuntimeAuthImplementation
            = ReadString(aTree, "auth.flow.runtimeAuthImplementation");
        aManifest.RefreshStrategy = ReadString(aTree, "auth.refreshPolicy.strategy");
        aManifest.BackgroundRefresh
            = ReadBool(aTree, "auth.refreshPolicy.backgroundRefresh", true);
        aManifest.StoresRefreshToken
            = ReadBool(aTree, "auth.refreshPolicy.storesRefreshToken", true);
        aManifest.RuntimeRefreshImplementation
            = ReadString(aTree, "auth.refreshPolicy.runtimeRefreshImplementation");
        aManifest.ServiceModes = ReadStringArray(aTree, "serviceModes");
        aManifest.Scopes = ReadScopes(aTree);
        aManifest.EvidenceEmit = ReadBool(aTree, "evidence.emit", false);
        aManifest.EvidenceCategory = ReadString(aTree, "evidence.category");
        aManifest.RequiresTenantPolicy = ReadBool(aTree, "requiresTenantPolicy", false);
        aManifest.CacheTTLSeconds = ReadInt(aTree, "cacheTTLSeconds", 300);
        return Validate(aManifest);
    }
    catch (const std::exception&)
    {
        aResult.Message = u"connector-manifest-load-failed reason=json-parse-failed"_ustr;
        return aResult;
    }
}

AIChatConnectorManifestLoadResult
AIChatConnectorManifestLoader::Validate(const AIChatConnectorManifest& rManifest) const
{
    AIChatConnectorManifestLoadResult aResult;
    aResult.Manifest = rManifest;

    if (!IsValidConnectorId(rManifest.Id))
    {
        aResult.Message = u"connector-manifest-invalid reason=id"_ustr;
        return aResult;
    }
    if (!IsValidTrustEnvelope(rManifest))
    {
        aResult.Message = u"connector-manifest-invalid reason=trust-envelope connector-id="_ustr
                          + rManifest.Id;
        return aResult;
    }
    if (!IsReadOnlyOperationsEnvelope(rManifest))
    {
        aResult.Message = u"connector-manifest-invalid reason=read-only-operations connector-id="_ustr
                          + rManifest.Id;
        return aResult;
    }
    if (!IsAuthFlowAllowed(rManifest))
    {
        aResult.Message = u"connector-manifest-invalid reason=auth-flow connector-id="_ustr
                          + rManifest.Id;
        return aResult;
    }
    if (!IsRefreshPolicyAllowed(rManifest))
    {
        aResult.Message = u"connector-manifest-invalid reason=token-refresh connector-id="_ustr
                          + rManifest.Id;
        return aResult;
    }
    if (Contains(rManifest.ServiceModes, u"offline"_ustr))
    {
        aResult.Message = u"connector-manifest-invalid reason=offline-service-mode connector-id="_ustr
                          + rManifest.Id;
        return aResult;
    }
    if (!rManifest.EvidenceEmit || rManifest.EvidenceCategory != u"data-fetch"_ustr)
    {
        aResult.Message = u"connector-manifest-invalid reason=evidence connector-id="_ustr
                          + rManifest.Id;
        return aResult;
    }
    if (RequiresTenantPolicyForScopes(rManifest))
    {
        aResult.Message = u"connector-manifest-invalid reason=tenant-policy-required connector-id="_ustr
                          + rManifest.Id;
        return aResult;
    }

    aResult.Success = true;
    aResult.Message = u"connector-manifest-loaded connector-id="_ustr + rManifest.Id
                      + u" source="_ustr + rManifest.TrustSource + u" publisher="_ustr
                      + rManifest.Publisher + u" manifest-sha256="_ustr
                      + rManifest.ManifestSha256 + u" review-state="_ustr
                      + rManifest.ReviewState + u" install-scope="_ustr
                      + rManifest.InstallScope
                      + u" signature-required=true allow-unsigned=false"_ustr
                      + u" mode=read-only allowed-actions=read writeback=false"_ustr
                      + u" auth-runtime=not-started refresh-runtime=not-started"_ustr
                      + u" evidence-category=data-fetch metadata-only=true"_ustr
                      + u" network-started=false connector-writeback=false raw-payload=false"_ustr;
    return aResult;
}

bool AIChatConnectorManifestLoader::IsValidConnectorId(const OUString& rId)
{
    if (rId.getLength() < 3 || rId.getLength() > 32 || !IsLowerAscii(rId[0]))
        return false;
    for (sal_Int32 i = 1; i < rId.getLength(); ++i)
    {
        const sal_Unicode c = rId[i];
        if (!IsLowerAscii(c) && !IsDigit(c) && c != u'-')
            return false;
    }
    return true;
}

bool AIChatConnectorManifestLoader::IsValidPublisher(const OUString& rPublisher)
{
    if (rPublisher.getLength() < 3 || rPublisher.getLength() > 64
        || !IsLowerAscii(rPublisher[0]))
    {
        return false;
    }
    for (sal_Int32 i = 1; i < rPublisher.getLength(); ++i)
    {
        const sal_Unicode c = rPublisher[i];
        if (!IsLowerAscii(c) && !IsDigit(c) && c != u'-')
            return false;
    }
    return true;
}

bool AIChatConnectorManifestLoader::IsValidManifestSha256(const OUString& rManifestSha256)
{
    if (rManifestSha256.getLength() != 71 || !rManifestSha256.startsWith(u"sha256:"_ustr))
        return false;
    for (sal_Int32 i = 7; i < rManifestSha256.getLength(); ++i)
    {
        if (!IsLowerHex(rManifestSha256[i]))
            return false;
    }
    return true;
}

bool AIChatConnectorManifestLoader::IsValidTrustEnvelope(
    const AIChatConnectorManifest& rManifest)
{
    if (!IsValidPublisher(rManifest.Publisher)
        || !IsValidManifestSha256(rManifest.ManifestSha256) || !rManifest.SignatureRequired
        || rManifest.AllowUnsigned)
    {
        return false;
    }

    if (rManifest.TrustSource == u"builtin"_ustr)
    {
        return rManifest.Publisher == u"kqoffice"_ustr
               && rManifest.ReviewState == u"repo-reviewed"_ustr
               && rManifest.InstallScope == u"builtin"_ustr;
    }
    if (rManifest.TrustSource == u"community"_ustr)
    {
        return (rManifest.InstallScope == u"user"_ustr
                && rManifest.ReviewState == u"security-reviewed"_ustr)
               || (rManifest.InstallScope == u"tenant"_ustr
                   && rManifest.ReviewState == u"tenant-approved"_ustr);
    }
    if (rManifest.TrustSource == u"enterprise-admin"_ustr)
    {
        return rManifest.InstallScope == u"tenant"_ustr
               && rManifest.ReviewState == u"tenant-approved"_ustr;
    }
    return false;
}

bool AIChatConnectorManifestLoader::IsReadOnlyOperationsEnvelope(
    const AIChatConnectorManifest& rManifest)
{
    return rManifest.OperationMode == u"read-only"_ustr
           && rManifest.AllowedActions.size() == 1
           && rManifest.AllowedActions.front() == u"read"_ustr && !rManifest.Writeback
           && !rManifest.WriteScopesAllowed
           && rManifest.RuntimeWriteImplementation == u"not-started"_ustr
           && !HasWriteScope(rManifest.AuthScopes)
           && !HasWriteConnectorScope(rManifest.Scopes);
}

bool AIChatConnectorManifestLoader::IsAuthFlowAllowed(
    const AIChatConnectorManifest& rManifest)
{
    if (rManifest.EmbeddedWebView || rManifest.RuntimeAuthImplementation != u"not-started"_ustr)
        return false;

    if (rManifest.AuthType == u"none"_ustr)
    {
        return rManifest.TokenStorage == u"none"_ustr && rManifest.AuthScopes.empty()
               && rManifest.AuthFlowStrategy == u"not-applicable"_ustr
               && rManifest.AuthCallback == u"none"_ustr;
    }
    if (rManifest.AuthType == u"oauth2"_ustr)
    {
        return rManifest.TokenStorage != u"none"_ustr && !rManifest.AuthScopes.empty()
               && rManifest.AuthFlowStrategy == u"system-browser-loopback"_ustr
               && rManifest.AuthCallback == u"loopback-127.0.0.1"_ustr;
    }
    if (rManifest.AuthType == u"api-key"_ustr)
    {
        return rManifest.TokenStorage != u"none"_ustr && !rManifest.AuthScopes.empty()
               && rManifest.AuthFlowStrategy == u"manual-secret-entry"_ustr
               && rManifest.AuthCallback == u"manual-entry"_ustr;
    }
    return false;
}

bool AIChatConnectorManifestLoader::IsRefreshPolicyAllowed(
    const AIChatConnectorManifest& rManifest)
{
    if (rManifest.BackgroundRefresh || rManifest.StoresRefreshToken
        || rManifest.RuntimeRefreshImplementation != u"not-started"_ustr)
    {
        return false;
    }

    if (rManifest.AuthType == u"none"_ustr)
        return rManifest.RefreshStrategy == u"not-applicable"_ustr;
    if (rManifest.AuthType == u"oauth2"_ustr)
        return rManifest.RefreshStrategy == u"reauth-on-expiry"_ustr;
    if (rManifest.AuthType == u"api-key"_ustr)
        return rManifest.RefreshStrategy == u"manual-rotate"_ustr;
    return false;
}

bool AIChatConnectorManifestLoader::RequiresTenantPolicyForScopes(
    const AIChatConnectorManifest& rManifest)
{
    if (!Contains(rManifest.ServiceModes, u"cloud"_ustr) || rManifest.RequiresTenantPolicy)
        return false;

    return std::any_of(rManifest.Scopes.begin(), rManifest.Scopes.end(),
                       [](const AIChatConnectorScope& rScope) {
                           return rScope.DataClass == u"confidential"_ustr
                                  || rScope.DataClass == u"secret"_ustr;
                       });
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
