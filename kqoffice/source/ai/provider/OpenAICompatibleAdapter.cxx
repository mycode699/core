/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "OpenAICompatibleAdapter.hxx"
#include "KqNetSocket.hxx"

#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace kqoffice::ai
{
namespace
{
constexpr int kProbeTimeoutMs = 3000; // remote gateways need more than localhost
constexpr int kGenerateTimeoutMs = 120 * 1000;
constexpr std::size_t kMaxTagsResponseBytes = 32 * 1024;
constexpr std::size_t kMaxGenerateResponseBytes = 512 * 1024;

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

bool sendAll(KqSock fd, const char* buf, std::size_t len)
{
    return kqNetSendAll(fd, buf, len);
}

std::string readAllBounded(KqSock fd, std::size_t maxBytes)
{
    return kqNetRecvBounded(fd, maxBytes);
}

KqSock openConnection(const OString& hostUtf8, int port, int timeoutMs)
{
    return kqNetConnect(hostUtf8.getStr(), port, timeoutMs);
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
    if (u.startsWithIgnoreAsciiCase(u"https://"_ustr))
    {
        ep.useTls = true;
        u = u.copy(8);
    }
    else if (u.startsWithIgnoreAsciiCase(u"http://"_ustr))
    {
        ep.useTls = false;
        u = u.copy(7);
    }
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
        ep.port = ep.useTls ? 443 : 80;
    }
    else
    {
        ep.host = hostPort.copy(0, colon);
        OUString portStr = hostPort.copy(colon + 1);
        ep.port = portStr.toInt32();
        if (ep.port <= 0 || ep.port > 65535)
            ep.port = ep.useTls ? 443 : 80;
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
    {
        // Trim whitespace/newlines from env.
        OUString s = OUString::createFromAscii(k).trim();
        if (!s.isEmpty())
            return s;
    }
    // Fallback: local key file (mac ~/.config · Win %APPDATA%\kqoffice).
    auto tryKeyFile = [](const std::string& path) -> OUString {
        std::ifstream in(path);
        if (!in)
            return {};
        std::string line;
        if (!std::getline(in, line))
            return {};
        while (!line.empty()
               && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '
                   || line.back() == '\t'))
            line.pop_back();
        if (line.empty())
            return {};
        return OStringToOUString(OString(line.data(), static_cast<sal_Int32>(line.size())),
                                 RTL_TEXTENCODING_UTF8);
    };
#if defined(_WIN32)
    if (const char* app = std::getenv("APPDATA"); app && *app)
    {
        const OUString k = tryKeyFile(std::string(app) + "/kqoffice/api-key");
        if (!k.isEmpty())
            return k;
    }
    if (const char* up = std::getenv("USERPROFILE"); up && *up)
    {
        const OUString k = tryKeyFile(std::string(up) + "/.config/kqoffice/api-key");
        if (!k.isEmpty())
            return k;
    }
#endif
    if (const char* home = std::getenv("HOME"); home && *home)
    {
        const OUString k = tryKeyFile(std::string(home) + "/.config/kqoffice/api-key");
        if (!k.isEmpty())
            return k;
    }
    return OUString();
}

OpenAICompatibleAdapter::OpenAICompatibleAdapter(const OUString& rBaseUrl,
                                                 const OUString& rApiKey)
    : m_ep(parseBaseUrl(rBaseUrl))
    , m_apiKey(rApiKey.isEmpty() ? apiKeyFromEnv() : rApiKey)
{
}

OUString OpenAICompatibleAdapter::absoluteUrl(const char* path) const
{
    OUStringBuffer b;
    b.append(m_ep.useTls ? u"https://"_ustr : u"http://"_ustr);
    b.append(m_ep.host);
    if (!(m_ep.useTls && m_ep.port == 443) && !( !m_ep.useTls && m_ep.port == 80))
    {
        b.append(u':');
        b.append(static_cast<sal_Int32>(m_ep.port));
    }
    b.append(OUString::createFromAscii(path));
    return b.makeStringAndClear();
}

OString OpenAICompatibleAdapter::httpsRequest(const char* method, const char* path,
                                              const OString& rJsonBody, int timeoutSec) const
{
    if (!m_ep.valid || !path)
        return OString();

    // Body / response temp files under /tmp (no secrets on argv when using header file).
    char bodyPath[] = "/tmp/kqoffice-ai-body-XXXXXX";
    char outPath[] = "/tmp/kqoffice-ai-out-XXXXXX";
    char hdrPath[] = "/tmp/kqoffice-ai-hdr-XXXXXX";
    const int bodyFd = ::mkstemp(bodyPath);
    const int outFd = ::mkstemp(outPath);
    const int hdrFd = ::mkstemp(hdrPath);
    if (bodyFd < 0 || outFd < 0 || hdrFd < 0)
    {
        if (bodyFd >= 0)
            ::close(bodyFd);
        if (outFd >= 0)
            ::close(outFd);
        if (hdrFd >= 0)
            ::close(hdrFd);
        return OString();
    }
    if (!rJsonBody.isEmpty())
    {
        const ssize_t w
            = ::write(bodyFd, rJsonBody.getStr(), static_cast<size_t>(rJsonBody.getLength()));
        (void)w;
    }
    ::close(bodyFd);
    ::close(outFd);

    {
        OStringBuffer hb;
        if (!m_apiKey.isEmpty())
        {
            hb.append("Authorization: Bearer ");
            hb.append(OUStringToOString(m_apiKey, RTL_TEXTENCODING_UTF8));
            hb.append("\r\n");
        }
        hb.append("Accept: application/json\r\n");
        if (rJsonBody.getLength() > 0)
            hb.append("Content-Type: application/json\r\n");
        const OString hdr = hb.makeStringAndClear();
        const ssize_t w = ::write(hdrFd, hdr.getStr(), static_cast<size_t>(hdr.getLength()));
        (void)w;
        ::close(hdrFd);
    }

    const OUString url = absoluteUrl(path);
    const OString urlUtf8 = OUStringToOString(url, RTL_TEXTENCODING_UTF8);
    // curl -sS -X METHOD -D - --max-time N -H @hdr --data-binary @body -o out URL
    // Use -D - to include response headers on stdout; we still write body to outPath.
    OStringBuffer cmd;
    cmd.append("curl -sS --http1.1 --max-time ");
    cmd.append(static_cast<sal_Int32>(timeoutSec > 0 ? timeoutSec : 120));
    cmd.append(" -X ");
    cmd.append(method);
    cmd.append(" -H @");
    cmd.append(hdrPath);
    if (rJsonBody.getLength() > 0)
    {
        cmd.append(" --data-binary @");
        cmd.append(bodyPath);
    }
    cmd.append(" -o ");
    cmd.append(outPath);
    cmd.append(" -w '\\nHTTP_CODE=%{http_code}\\n' ");
    // Quote URL for shell.
    cmd.append('\'');
    cmd.append(urlUtf8);
    cmd.append('\'');
    cmd.append(" 2>/dev/null");

    const OString cmdStr = cmd.makeStringAndClear();
    FILE* pipe = ::popen(cmdStr.getStr(), "r");
    int httpCode = 0;
    if (pipe)
    {
        char line[256];
        while (::fgets(line, sizeof(line), pipe))
        {
            if (std::strncmp(line, "HTTP_CODE=", 10) == 0)
                httpCode = std::atoi(line + 10);
        }
        ::pclose(pipe);
    }
    OString body;
    {
        std::ifstream in(outPath, std::ios::binary);
        if (in)
        {
            std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (s.size() > kMaxGenerateResponseBytes)
                s.resize(kMaxGenerateResponseBytes);
            body = OString(s.data(), static_cast<sal_Int32>(s.size()));
        }
    }
    ::unlink(bodyPath);
    ::unlink(outPath);
    ::unlink(hdrPath);
    m_lastHttpCode = httpCode;
    // Always return body when present so chat() can parse error JSON on 401/403.
    return body;
}

OUString OpenAICompatibleAdapter::probe()
{
    if (!m_ep.valid)
        return u"unreachable"_ustr;
    const OString host = OUStringToOString(m_ep.host, RTL_TEXTENCODING_UTF8);
    KqSock fd = openConnection(host, m_ep.port, kProbeTimeoutMs);
    if (fd < 0)
        return u"unreachable"_ustr;
    kqNetClose(fd);
    return u"reachable"_ustr;
}

std::vector<OUString> OpenAICompatibleAdapter::listModels()
{
    if (!m_ep.valid)
        return {};

    if (m_ep.useTls)
    {
        // Non-2xx (e.g. 401) still means gateway is up; primaryModel (e.g. "auto") remains usable.
        const OString body = httpsRequest("GET", "/v1/models", OString(), 15);
        if (body.isEmpty())
            return {};
        return parseModelsJson(body);
    }

    const OString host = OUStringToOString(m_ep.host, RTL_TEXTENCODING_UTF8);
    KqSock fd = openConnection(host, m_ep.port, kProbeTimeoutMs);
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
        kqNetClose(fd);
        return {};
    }
    std::string raw = readAllBounded(fd, kMaxTagsResponseBytes);
    kqNetClose(fd);
    if (raw.empty() || !is2xx(raw))
        return {};
    return parseModelsJson(bodyAfterHeaders(raw));
}

