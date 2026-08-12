/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1 Day-1: Ollama adapter).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "OllamaAdapter.hxx"
#include "KqNetSocket.hxx"

#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

namespace kqoffice::ai
{
namespace
{
// 100ms cap. Plenty for a localhost listener; short enough that a missing
// daemon never stalls the UI thread or a cppunit run on a dev laptop.
constexpr int kProbeTimeoutMs = 100;

// 30s cap for /api/generate. Long enough for a 7B local model to produce
// a short answer on a laptop CPU; short enough that a wedged daemon is
// not a UI hang.
constexpr int kGenerateTimeoutMs = 30 * 1000;

// 8KB ceiling on /api/tags response. Real-world response is well under
// 4KB even for ~20 installed models. Bound everything so a hostile or
// rogue listener cannot drag us into an unbounded read.
constexpr std::size_t kMaxTagsResponseBytes = 8 * 1024;

// 256KB ceiling on /api/generate response. A non-stream short answer is
// typically well under 4KB; 256KB is generous headroom while still
// preventing an unbounded read if the daemon misbehaves or streams.
constexpr std::size_t kMaxGenerateResponseBytes = 256 * 1024;

/// One-shot blocking connect to 127.0.0.1:kPort. Caller must kqNetClose().
KqSock openConnection(int timeoutMs)
{
    return kqNetConnect(OllamaAdapter::kHost, OllamaAdapter::kPort, timeoutMs);
}

bool sendAll(KqSock fd, const char* buf, std::size_t len)
{
    return kqNetSendAll(fd, buf, len);
}

std::string readAllBounded(KqSock fd, std::size_t maxBytes)
{
    return kqNetRecvBounded(fd, maxBytes);
}

/// Escape `"`, `\`, and control chars (<0x20) inside an OString so the
/// result can be embedded inside a JSON string literal. Prompts are
/// user-supplied — we cannot trust them to be JSON-clean.
void appendJsonEscaped(OStringBuffer& out, const OString& s)
{
    const sal_Int32 n = s.getLength();
    const char* p = s.getStr();
    for (sal_Int32 i = 0; i < n; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        switch (c)
        {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (c < 0x20)
                {
                    char hex[8];
                    ::snprintf(hex, sizeof(hex), "\\u%04x",
                               static_cast<unsigned>(c));
                    out.append(hex);
                }
                else
                {
                    out.append(static_cast<char>(c));
                }
                break;
        }
    }
}

} // namespace

OUString OllamaAdapter::probe()
{
    KqSock fd = openConnection(kProbeTimeoutMs);
    if (fd < 0)
        return u"unreachable"_ustr;
    kqNetClose(fd);
    return u"reachable"_ustr;
}

std::vector<OUString> OllamaAdapter::listModels()
{
    KqSock fd = openConnection(kProbeTimeoutMs);
    if (fd < 0)
        return {};

    // HTTP/1.0 — connection: close is implicit, daemon will half-close
    // after writing the body. No keepalive, no chunked complications.
    const std::string req = std::string("GET ") + kTagsPath
                          + " HTTP/1.0\r\nHost: " + kHost
                          + "\r\nAccept: application/json\r\n\r\n";
    if (!sendAll(fd, req.data(), req.size()))
    {
        kqNetClose(fd);
        return {};
    }

    std::string raw = readAllBounded(fd, kMaxTagsResponseBytes);
    kqNetClose(fd);
    if (raw.empty())
        return {};

    // Split header / body on the first CRLFCRLF. Anything before is
    // the status line + headers we do not need to inspect for Day-1
    // (a non-200 daemon will return an empty/non-JSON body and the
    // parse below will simply yield nothing).
    const std::string sep = "\r\n\r\n";
    auto pos = raw.find(sep);
    if (pos == std::string::npos)
        return {};
    OString body(raw.data() + pos + sep.size(),
                 static_cast<sal_Int32>(raw.size() - pos - sep.size()));
    return parseModelsJson(body);
}

OString OllamaAdapter::buildGenerateRequestJson(const OUString& model, const OUString& prompt,
                                                bool bStream)
{
    OString modelUtf8 = OUStringToOString(model, RTL_TEXTENCODING_UTF8);
    OString promptUtf8 = OUStringToOString(prompt, RTL_TEXTENCODING_UTF8);
    OStringBuffer body(256 + promptUtf8.getLength());
    body.append("{\"model\":\"");
    appendJsonEscaped(body, modelUtf8);
    body.append("\",\"prompt\":\"");
    appendJsonEscaped(body, promptUtf8);
    body.append("\",\"stream\":");
    body.append(bStream ? "true" : "false");
    // Stream path: plain text (no forced JSON mode) so tokens stream cleanly.
    if (bStream)
        body.append(",\"options\":{\"temperature\":0}}");
    else
        body.append(",\"format\":\"json\",\"options\":{\"temperature\":0}}");
    return body.makeStringAndClear();
}

OUString OllamaAdapter::generate(const OUString& model, const OUString& prompt)
{
    // Build the JSON request body up front so we can supply an exact
    // Content-Length and avoid chunked encoding on the request side.
    OString jsonBody = buildGenerateRequestJson(model, prompt);

    KqSock fd = openConnection(kGenerateTimeoutMs);
    if (fd < 0)
        return OUString();

    // HTTP/1.0 POST with explicit Content-Length. Connection close is
    // implicit so the daemon half-closes after the body is delivered.
    OStringBuffer hdr(256);
    hdr.append("POST ");
    hdr.append(kGeneratePath);
    hdr.append(" HTTP/1.0\r\nHost: ");
    hdr.append(kHost);
    hdr.append("\r\nContent-Type: application/json\r\nAccept: application/json"
               "\r\nContent-Length: ");
    hdr.append(static_cast<sal_Int32>(jsonBody.getLength()));
    hdr.append("\r\n\r\n");
    OString hdrStr = hdr.makeStringAndClear();

    if (!sendAll(fd, hdrStr.getStr(), static_cast<std::size_t>(hdrStr.getLength()))
        || !sendAll(fd, jsonBody.getStr(),
                    static_cast<std::size_t>(jsonBody.getLength())))
    {
        kqNetClose(fd);
        return OUString();
    }

    std::string raw = readAllBounded(fd, kMaxGenerateResponseBytes);
    kqNetClose(fd);
    if (raw.empty())
        return OUString();

    // Reject non-2xx responses up front. Status line is "HTTP/1.x NNN ...".
    // A malformed line falls through to the JSON parser which will yield
    // an empty OUString.
    if (raw.size() >= 12 && raw.compare(0, 5, "HTTP/") == 0)
    {
        const std::size_t spc = raw.find(' ');
        if (spc != std::string::npos && spc + 4 <= raw.size())
        {
            const char d0 = raw[spc + 1];
            if (d0 != '2')
                return OUString();
        }
    }

    const std::string sep = "\r\n\r\n";
    auto pos = raw.find(sep);
    if (pos == std::string::npos)
        return OUString();
    OString respBody(raw.data() + pos + sep.size(),
                     static_cast<sal_Int32>(raw.size() - pos - sep.size()));
    return parseGenerateJson(respBody);
}

std::vector<OUString> OllamaAdapter::parseModelsJson(const OString& body)
{
    std::vector<OUString> out;
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();

    sal_Int32 i = 0;
    while (i < n)
    {
        // Find the next `"name"` token.
        sal_Int32 found = body.indexOf("\"name\"", i);
        if (found < 0)
            break;
        i = found + 6; // past `"name"`

        // Skip whitespace, then expect ':'.
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n || s[i] != ':')
            continue;
        ++i; // past ':'
        // Skip whitespace, then expect opening '"'.
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n || s[i] != '"')
            continue;
        ++i; // past opening '"'

