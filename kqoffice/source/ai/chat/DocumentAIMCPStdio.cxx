/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "DocumentAIMCPStdio.hxx"

#include "DocumentAIMCPTools.hxx"

#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace kqoffice::ai::chat
{
namespace
{
OUString jsonEscape(const OUString& s)
{
    OUStringBuffer b;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        switch (c)
        {
            case u'\\':
                b.append(u"\\\\"_ustr);
                break;
            case u'"':
                b.append(u"\\\""_ustr);
                break;
            case u'\n':
                b.append(u"\\n"_ustr);
                break;
            case u'\r':
                b.append(u"\\r"_ustr);
                break;
            case u'\t':
                b.append(u"\\t"_ustr);
                break;
            default:
                if (c < 0x20)
                {
                    // skip other controls
                }
                else
                    b.append(c);
                break;
        }
    }
    return b.makeStringAndClear();
}

OUString extractJsonStringField(const OUString& json, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = json.indexOf(needle);
    if (p < 0)
        return OUString();
    p = json.indexOf(u':', p + needle.getLength());
    if (p < 0)
        return OUString();
    sal_Int32 q1 = json.indexOf(u'"', p + 1);
    if (q1 < 0)
        return OUString();
    sal_Int32 q2 = q1 + 1;
    while (q2 < json.getLength())
    {
        if (json[q2] == u'\\' && q2 + 1 < json.getLength())
        {
            q2 += 2;
            continue;
        }
        if (json[q2] == u'"')
            break;
        ++q2;
    }
    if (q2 >= json.getLength())
        return OUString();
    // Unescape minimal
    OUString raw = json.copy(q1 + 1, q2 - q1 - 1);
    OUStringBuffer out;
    for (sal_Int32 i = 0; i < raw.getLength(); ++i)
    {
        if (raw[i] == u'\\' && i + 1 < raw.getLength())
        {
            const sal_Unicode n = raw[i + 1];
            if (n == u'n')
                out.append(u'\n');
            else if (n == u't')
                out.append(u'\t');
            else if (n == u'"' || n == u'\\')
                out.append(n);
            else
                out.append(n);
            ++i;
            continue;
        }
        out.append(raw[i]);
    }
    return out.makeStringAndClear();
}

bool extractJsonBoolField(const OUString& json, const OUString& key, bool def)
{
    const OUString needle = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = json.indexOf(needle);
    if (p < 0)
        return def;
    p = json.indexOf(u':', p + needle.getLength());
    if (p < 0)
        return def;
    const OUString rest = json.copy(p + 1);
    if (rest.indexOf(u"true"_ustr) >= 0 && rest.indexOf(u"true"_ustr) < 16)
        return true;
    if (rest.indexOf(u"false"_ustr) >= 0 && rest.indexOf(u"false"_ustr) < 16)
        return false;
    return def;
}

/// Extract id field as raw JSON token (number or string or null).
OUString extractIdToken(const OUString& json)
{
    const OUString needle = u"\"id\""_ustr;
    sal_Int32 p = json.indexOf(needle);
    if (p < 0)
        return u"null"_ustr;
    p = json.indexOf(u':', p + needle.getLength());
    if (p < 0)
        return u"null"_ustr;
    sal_Int32 i = p + 1;
    while (i < json.getLength() && (json[i] == u' ' || json[i] == u'\t'))
        ++i;
    if (i >= json.getLength())
        return u"null"_ustr;
    if (json[i] == u'"')
    {
        sal_Int32 q2 = i + 1;
        while (q2 < json.getLength())
        {
            if (json[q2] == u'\\' && q2 + 1 < json.getLength())
            {
                q2 += 2;
                continue;
            }
            if (json[q2] == u'"')
                break;
            ++q2;
        }
        if (q2 < json.getLength())
            return json.copy(i, q2 - i + 1);
        return u"null"_ustr;
    }
    if (json.copy(i, 4) == u"null"_ustr)
        return u"null"_ustr;
    sal_Int32 j = i;
    if (j < json.getLength() && (json[j] == u'-' || (json[j] >= u'0' && json[j] <= u'9')))
    {
        if (json[j] == u'-')
            ++j;
        while (j < json.getLength() && json[j] >= u'0' && json[j] <= u'9')
            ++j;
        return json.copy(i, j - i);
    }
    return u"null"_ustr;
}

OUString extractObjectAfterKey(const OUString& json, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = json.indexOf(needle);
    if (p < 0)
        return OUString();
    p = json.indexOf(u'{', p + needle.getLength());
    if (p < 0)
        return OUString();
    sal_Int32 depth = 0;
    for (sal_Int32 i = p; i < json.getLength(); ++i)
    {
        if (json[i] == u'{')
            ++depth;
        else if (json[i] == u'}')
        {
            --depth;
            if (depth == 0)
                return json.copy(p, i - p + 1);
        }
    }
    return OUString();
}

OUString rpcError(const OUString& idToken, sal_Int32 code, const OUString& message)
{
    OUStringBuffer b;
    b.append(u"{\"jsonrpc\":\"2.0\",\"id\":"_ustr);
    b.append(idToken);
    b.append(u",\"error\":{\"code\":"_ustr);
    b.append(OUString::number(code));
    b.append(u",\"message\":\""_ustr);
    b.append(jsonEscape(message));
    b.append(u"\"}}"_ustr);
    return b.makeStringAndClear();
}

OUString rpcResult(const OUString& idToken, const OUString& resultObject)
{
    OUStringBuffer b;
    b.append(u"{\"jsonrpc\":\"2.0\",\"id\":"_ustr);
    b.append(idToken);
    b.append(u",\"result\":"_ustr);
    b.append(resultObject);
    b.append(u"}"_ustr);
    return b.makeStringAndClear();
}

OUString toolsListResult()
{
    OUStringBuffer b;
    b.append(u"{\"tools\":["_ustr);
    bool first = true;
    for (const auto& d : DocumentAIMCPTools::listTools())
    {
        if (d.name == u"list_tools"_ustr)
            continue; // MCP has tools/list; keep list_tools as callable alias only
        if (!first)
            b.append(u',');
        first = false;
        b.append(u"{\"name\":\""_ustr);
        b.append(jsonEscape(d.name));
        b.append(u"\",\"description\":\""_ustr);
        b.append(jsonEscape(d.descriptionZh));
        b.append(u"\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"_ustr);
        if (d.name == u"read_blocks"_ustr)
            b.append(u"\"start\":{\"type\":\"integer\"},\"end\":{\"type\":\"integer\"}"_ustr);
        else if (d.name == u"read_skeleton"_ustr)
            b.append(u"\"max_blocks\":{\"type\":\"integer\"}"_ustr);
        else if (d.name == u"formula_dry_run"_ustr || d.name == u"verify_plan"_ustr
                 || d.name == u"apply_preview"_ustr || d.name == u"apply_approved"_ustr)
        {
            b.append(u"\"text\":{\"type\":\"string\"},\"plan\":{\"type\":\"string\"}"_ustr);
            if (d.name == u"apply_approved"_ustr)
                b.append(u",\"humanApproval\":{\"type\":\"boolean\"}"_ustr);
        }
        else if (d.name == u"connector_invoke"_ustr)
        {
            b.append(
                u"\"connectorId\":{\"type\":\"string\"},\"operation\":{\"type\":\"string\"},"
                u"\"query\":{\"type\":\"string\"},\"body\":{\"type\":\"string\"},"
                u"\"httpMethod\":{\"type\":\"string\"},"
                u"\"explicitUserApproval\":{\"type\":\"boolean\"},"
                u"\"humanApproval\":{\"type\":\"boolean\"}"_ustr);
        }
        else if (d.name == u"connector_device_start"_ustr
                 || d.name == u"connector_device_poll"_ustr)
        {
            b.append(u"\"connectorId\":{\"type\":\"string\"},"
                     u"\"explicitUserApproval\":{\"type\":\"boolean\"},"
                     u"\"humanApproval\":{\"type\":\"boolean\"}"_ustr);
        }
        // list_connectors / connector_status / vision_status: empty props
        b.append(u"},\"additionalProperties\":true}"_ustr);
        if (d.requiresHumanApproval)
            b.append(u",\"_kqRequiresHumanApproval\":true"_ustr);
        if (d.mutatesDocument)
            b.append(u",\"_kqMutatesDocument\":true"_ustr);
        b.append(u"}"_ustr);
    }
    b.append(u"]}"_ustr);
    return b.makeStringAndClear();
}

OUString initializeResult()
{
    OUStringBuffer b;
    b.append(u"{"_ustr);
    b.append(u"\"protocolVersion\":\""_ustr);
    b.append(DocumentAIMCPStdio::protocolVersion());
    b.append(u"\","_ustr);
    b.append(u"\"capabilities\":{\"tools\":{\"listChanged\":false}},"_ustr);
    b.append(u"\"serverInfo\":{\"name\":\""_ustr);
    b.append(jsonEscape(DocumentAIMCPStdio::serverName()));
    b.append(u"\",\"version\":\""_ustr);
    b.append(jsonEscape(DocumentAIMCPStdio::serverVersion()));
    b.append(u"\"},"_ustr);
    b.append(u"\"_kqPrinciples\":[\"local-first\",\"approve-before-write\",\"no-silent-upload\"]"_ustr);
    b.append(u"}"_ustr);
    return b.makeStringAndClear();
}

OUString handleToolsCall(const OUString& paramsJson)
{
    OUString name = extractJsonStringField(paramsJson, u"name"_ustr);
    if (name.isEmpty())
        name = extractJsonStringField(paramsJson, u"tool"_ustr);
    const OUString argsObj = extractObjectAfterKey(paramsJson, u"arguments"_ustr);
    const OUString argsFlat = argsObj.isEmpty() ? paramsJson : argsObj;

    MCPToolCall call;
    call.name = name;
    call.arguments = argsFlat;
    call.humanApproval = extractJsonBoolField(argsFlat, u"humanApproval"_ustr, false)
                         || extractJsonBoolField(paramsJson, u"humanApproval"_ustr, false);
    // plan / text body
    OUString plan = extractJsonStringField(argsFlat, u"plan"_ustr);
    if (plan.isEmpty())
        plan = extractJsonStringField(argsFlat, u"text"_ustr);
    if (plan.isEmpty())
        plan = extractJsonStringField(argsFlat, u"content"_ustr);
    if (plan.isEmpty())
        plan = extractJsonStringField(argsFlat, u"planContent"_ustr);
    call.planContent = plan;
    // Also pass raw arguments string for key=value parsers (start/end)
    if (call.arguments.isEmpty())
        call.arguments = plan;

    const MCPToolResult r = DocumentAIMCPTools::dispatch(call);

    OUStringBuffer content;
    content.append(u"{\"success\":"_ustr);
    content.append(r.success ? u"true"_ustr : u"false"_ustr);
    content.append(u",\"tool\":\""_ustr);
    content.append(jsonEscape(r.toolName.isEmpty() ? name : r.toolName));
    content.append(u"\",\"summary\":\""_ustr);
    content.append(jsonEscape(r.summaryZh));
    content.append(u"\",\"content\":\""_ustr);
    content.append(jsonEscape(r.content));
    content.append(u"\",\"error\":\""_ustr);
    content.append(jsonEscape(r.error));
    content.append(u"\",\"mainDocumentMutation\":"_ustr);
    content.append(r.mainDocumentMutation ? u"true"_ustr : u"false"_ustr);
    content.append(u"}"_ustr);

    // MCP tools/call result shape: content array of text parts
    OUStringBuffer result;
    result.append(u"{\"content\":[{\"type\":\"text\",\"text\":\""_ustr);
    result.append(jsonEscape(content.makeStringAndClear()));
    result.append(u"\"}],\"isError\":"_ustr);
    result.append(r.success ? u"false"_ustr : u"true"_ustr);
    result.append(u"}"_ustr);
    return result.makeStringAndClear();
}

void writeFramed(const OUString& body)
{
    const OString utf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);
    std::printf("Content-Length: %d\r\n\r\n", static_cast<int>(utf8.getLength()));
    std::fwrite(utf8.getStr(), 1, static_cast<size_t>(utf8.getLength()), stdout);
    std::fflush(stdout);
}