OString OpenAICompatibleAdapter::buildChatRequestJson(const OUString& model,
                                                      const OUString& prompt, bool bStream)
{
    OString modelUtf8 = OUStringToOString(model, RTL_TEXTENCODING_UTF8);
    OString promptUtf8 = OUStringToOString(prompt, RTL_TEXTENCODING_UTF8);
    OStringBuffer body(256 + promptUtf8.getLength());
    body.append("{\"model\":\"");
    appendJsonEscaped(body, modelUtf8);
    body.append("\",\"messages\":[{\"role\":\"user\",\"content\":\"");
    appendJsonEscaped(body, promptUtf8);
    body.append("\"}],\"stream\":");
    body.append(bStream ? "true" : "false");
    body.append(",\"temperature\":0}");
    return body.makeStringAndClear();
}

namespace
{
/// Extract a short string field from OpenAI/membership JSON without full parse.
OUString jsonFieldSnippet(const OString& body, const char* key)
{
    OStringBuffer nb;
    nb.append('"');
    nb.append(key);
    nb.append('"');
    const OString needle = nb.makeStringAndClear();
    const sal_Int32 k = body.indexOf(needle);
    if (k < 0)
        return OUString();
    // skip to value string after colon
    const sal_Int32 colon = body.indexOf(':', k + needle.getLength());
    if (colon < 0)
        return OUString();
    const sal_Int32 q1 = body.indexOf('"', colon + 1);
    if (q1 < 0)
        return OUString();
    const sal_Int32 q2 = body.indexOf('"', q1 + 1);
    if (q2 <= q1)
        return OUString();
    return OStringToOUString(body.copy(q1 + 1, q2 - q1 - 1), RTL_TEXTENCODING_UTF8);
}

OUString AuthErrorZhFromHttp(int httpCode, const OString& body)
{
    // Prefer membership messageZh, then message / error.message (never include secrets).
    OUString msgZh = jsonFieldSnippet(body, "messageZh");
    OUString msg = msgZh;
    if (msg.isEmpty())
        msg = jsonFieldSnippet(body, "message");
    if (msg.isEmpty())
        msg = OpenAICompatibleAdapter::parseChatCompletionJson(body);
    const OUString code = jsonFieldSnippet(body, "code");
    const bool isQuota = httpCode == 402 || httpCode == 429
                         || code == u"quota_exceeded"_ustr
                         || body.indexOf("quota_exceeded") >= 0
                         || body.indexOf("Fast daily free quota") >= 0
                         || body.indexOf("quota used") >= 0;

    if (isQuota)
    {
        OUString detail = msg.isEmpty()
                              ? u"今日免费额度已用完"_ustr
                              : msg;
        // Product guidance for 可圈 membership (api.03122.com)
        return u"会员额度不足："_ustr + detail
               + u"。可：① 网页账户签到领取加油包；② 邀请好友；"
                 u"③ 升级套餐 https://www.03122.com/zh-CN/pricing.html ；"
                 u"④ 明日额度刷新后再试。"_ustr;
    }
    if (httpCode == 401 || httpCode == 403 || code == u"sign_in_required"_ustr)
    {
        OUString detail = msg.isEmpty() ? OUString() : (u"："_ustr + msg);
        return u"会员未登录或会话失效（HTTP "_ustr
               + OUString::number(static_cast<sal_Int32>(httpCode)) + u"）"_ustr + detail
               + u"。请运行 bin/kqoffice-cloud-login.sh 重新 Device 登录，"
                 u"或更新 ~/.config/kqoffice/api-key（须为会员 sessionToken，不是上游 sk-）。"_ustr;
    }
    if (httpCode == 0)
        return u"会员网关无响应（超时或网络失败）。请检查 https://api.03122.com 与网络。"_ustr;
    if (httpCode < 200 || httpCode >= 300)
    {
        OUString detail = msg.isEmpty() ? OUString() : (u"： "_ustr + msg);
        return u"会员网关错误 HTTP "_ustr
               + OUString::number(static_cast<sal_Int32>(httpCode)) + detail
               + u"。请检查模型名与 api.03122.com 状态。"_ustr;
    }
    return OUString();
}
} // namespace

