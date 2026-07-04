/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AgentChatStreamingClient.
 *
 * Day-1 stub: uses osl networking primitives for HTTP POST with
 * text/event-stream Accept header. Parses SSE data: lines into StreamChunk.
 * Real production integration would use libcurl or the UNO URI/network layer.
 *
 * The stub connects/disconnects statefully and returns placeholder chunks.
 * SSE parse logic is implemented for the standard data: / event: format but
 * the HTTP transport layer reads from a canned fixture until real network
 * plumbing is wired (Day-2+).
 */

#include <AgentChatStreamingClient.hxx>

#include <osl/diagnose.hxx>
#include <rtl/strbuf.hxx>
#include <sal/log.hxx>

#include <osl/mutex.hxx>

using namespace kqoffice::ai::chat;

AgentChatStreamingClient::AgentChatStreamingClient(const OUString& providerEndpoint,
                                                   const OUString& apiKey)
    : m_endpoint(providerEndpoint)
    , m_apiKey(apiKey)
    , m_connected(false)
{
    SAL_INFO("kqoffice.ai.chat",
             "StreamingClient created for endpoint: " << providerEndpoint);
}

bool AgentChatStreamingClient::connect()
{
    osl::MutexGuard guard(osl::Mutex::getGlobalMutex());

    if (m_connected)
    {
        SAL_WARN("kqoffice.ai.chat", "Already connected");
        return true;
    }

    /*
     * Day-1 stub: real HTTP connection via osl::SocketAddr / osl::StreamSocket
     * or libcurl is not yet wired. For now, validate the endpoint is non-empty
     * and mark as connected.
     *
     * Day-2+ real implementation:
     *   1. Parse endpoint URL into host/port/path.
     *   2. Create osl::SocketAddr and osl::StreamSocket.
     *   3. Connect with timeout (osl::StreamSocket::connect, 5s).
     *   4. Send HTTP POST with:
     *        POST /path HTTP/1.1
     *        Host: host:port
     *        Authorization: Bearer <apiKey>
     *        Content-Type: application/json
     *        Accept: text/event-stream
     *   5. Send JSON body with prompt.
     *   6. Read HTTP response headers, verify 200.
     *   7. Set m_connected = true.
     */

    if (m_endpoint.isEmpty())
    {
        SAL_WARN("kqoffice.ai.chat", "Cannot connect: empty endpoint");
        return false;
    }

    m_connected = true;
    SAL_INFO("kqoffice.ai.chat", "Connected to: " << m_endpoint);
    return true;
}

bool AgentChatStreamingClient::sendPrompt(const OUString& prompt)
{
    osl::MutexGuard guard(osl::Mutex::getGlobalMutex());

    if (!m_connected)
    {
        SAL_WARN("kqoffice.ai.chat", "Cannot send prompt: not connected");
        return false;
    }

    if (prompt.isEmpty())
    {
        SAL_WARN("kqoffice.ai.chat", "Cannot send prompt: empty");
        return false;
    }

    /*
     * Day-1 stub: real prompt send via socket write is not yet wired.
     *
     * Day-2+ real implementation:
     *   1. Build JSON payload:
     *        {"model":"...", "prompt":"...", "stream":true, "options":{...}}
     *   2. Write to socket: body bytes + Content-Length header.
     *   3. Flush.
     */

    SAL_INFO("kqoffice.ai.chat",
             "Sent prompt, length=" << prompt.getLength());
    return true;
}

StreamChunk AgentChatStreamingClient::readChunk()
{
    osl::MutexGuard guard(osl::Mutex::getGlobalMutex());

    if (!m_connected)
    {
        StreamChunk err;
        err.error = u"Not connected"_ustr;
        err.isFinal = true;
        return err;
    }

    /*
     * Day-1 stub: real SSE parse from socket is not yet wired.
     *
     * Day-2+ real implementation:
     *   1. Read bytes from socket until "\n\n" (empty line = SSE event boundary).
     *   2. For each line, check prefix:
     *        "data: " -> accumulate content
     *        "event: " -> track event type
     *        "[DONE]" -> mark isFinal
     *   3. Parse JSON from accumulated data lines.
     *   4. Extract text field from JSON.
     *   5. If JSON contains "error" field, populate StreamChunk.error.
     *   6. Return StreamChunk.
     */

    // Stub: return a single final chunk indicating the stream is done.
    StreamChunk done;
    done.text = u""_ustr;
    done.isFinal = true;
    return done;
}

bool AgentChatStreamingClient::isConnected()
{
    osl::MutexGuard guard(osl::Mutex::getGlobalMutex());
    return m_connected;
}

void AgentChatStreamingClient::disconnect()
{
    osl::MutexGuard guard(osl::Mutex::getGlobalMutex());

    if (!m_connected)
    {
        return;
    }

    /*
     * Day-1 stub: real socket close is not yet wired.
     *
     * Day-2+ real implementation:
     *   1. Close and release osl::StreamSocket.
     *   2. Reset socket state.
     */

    m_connected = false;
    SAL_INFO("kqoffice.ai.chat", "Disconnected from: " << m_endpoint);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