        // Capture until closing '"', honoring `\"` and `\\` escapes.
        OStringBuffer val;
        while (i < n)
        {
            const char c = s[i];
            if (c == '\\' && i + 1 < n)
            {
                const char esc = s[i + 1];
                // Pass common JSON escapes through literally — model
                // names from Ollama are ASCII tags like "qwen2.5:7b"
                // so we never expect escapes, but parse defensively.
                switch (esc)
                {
                    case '"': val.append('"'); break;
                    case '\\': val.append('\\'); break;
                    case '/': val.append('/'); break;
                    case 'n': val.append('\n'); break;
                    case 't': val.append('\t'); break;
                    case 'r': val.append('\r'); break;
                    default:  val.append(esc); break;
                }
                i += 2;
                continue;
            }
            if (c == '"')
                break;
            val.append(c);
            ++i;
        }
        if (i >= n)
            break; // unterminated string — give up on this entry

        out.push_back(OStringToOUString(val.makeStringAndClear(),
                                        RTL_TEXTENCODING_UTF8));
        ++i; // past closing '"'
    }
    return out;
}

OUString OllamaAdapter::parseGenerateJson(const OString& body)
{
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();

    // Linear scan for the first `"response"` key. Same defensive style
    // as parseModelsJson — no full JSON parser, just enough to lift the
    // single string field we care about.
    sal_Int32 i = 0;
    while (i < n)
    {
        sal_Int32 found = body.indexOf("\"response\"", i);
        if (found < 0)
            return OUString();
        i = found + 10; // past `"response"`

        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n || s[i] != ':')
            continue;
        ++i; // past ':'
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n || s[i] != '"')
            continue;
        ++i; // past opening '"'

        OStringBuffer val;
        while (i < n)
        {
            const char c = s[i];
            if (c == '\\' && i + 1 < n)
            {
                const char esc = s[i + 1];
                switch (esc)
                {
                    case '"':  val.append('"'); break;
                    case '\\': val.append('\\'); break;
                    case '/':  val.append('/'); break;
                    case 'n':  val.append('\n'); break;
                    case 't':  val.append('\t'); break;
                    case 'r':  val.append('\r'); break;
                    case 'b':  val.append('\b'); break;
                    case 'f':  val.append('\f'); break;
                    default:   val.append(esc); break;
                }
                i += 2;
                continue;
            }
            if (c == '"')
                break;
            val.append(c);
            ++i;
        }
        if (i >= n)
            return OUString(); // unterminated string

        return OStringToOUString(val.makeStringAndClear(),
                                 RTL_TEXTENCODING_UTF8);
    }
    return OUString();
}