void writeLine(const OUString& body)
{
    const OString utf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);
    std::fwrite(utf8.getStr(), 1, static_cast<size_t>(utf8.getLength()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}
} // namespace

OUString DocumentAIMCPStdio::protocolVersion() { return u"2024-11-05"_ustr; }

OUString DocumentAIMCPStdio::serverName() { return u"kqoffice-mcp"_ustr; }

OUString DocumentAIMCPStdio::serverVersion() { return u"0.1.0"_ustr; }

OUString DocumentAIMCPStdio::handleRequestJson(const OUString& rRequestJson)
{
    const OUString json = rRequestJson.trim();
    if (json.isEmpty())
        return rpcError(u"null"_ustr, -32700, u"Parse error: empty"_ustr);

    const OUString idToken = extractIdToken(json);
    const OUString method = extractJsonStringField(json, u"method"_ustr);
    if (method.isEmpty())
        return rpcError(idToken, -32600, u"Invalid Request: missing method"_ustr);

    // Notifications (no response required for some hosts; we still no-op with empty)
    if (method.startsWith(u"notifications/"_ustr))
    {
        // Return empty string → caller may skip write
        return OUString();
    }

    if (method == u"initialize"_ustr)
        return rpcResult(idToken, initializeResult());
    if (method == u"ping"_ustr)
        return rpcResult(idToken, u"{}"_ustr);
    if (method == u"tools/list"_ustr || method == u"list_tools"_ustr)
        return rpcResult(idToken, toolsListResult());
    if (method == u"tools/call"_ustr || method == u"call_tool"_ustr)
    {
        const OUString params = extractObjectAfterKey(json, u"params"_ustr);
        return rpcResult(idToken, handleToolsCall(params.isEmpty() ? json : params));
    }

    return rpcError(idToken, -32601, u"Method not found: "_ustr + method);
}

