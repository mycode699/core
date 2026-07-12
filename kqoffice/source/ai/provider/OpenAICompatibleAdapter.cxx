/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "OpenAICompatibleAdapter.hxx"

#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

namespace kqoffice::ai
{
namespace
{
constexpr int kProbeTimeoutMs = 100;
constexpr int kGenerateTimeoutMs = 30 * 1000;
constexpr std::size_t kMaxTagsResponseBytes = 32 * 1024;
constexpr std::size_t kMaxGenerateResponseBytes = 256 * 1024;

void appendJsonEscaped(OStringBuffer& out, const OString& s)
{
    const sal_Int32 n = s.getLength();
    const char* p = s.getStr();
    for (sal_Int32 i = 0; i < n; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        switch (c)
        {
            case '"':
                out.append("\\\"");
                break;
            case '\\':
                out.append("\\\\");
                break;
            case '\b':
                out.append("\\b");
                break;
            case '\f':
                out.append("\\f");
                break;
            case '\n':
                out.append("\\n");
                break;
            case '\r':
                out.append("\\r");
                break;
            case '\t':
                out.append("\\t");
                break;
            default:
                if (c < 0x20)
                {
                    char hex[8];
                    ::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned>(c));
                    out.append(hex);
                }
                else
                    out.append(static_cast<char>(c));
                break;
        }
    }
}

bool sendAll(int fd, const char* buf, std::size_t len)
{
    std::size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = ::send(fd, buf + sent, len - sent, 0);
        if (n <= 0)
            return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

std::string readAllBounded(int fd, std::size_t maxBytes)
{
    std::string out;
    out.reserve(std::min<std::size_t>(maxBytes, std::size_t{2048}));
    char buf[4096];
    while (out.size() < maxBytes)
    {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0)
            break;
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return std::string();
        }
        out.append(buf, static_cast<std::size_t>(n));
    }
    return out;
}

int openConnection(const OString& hostUtf8, int port, int timeoutMs)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    // Prefer numeric host; fall back to gethostbyname for LAN hostnames.
    if (::inet_pton(AF_INET, hostUtf8.getStr(), &addr.sin_addr) != 1)
    {
        hostent* he = ::gethostbyname(hostUtf8.getStr());
        if (!he || he->h_addrtype != AF_INET || !he->h_addr_list || !he->h_addr_list[0])
        {
            ::close(fd);
            return -1;
        }
        std::memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(in_addr));
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