OUString OllamaAdapter::generateStream(const OUString& model, const OUString& prompt,
                                       const StreamChunkFn& rOnChunk,
                                       const StreamCancelFn& rShouldCancel)
{
    // curl -N NDJSON stream from Ollama /api/generate
    char bodyPath[] = "/tmp/kqoffice-ollama-sbody-XXXXXX";
    const int bodyFd = ::mkstemp(bodyPath);
    if (bodyFd < 0)
        return OUString();
    const OString jsonBody = buildGenerateRequestJson(model, prompt, /*bStream*/ true);
    (void)::write(bodyFd, jsonBody.getStr(), static_cast<size_t>(jsonBody.getLength()));
    ::close(bodyFd);

    OStringBuffer cmd;
    cmd.append("curl -sS -N --http1.1 --max-time ");
    cmd.append(static_cast<sal_Int32>(kGenerateTimeoutMs / 1000));
    cmd.append(" -X POST -H 'Content-Type: application/json' --data-binary @");
    cmd.append(bodyPath);
    cmd.append(" http://127.0.0.1:11434/api/generate 2>/dev/null");

    FILE* pipe = ::popen(cmd.makeStringAndClear().getStr(), "r");
    if (!pipe)
    {
        ::unlink(bodyPath);
        return OUString();
    }

    OUStringBuffer assembled;
    char lineBuf[8192];
    auto cancelled = [&]() { return rShouldCancel && rShouldCancel(); };
    bool bSaw = false;
    while (::fgets(lineBuf, sizeof(lineBuf), pipe))
    {
        if (cancelled())
            break;
        std::string line(lineBuf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (line.empty())
            continue;
        const OString json(line.data(), static_cast<sal_Int32>(line.size()));
        // Extract "response":"..."
        const sal_Int32 k = json.indexOf("\"response\"");
        if (k < 0)
            continue;
        sal_Int32 i = k + 10;
        while (i < json.getLength()
               && (json[i] == ' ' || json[i] == '\t' || json[i] == ':'))
            ++i;
        if (i >= json.getLength() || json[i] != '"')
            continue;
        ++i;
        OStringBuffer val;
        while (i < json.getLength())
        {
            const char c = json[i];
            if (c == '\\' && i + 1 < json.getLength())
            {
                const char n = json[i + 1];
                if (n == 'n')
                    val.append('\n');
                else if (n == 't')
                    val.append('\t');
                else
                    val.append(n);
                i += 2;
                continue;
            }
            if (c == '"')
                break;
            val.append(c);
            ++i;
        }
        const OUString delta
            = OStringToOUString(val.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
        if (delta.isEmpty())
            continue;
        bSaw = true;
        assembled.append(delta);
        if (rOnChunk && !rOnChunk(delta))
            break;
    }
    ::pclose(pipe);
    ::unlink(bodyPath);
    if (!bSaw)
        return OUString();
    return assembled.makeStringAndClear();
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
