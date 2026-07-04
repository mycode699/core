/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V4 M2 Day-1c — SSE-based streaming client for AI chat providers.
 * Connects to an HTTP endpoint, sends prompts, and reads SSE data chunks.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATSTREAMINGCLIENT_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATSTREAMINGCLIENT_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

/// Represents a single chunk from an SSE stream.
struct StreamChunk
{
    OUString text;    ///< Text content of this chunk
    bool isFinal = false; ///< True if this is the final chunk
    OUString error;   ///< Error message if the stream encountered an error
};

/// SSE-based streaming client for AI chat provider endpoints.
class SAL_DLLPUBLIC_EXPORT AgentChatStreamingClient
{
public:
    /// Construct a streaming client for the given endpoint.
    /// @param providerEndpoint URL of the AI provider's SSE endpoint
    /// @param apiKey Optional API key for authentication
    AgentChatStreamingClient(const OUString& providerEndpoint,
                             const OUString& apiKey);

    /// Connect to the provider endpoint.
    /// Returns true if the connection was established.
    bool connect();

    /// Send a prompt to the connected provider.
    /// Returns true if the prompt was sent successfully.
    bool sendPrompt(const OUString& prompt);

    /// Read the next chunk from the SSE stream.
    /// Blocks until data is available or the stream ends.
    StreamChunk readChunk();

    /// Check if the client is currently connected.
    bool isConnected();

    /// Disconnect from the provider.
    void disconnect();

private:
    OUString m_endpoint;
    OUString m_apiKey;
    bool m_connected;
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
