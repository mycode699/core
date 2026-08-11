/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Enterprise connectors (default OFF).
 * Local-first: no auto-discovery, no silent network, explicit enable + grant.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIENTERPRISECONNECTORS_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIENTERPRISECONNECTORS_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

struct EnterpriseConnector
{
    OUString id; ///< stable id e.g. corp-sharepoint
    OUString nameZh;
    OUString baseUrl; ///< never called unless enabled+granted
    OUString scopeSummaryZh; ///< human disclosure
    bool enabled = false; ///< per-connector switch (still needs global + grant)
    bool granted = false; ///< explicit user grant this session or persisted
    bool requiresNetwork = true;
    /// When true (default), only private/loopback/corp hosts may be contacted.
    bool privateOnly = true;
    /// Optional path template; `{query}` substituted. e.g. /api/search?q={query}
    OUString gatewayPath;
    /// GET (default) | POST — search/fetch may stay GET; custom ops often POST.
    OUString httpMethod;
    /// Authorization scheme for private gateway: bearer (default) | header | none
    OUString authScheme;
    /// Header name when authScheme=header (default Authorization).
    OUString authHeaderName;
    /// true if a local secret is present for this id (never exposes the secret).
    bool hasLocalSecret = false;
    /// Optional OAuth device-code endpoints (private gateway only).
    /// Empty → defaults: /oauth/device/code and /oauth/token
    OUString deviceAuthPath;
    OUString deviceTokenPath;
    /// Optional OAuth client_id for device flow (non-secret).
    OUString clientId;
};

/// In-process + local-file device authorization session (RFC 8628-shaped).
struct ConnectorDeviceSession
{
    OUString connectorId;
    OUString deviceCode;
    OUString userCode;
    OUString verificationUri;
    OUString verificationUriComplete;
    sal_Int32 intervalSec = 5;
    sal_Int64 expiresAtUnix = 0;
    /// pending | authorized | denied | expired | error | none
    OUString status;
    OUString messageZh;
    bool networkAttempted = false;
};

struct ConnectorInvokeRequest
{
    OUString connectorId;
    OUString operation; ///< list|search|fetch|status|post
    OUString query;
    /// Optional POST body (JSON or form). If empty and method=POST, builds {"q","op"}.
    OUString body;
    /// Optional method override (GET|POST). Empty → connector.httpMethod or heuristic.
    OUString httpMethod;
    /// Must be true for any non-status operation that would leave the machine.
    bool explicitUserApproval = false;
};

struct ConnectorInvokeResult
{
    bool success = false;
    bool networkAttempted = false;
    bool usedAuthHeader = false;
    OUString connectorId;
    OUString httpMethod;
    OUString status; ///< ok | disabled | not-granted | unknown | unsupported | policy-denied | error
    OUString messageZh;
    OUString content; ///< payload if any
};

/// Registry + hard gate for enterprise connectors.
class SAL_DLLPUBLIC_EXPORT DocumentAIEnterpriseConnectors
{
public:
    /// Global master switch (prefs / env). Default false.
    static bool globalEnabled();

    /// Load connectors from ~/.config/kqoffice/connectors.json (optional).
    /// Never auto-enables network.
    static std::vector<EnterpriseConnector> listConnectors();

    /// JSON array of connector ids for harnesses.
    static OUString listConnectorIdsJson();

    /// Human multi-line status for settings / transcript.
    static OUString statusSummaryZh();

    /// Hard-gated invoke: private gateway GET/POST + optional local secret auth.
    /// Never silent public egress.
    static ConnectorInvokeResult invoke(const ConnectorInvokeRequest& rReq);

    /// Persist grant flag for connector id in session file (local).
    static bool setGranted(const OUString& rId, bool bGranted);

    /// Persist enabled flag for connector id (rewrites connectors.json).
    static bool setEnabled(const OUString& rId, bool bEnabled);

    /// Store secret for connector id in local secrets file (mode best-effort 0600).
    /// Empty secret removes the entry. Never logs the secret value.
    static bool setLocalSecret(const OUString& rId, const OUString& rSecret);

    /// True when a non-empty secret exists for id (file or env).
    static bool hasLocalSecret(const OUString& rId);

    /// True when URL host is loopback / RFC1918 / *.local|*.corp|*.internal|*.lan.
    static bool isPrivateBaseUrl(const OUString& rBaseUrl);

    /// Human-readable row for settings list: "id · name · en=0 · grant=0 · private".
    static OUString formatListRowZh(const EnterpriseConnector& c);

    /// Start OAuth device-code on private gateway (requires global+enabled+approval).
    /// Does not auto-poll; user opens verification URI then calls pollDeviceAuth.
    static ConnectorDeviceSession startDeviceAuth(const OUString& rId,
                                                  bool bExplicitUserApproval);

    /// One poll of token endpoint. On success stores access_token via setLocalSecret.
    static ConnectorDeviceSession pollDeviceAuth(const OUString& rId,
                                                 bool bExplicitUserApproval);

    /// Local session snapshot (no network).
    static ConnectorDeviceSession deviceAuthStatus(const OUString& rId);

    /// Clear pending device session for id (local only).
    static bool clearDeviceSession(const OUString& rId);

    static OUString defaultConfigPath();
    static OUString defaultGrantsPath();
    /// ~/.config/kqoffice/connector-secrets.json — never commit; local only.
    static OUString defaultSecretsPath();
    static OUString defaultDeviceSessionsPath();
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