OUString OpenAICompatibleAdapter::chat(const OUString& model, const OUString& prompt)
{
    m_lastHttpCode = 0;
    m_lastErrorZh.clear();
    if (!m_ep.valid || model.isEmpty())
    {
        m_lastErrorZh = u"网关配置无效或模型名为空"_ustr;
        return OUString();
    }
    if (m_apiKey.isEmpty() && m_ep.useTls)
    {
        m_lastErrorZh
            = u"未找到 API Key。请写入 ~/.config/kqoffice/api-key 或 export KQOFFICE_AI_API_KEY"_ustr;
        return OUString();
    }
    OString jsonBody = buildChatRequestJson(model, prompt);

    if (m_ep.useTls)
    {
        const OString body
            = httpsRequest("POST", "/v1/chat/completions", jsonBody, kGenerateTimeoutMs / 1000);
        if (m_lastHttpCode < 200 || m_lastHttpCode >= 300 || body.isEmpty())
        {
            m_lastErrorZh = AuthErrorZhFromHttp(m_lastHttpCode, body);
            if (m_lastErrorZh.isEmpty())
                m_lastErrorZh = u"openai-compatible 调用失败（空响应）"_ustr;
            return OUString();
        }
        OUString text = parseChatCompletionJson(body);
        if (text.isEmpty())
            m_lastErrorZh = u"网关返回了空内容（模型="_ustr + model + u"）"_ustr;
        return text;
    }

    const OString host = OUStringToOString(m_ep.host, RTL_TEXTENCODING_UTF8);
    KqSock fd = openConnection(host, m_ep.port, kGenerateTimeoutMs);
    if (fd < 0)
    {
        m_lastErrorZh = u"无法连接网关 "_ustr + m_ep.host + u":"_ustr
                        + OUString::number(static_cast<sal_Int32>(m_ep.port));
        return OUString();
    }

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
        kqNetClose(fd);
        m_lastErrorZh = u"向网关发送请求失败"_ustr;
        return OUString();
    }
    std::string raw = readAllBounded(fd, kMaxGenerateResponseBytes);
    kqNetClose(fd);
    if (raw.empty())
    {
        m_lastErrorZh = u"网关返回空响应"_ustr;
        return OUString();
    }
    // Parse status from HTTP/1.x line
    if (raw.size() >= 12 && raw.compare(0, 5, "HTTP/") == 0)
    {
        const auto sp = raw.find(' ');
        if (sp != std::string::npos)
            m_lastHttpCode = std::atoi(raw.c_str() + sp + 1);
    }
    const OString payload = bodyAfterHeaders(raw);
    if (!is2xx(raw))
    {
        m_lastErrorZh = AuthErrorZhFromHttp(m_lastHttpCode, payload);
        if (m_lastErrorZh.isEmpty())
            m_lastErrorZh = u"网关非 2xx 响应"_ustr;
        return OUString();
    }
    return parseChatCompletionJson(payload);
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