OUString extractJsonStringAfterKey(const OString& body, const char* key)
{
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();
    OStringBuffer needleBuf;
    needleBuf.append('"');
    needleBuf.append(key);
    needleBuf.append('"');
    const OString needle = needleBuf.makeStringAndClear();
    sal_Int32 i = 0;
    while (i < n)
    {
        sal_Int32 found = body.indexOf(needle, i);
        if (found < 0)
            return OUString();
        i = found + needle.getLength();
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n || s[i] != ':')
            continue;
        ++i;
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n || s[i] != '"')
            continue;
        ++i;
        OStringBuffer val;
        while (i < n)
        {
            const char c = s[i];
            if (c == '\\' && i + 1 < n)
            {
                const char esc = s[i + 1];
                switch (esc)
                {
                    case '"':
                        val.append('"');
                        break;
                    case '\\':
                        val.append('\\');
                        break;
                    case '/':
                        val.append('/');
                        break;
                    case 'n':
                        val.append('\n');
                        break;
                    case 't':
                        val.append('\t');
                        break;
                    case 'r':
                        val.append('\r');
                        break;
                    case 'b':
                        val.append('\b');
                        break;
                    case 'f':
                        val.append('\f');
                        break;
                    default:
                        val.append(esc);
                        break;
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
            return OUString();
        return OStringToOUString(val.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    }
    return OUString();
}

bool is2xx(const std::string& raw)
{
    if (raw.size() < 12 || raw.compare(0, 5, "HTTP/") != 0)
        return true; // let parser decide
    const std::size_t spc = raw.find(' ');
    if (spc == std::string::npos || spc + 1 >= raw.size())
        return false;
    return raw[spc + 1] == '2';
}

OString bodyAfterHeaders(const std::string& raw)
{
    const std::string sep = "\r\n\r\n";
    auto pos = raw.find(sep);
    if (pos == std::string::npos)
        return OString();
    return OString(raw.data() + pos + sep.size(),
                   static_cast<sal_Int32>(raw.size() - pos - sep.size()));
}
} // namespace

OpenAICompatibleAdapter::Endpoint
OpenAICompatibleAdapter::parseBaseUrl(const OUString& rBaseUrl)
{
    Endpoint ep;
    OUString u = rBaseUrl.trim();
    if (u.isEmpty())
        return ep;
    // HTTPS not supported in this socket-only path.
    if (u.startsWithIgnoreAsciiCase(u"https://"_ustr))
        return ep;
    if (u.startsWithIgnoreAsciiCase(u"http://"_ustr))
        u = u.copy(7);
    // strip path for host:port
    sal_Int32 slash = u.indexOf('/');
    OUString hostPort = slash >= 0 ? u.copy(0, slash) : u;
    OUString path = slash >= 0 ? u.copy(slash) : OUString();
    // drop trailing /v1 from path — we always hit /v1/...
    if (path.endsWith(u"/v1"_ustr) || path.endsWith(u"/v1/"_ustr))
    {
        // path prefix unused; keep empty
        path = OUString();
    }
    sal_Int32 colon = hostPort.indexOf(':');
    if (colon < 0)
    {
        ep.host = hostPort;
        ep.port = 80;
    }
    else
    {
        ep.host = hostPort.copy(0, colon);
        OUString portStr = hostPort.copy(colon + 1);
        ep.port = portStr.toInt32();
        if (ep.port <= 0 || ep.port > 65535)
            ep.port = 80;
    }
    if (ep.host.isEmpty())
        return ep;
    ep.pathPrefix = path; // unused for now
    ep.valid = true;
    return ep;
}

bool OpenAICompatibleAdapter::isOpenAICompatibleBackend(const OUString& rBackend)
{
    const OUString b = rBackend.toAsciiLowerCase().trim();
    return b == u"openai"_ustr || b == u"openai-compatible"_ustr || b == u"openai-compat"_ustr
           || b == u"openai_compatible"_ustr || b == u"oai"_ustr;
}

OUString OpenAICompatibleAdapter::apiKeyFromEnv()
{
    if (const char* k = std::getenv("KQOFFICE_AI_API_KEY"))
        return OUString::createFromAscii(k);
    return OUString();
}

OpenAICompatibleAdapter::OpenAICompatibleAdapter(const OUString& rBaseUrl,
                                                 const OUString& rApiKey)
    : m_ep(parseBaseUrl(rBaseUrl))
    , m_apiKey(rApiKey)
{
}

OUString OpenAICompatibleAdapter::probe()
{
    if (!m_ep.valid)
        return u"unreachable"_ustr;
    const OString host = OUStringToOString(m_ep.host, RTL_TEXTENCODING_UTF8);
    int fd = openConnection(host, m_ep.port, kProbeTimeoutMs);
    if (fd < 0)
        return u"unreachable"_ustr;
    ::close(fd);
    return u"reachable"_ustr;
}

std::vector<OUString> OpenAICompatibleAdapter::listModels()
{
    if (!m_ep.valid)
        return {};
    const OString host = OUStringToOString(m_ep.host, RTL_TEXTENCODING_UTF8);
    int fd = openConnection(host, m_ep.port, kProbeTimeoutMs);
    if (fd < 0)
        return {};

    OStringBuffer req;
    req.append("GET /v1/models HTTP/1.0\r\nHost: ");
    req.append(host);
    req.append("\r\nAccept: application/json\r\n");
    if (!m_apiKey.isEmpty())
    {
        req.append("Authorization: Bearer ");
        req.append(OUStringToOString(m_apiKey, RTL_TEXTENCODING_UTF8));
        req.append("\r\n");
    }
    req.append("\r\n");
    const OString reqStr = req.makeStringAndClear();
    if (!sendAll(fd, reqStr.getStr(), static_cast<std::size_t>(reqStr.getLength())))
    {
        ::close(fd);
        return {};
    }
    std::string raw = readAllBounded(fd, kMaxTagsResponseBytes);
    ::close(fd);
    if (raw.empty() || !is2xx(raw))
        return {};
    return parseModelsJson(bodyAfterHeaders(raw));
}

OString OpenAICompatibleAdapter::buildChatRequestJson(const OUString& model,
                                                      const OUString& prompt)
{
    OString modelUtf8 = OUStringToOString(model, RTL_TEXTENCODING_UTF8);
    OString promptUtf8 = OUStringToOString(prompt, RTL_TEXTENCODING_UTF8);
    OStringBuffer body(256 + promptUtf8.getLength());
    body.append("{\"model\":\"");
    appendJsonEscaped(body, modelUtf8);
    body.append("\",\"messages\":[{\"role\":\"user\",\"content\":\"");
    appendJsonEscaped(body, promptUtf8);
    body.append("\"}],\"stream\":false,\"temperature\":0}");
    return body.makeStringAndClear();
}

OUString OpenAICompatibleAdapter::chat(const OUString& model, const OUString& prompt)
{
    if (!m_ep.valid || model.isEmpty())
        return OUString();
    OString jsonBody = buildChatRequestJson(model, prompt);
    const OString host = OUStringToOString(m_ep.host, RTL_TEXTENCODING_UTF8);
    int fd = openConnection(host, m_ep.port, kGenerateTimeoutMs);
    if (fd < 0)
        return OUString();

    OStringBuffer hdr(256);
    hdr.append("POST /v1/chat/completions HTTP/1.0\r\nHost: ");
    hdr.append(host);
    hdr.append("\r\nContent-Type: application/json\r\nAccept: application/json\r\n");
    if (!m_apiKey.isEmpty())
    {
        hdr.append("Authorization: Bearer ");
        hdr.append(OUStringToOString(m_apiKey, RTL_TEXTENCODING_UTF8));
        hdr.append("\r\n");
    }
    hdr.append("Content-Length: ");
    hdr.append(static_cast<sal_Int32>(jsonBody.getLength()));
    hdr.append("\r\n\r\n");
    OString hdrStr = hdr.makeStringAndClear();

    if (!sendAll(fd, hdrStr.getStr(), static_cast<std::size_t>(hdrStr.getLength()))
        || !sendAll(fd, jsonBody.getStr(), static_cast<std::size_t>(jsonBody.getLength())))
    {
        ::close(fd);
        return OUString();
    }
    std::string raw = readAllBounded(fd, kMaxGenerateResponseBytes);
    ::close(fd);
    if (raw.empty() || !is2xx(raw))
        return OUString();
    return parseChatCompletionJson(bodyAfterHeaders(raw));
}

OUString OpenAICompatibleAdapter::parseChatCompletionJson(const OString& body)
{
    // Prefer message.content (chat.completions). Fall back to "content" then "text".
    OUString content = extractJsonStringAfterKey(body, "content");
    if (!content.isEmpty())
        return content;
    return extractJsonStringAfterKey(body, "text");
}

std::vector<OUString> OpenAICompatibleAdapter::parseModelsJson(const OString& body)
{
    // Collect "id" fields under data[] — linear scan same as Ollama names.
    std::vector<OUString> out;
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();
    sal_Int32 i = 0;
    while (i < n)
    {
        sal_Int32 found = body.indexOf("\"id\"", i);
        if (found < 0)
            break;
        i = found + 4;
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n || s[i] != ':')
            continue;
        ++i;
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
            ++i;
        if (i >= n || s[i] != '"')
            continue;
        ++i;
        OStringBuffer val;
        while (i < n)
        {
            const char c = s[i];
            if (c == '\\' && i + 1 < n)
            {
                val.append(s[i + 1]);
                i += 2;
                continue;
            }
            if (c == '"')
                break;
            val.append(c);
            ++i;
        }
        if (i >= n)
            break;
        const OUString id
            = OStringToOUString(val.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
        if (!id.isEmpty())
            out.push_back(id);
        ++i;
    }
    return out;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
