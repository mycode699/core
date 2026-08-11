/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * MCP JSON-RPC 2.0 host (stdio / request-response).
 * Local-only; no network listen. apply_approved still needs humanApproval.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIMCPSTDIO_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIMCPSTDIO_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

/// MCP-over-JSON-RPC helpers for external hosts (Cursor / Claude / CLI).
class SAL_DLLPUBLIC_EXPORT DocumentAIMCPStdio
{
public:
    /// Protocol id advertised in initialize.
    static OUString protocolVersion();

    /// Server name / version strings.
    static OUString serverName();
    static OUString serverVersion();

    /**
     * Handle one JSON-RPC request object (as text) and return one response object.
     * Supports methods:
     *   initialize | ping | tools/list | tools/call | notifications/initialized (empty ack)
     * Unknown methods → JSON-RPC error -32601.
     */
    static OUString handleRequestJson(const OUString& rRequestJson);

    /**
     * Blocking stdio loop:
     * - Prefer MCP Content-Length framing when headers appear
     * - Else NDJSON (one JSON object per line) for easy testing
     * Returns process exit code (0).
     */
    static int runStdioLoop();
};

} // namespace kqoffice::ai::chat

// C ABI for harnesses / thin wrappers (UTF-8 in/out; caller frees with free()).
extern "C" SAL_DLLPUBLIC_EXPORT char* kqoffice_mcp_handle_json(const char* requestUtf8);
extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_mcp_free(char* p);

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