OUString OpenAICompatibleAdapter::chatStream(const OUString& model, const OUString& prompt,
                                             const StreamChunkFn& rOnChunk,
                                             const StreamCancelFn& rShouldCancel)
{
    m_lastHttpCode = 0;
    m_lastErrorZh.clear();
    if (!m_ep.valid || model.isEmpty())
    {
        m_lastErrorZh = u"网关配置无效或模型名为空"_ustr;
        return OUString();
    }
    if (m_apiKey.isEmpty() && m_ep.useTls)
    {
        m_lastErrorZh
            = u"未找到 API Key。请写入 ~/.config/kqoffice/api-key 或 export KQOFFICE_AI_API_KEY"_ustr;
        return OUString();
    }

    // Prefer curl -N line stream for HTTPS (and HTTP too for one code path).
    char bodyPath[] = "/tmp/kqoffice-ai-sbody-XXXXXX";
    char hdrPath[] = "/tmp/kqoffice-ai-shdr-XXXXXX";
    const int bodyFd = ::mkstemp(bodyPath);
    const int hdrFd = ::mkstemp(hdrPath);
    if (bodyFd < 0 || hdrFd < 0)
    {
        if (bodyFd >= 0)
            ::close(bodyFd);
        if (hdrFd >= 0)
            ::close(hdrFd);
        m_lastErrorZh = u"无法创建临时文件用于流式请求"_ustr;
        return OUString();
    }
    const OString jsonBody = buildChatRequestJson(model, prompt, /*bStream*/ true);
    (void)::write(bodyFd, jsonBody.getStr(), static_cast<size_t>(jsonBody.getLength()));
    ::close(bodyFd);
    {
        OStringBuffer hb;
        if (!m_apiKey.isEmpty())
        {
            hb.append("Authorization: Bearer ");
            hb.append(OUStringToOString(m_apiKey, RTL_TEXTENCODING_UTF8));
            hb.append("\r\n");
        }
        hb.append("Content-Type: application/json\r\nAccept: text/event-stream\r\n");
        const OString hdr = hb.makeStringAndClear();
        (void)::write(hdrFd, hdr.getStr(), static_cast<size_t>(hdr.getLength()));
        ::close(hdrFd);
    }

    const OUString url = absoluteUrl("/v1/chat/completions");
    const OString urlUtf8 = OUStringToOString(url, RTL_TEXTENCODING_UTF8);
    OStringBuffer cmd;
    cmd.append("curl -sS -N --http1.1 --max-time ");
    cmd.append(static_cast<sal_Int32>(kGenerateTimeoutMs / 1000));
    cmd.append(" -X POST -H @");
    cmd.append(hdrPath);
    cmd.append(" --data-binary @");
    cmd.append(bodyPath);
    cmd.append(" -w '\\nHTTP_CODE=%{http_code}\\n' '");
    cmd.append(urlUtf8);
    cmd.append("' 2>/dev/null");

    FILE* pipe = ::popen(cmd.makeStringAndClear().getStr(), "r");
    if (!pipe)
    {
        ::unlink(bodyPath);
        ::unlink(hdrPath);
        m_lastErrorZh = u"无法启动 curl 流式请求"_ustr;
        return OUString();
    }

    OUStringBuffer assembled;
    char lineBuf[8192];
    int httpCode = 0;
    bool bSawData = false;
    auto cancelled = [&]() { return rShouldCancel && rShouldCancel(); };

    while (::fgets(lineBuf, sizeof(lineBuf), pipe))
    {
        if (cancelled())
            break;
        if (std::strncmp(lineBuf, "HTTP_CODE=", 10) == 0)
        {
            httpCode = std::atoi(lineBuf + 10);
            continue;
        }
        // SSE: "data: {...}" or "data: [DONE]"
        const char* p = lineBuf;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (std::strncmp(p, "data:", 5) != 0)
            continue;
        p += 5;
        while (*p == ' ')
            ++p;
        // strip trailing CR/LF
        std::string payload(p);
        while (!payload.empty()
               && (payload.back() == '\n' || payload.back() == '\r' || payload.back() == ' '))
            payload.pop_back();
        if (payload == "[DONE]" || payload.empty())
            continue;

        // Extract delta.content
        const OString json(payload.data(), static_cast<sal_Int32>(payload.size()));
        // Prefer "content" after "delta" for SSE chunks; fall back to content.
        OUString delta;
        const sal_Int32 deltaKey = json.indexOf("\"delta\"");
        if (deltaKey >= 0)
        {
            const sal_Int32 ckey = json.indexOf("\"content\"", deltaKey);
            if (ckey >= 0)
            {
                sal_Int32 q1 = json.indexOf('"', ckey + 9);
                // skip : and whitespace
                sal_Int32 i = ckey + 9;
                while (i < json.getLength()
                       && (json[i] == ' ' || json[i] == '\t' || json[i] == ':' || json[i] == '\n'
                           || json[i] == '\r'))
                    ++i;
                if (i < json.getLength() && json[i] == '"')
                {
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
                            else if (n == 'r')
                                val.append('\r');
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
                    delta = OStringToOUString(val.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
                }
                (void)q1;
            }
        }
        if (delta.isEmpty())
            continue;
        bSawData = true;
        assembled.append(delta);
        if (rOnChunk && !rOnChunk(delta))
            break;
    }
    ::pclose(pipe);
    ::unlink(bodyPath);
    ::unlink(hdrPath);
    m_lastHttpCode = httpCode;

    if (cancelled())
        return assembled.makeStringAndClear();

    if (!bSawData)
    {
        // Gateway may not support stream — leave empty so helper falls back to chat().
        if (httpCode == 401 || httpCode == 403)
            m_lastErrorZh = AuthErrorZhFromHttp(httpCode, OString());
        else if (httpCode != 0 && (httpCode < 200 || httpCode >= 300))
            m_lastErrorZh = AuthErrorZhFromHttp(httpCode, OString());
        return OUString();
    }
    return assembled.makeStringAndClear();
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