int DocumentAIMCPStdio::runStdioLoop()
{
    // Dual mode: Content-Length framing or NDJSON.
    std::string line;
    bool framedMode = false;
    while (true)
    {
        if (!framedMode)
        {
            if (!std::getline(std::cin, line))
                break;
            // Strip CR
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (line.rfind("Content-Length:", 0) == 0
                || line.rfind("content-length:", 0) == 0)
            {
                framedMode = true;
                // fall into framed parse with current header line
            }
            else
            {
                const OUString req = OUString::fromUtf8(std::string_view(line));
                const OUString resp = handleRequestJson(req);
                if (!resp.isEmpty())
                    writeLine(resp);
                continue;
            }
        }

        // Framed mode: read headers until blank line, then body
        int contentLength = -1;
        std::string header = line;
        for (;;)
        {
            if (header.rfind("Content-Length:", 0) == 0
                || header.rfind("content-length:", 0) == 0)
            {
                const auto colon = header.find(':');
                if (colon != std::string::npos)
                    contentLength = std::atoi(header.c_str() + colon + 1);
            }
            if (!std::getline(std::cin, header))
                return 0;
            if (!header.empty() && header.back() == '\r')
                header.pop_back();
            if (header.empty())
                break;
        }
        if (contentLength < 0 || contentLength > 16 * 1024 * 1024)
            return 1;
        std::string body(static_cast<size_t>(contentLength), '\0');
        std::cin.read(body.data(), contentLength);
        if (std::cin.gcount() != contentLength)
            return 1;
        const OUString req = OUString::fromUtf8(std::string_view(body));
        const OUString resp = handleRequestJson(req);
        if (!resp.isEmpty())
            writeFramed(resp);
        line.clear();
        // stay in framed mode
    }
    return 0;
}

} // namespace kqoffice::ai::chat

extern "C" char* kqoffice_mcp_handle_json(const char* requestUtf8)
{
    if (!requestUtf8)
        return nullptr;
    const OUString req = OUString::fromUtf8(requestUtf8);
    const OUString resp = kqoffice::ai::chat::DocumentAIMCPStdio::handleRequestJson(req);
    const OString utf8 = OUStringToOString(resp, RTL_TEXTENCODING_UTF8);
    char* out = static_cast<char*>(std::malloc(static_cast<size_t>(utf8.getLength()) + 1));
    if (!out)
        return nullptr;
    std::memcpy(out, utf8.getStr(), static_cast<size_t>(utf8.getLength()) + 1);
    return out;
}

extern "C" void kqoffice_mcp_free(char* p) { std::free(p); }

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
