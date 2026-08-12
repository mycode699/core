/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "DocumentAIEnterpriseConnectors.hxx"

#include <DocumentAIInputPrefs.hxx>

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace kqoffice::ai::chat
{
namespace
{
OUString envOrEmpty(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return OUString();
    return OUString::fromUtf8(v);
}

bool envTruthy(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return false;
    const OUString s = OUString::fromUtf8(v).trim().toAsciiLowerCase();
    return s == u"1"_ustr || s == u"true"_ustr || s == u"yes"_ustr || s == u"on"_ustr;
}

OUString homeConfigRoot()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return u"/tmp/kqoffice"_ustr;
    return OUString::fromUtf8(home) + u"/.config/kqoffice"_ustr;
}

OUString readFileUtf8(const OUString& rSysPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return OUString();
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return OUString();
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 512 * 1024)
    {
        f.close();
        return OUString();
    }
    std::string buf(static_cast<size_t>(sz), '\0');
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    if (n != sz)
        return OUString();
    return OUString::fromUtf8(std::string_view(buf.data(), buf.size()));
}

bool writeFileUtf8(const OUString& rSysPath, const std::string& body)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    // ensure parent
    const sal_Int32 slash = rSysPath.lastIndexOf(u'/');
    if (slash > 0)
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(rSysPath.copy(0, slash), dirUrl)
            == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    sal_uInt64 n = 0;
    const bool ok = f.write(body.data(), body.size(), n) == osl::FileBase::E_None
                    && n == body.size();
    f.close();
    return ok;
}

OUString jsonStringField(const OUString& frag, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = frag.indexOf(needle);
    if (p < 0)
        return OUString();
    p = frag.indexOf(u':', p);
    if (p < 0)
        return OUString();
    sal_Int32 q1 = frag.indexOf(u'"', p + 1);
    if (q1 < 0)
        return OUString();
    sal_Int32 q2 = q1 + 1;
    while (q2 < frag.getLength())
    {
        if (frag[q2] == u'\\' && q2 + 1 < frag.getLength())
        {
            q2 += 2;
            continue;
        }
        if (frag[q2] == u'"')
            break;
        ++q2;
    }
    if (q2 >= frag.getLength())
        return OUString();
    return frag.copy(q1 + 1, q2 - q1 - 1);
}

bool jsonBoolField(const OUString& frag, const OUString& key, bool def)
{
    const OUString needle = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = frag.indexOf(needle);
    if (p < 0)
        return def;
    p = frag.indexOf(u':', p + needle.getLength());
    if (p < 0)
        return def;
    const OUString rest = frag.copy(p + 1);
    if (rest.indexOf(u"true"_ustr) >= 0 && rest.indexOf(u"true"_ustr) < 12)
        return true;
    if (rest.indexOf(u"false"_ustr) >= 0 && rest.indexOf(u"false"_ustr) < 12)
        return false;
    return def;
}

/// Split connectors.json "connectors":[ {...}, {...} ] naively by objects.
std::vector<OUString> splitJsonObjects(const OUString& body)
{
    std::vector<OUString> out;
    sal_Int32 arr = body.indexOf(u"["_ustr);
    if (arr < 0)
        return out;
    sal_Int32 i = arr + 1;
    while (i < body.getLength())
    {
        while (i < body.getLength() && body[i] != u'{')
        {
            if (body[i] == u']')
                return out;
            ++i;
        }
        if (i >= body.getLength())
            break;
        sal_Int32 start = i;
        sal_Int32 depth = 0;
        for (; i < body.getLength(); ++i)
        {
            if (body[i] == u'{')
                ++depth;
            else if (body[i] == u'}')
            {
                --depth;
                if (depth == 0)
                {
                    out.push_back(body.copy(start, i - start + 1));
                    ++i;
                    break;
                }
            }
        }
    }
    return out;
}

std::vector<EnterpriseConnector> loadFromDisk()
{
    std::vector<EnterpriseConnector> list;
    const OUString path = DocumentAIEnterpriseConnectors::defaultConfigPath();
    const OUString body = readFileUtf8(path);
    if (body.isEmpty())
    {
        // Built-in template entries (disabled, not granted) for documentation.
        EnterpriseConnector a;
        a.id = u"example-corp-docs"_ustr;
        a.nameZh = u"示例·企业文档库（未启用）"_ustr;
        a.baseUrl = u"http://docs.example.corp"_ustr;
        a.scopeSummaryZh = u"仅在管理员配置且你显式授权后，才可检索已授权目录元数据（private 网关）"_ustr;
        a.enabled = false;
        a.granted = false;
        a.requiresNetwork = true;
        a.privateOnly = true;
        a.gatewayPath = u"/api/search?q={query}"_ustr;
        list.push_back(a);
        return list;
    }

    const auto grantsBody = readFileUtf8(DocumentAIEnterpriseConnectors::defaultGrantsPath());

    for (const auto& obj : splitJsonObjects(body))
    {
        EnterpriseConnector c;
        c.id = jsonStringField(obj, u"id"_ustr);
        if (c.id.isEmpty())
            continue;
        c.nameZh = jsonStringField(obj, u"nameZh"_ustr);
        if (c.nameZh.isEmpty())
            c.nameZh = jsonStringField(obj, u"name"_ustr);
        c.baseUrl = jsonStringField(obj, u"baseUrl"_ustr);
        c.scopeSummaryZh = jsonStringField(obj, u"scopeSummaryZh"_ustr);
        c.enabled = jsonBoolField(obj, u"enabled"_ustr, false);
        c.requiresNetwork = jsonBoolField(obj, u"requiresNetwork"_ustr, true);
        c.privateOnly = jsonBoolField(obj, u"privateOnly"_ustr, true);
        c.gatewayPath = jsonStringField(obj, u"gatewayPath"_ustr);
        c.httpMethod = jsonStringField(obj, u"httpMethod"_ustr);
        c.authScheme = jsonStringField(obj, u"authScheme"_ustr);
        c.authHeaderName = jsonStringField(obj, u"authHeaderName"_ustr);
        c.deviceAuthPath = jsonStringField(obj, u"deviceAuthPath"_ustr);
        c.deviceTokenPath = jsonStringField(obj, u"deviceTokenPath"_ustr);
        c.clientId = jsonStringField(obj, u"clientId"_ustr);
        // grants file: {"example-corp-docs":true}
        if (!grantsBody.isEmpty())
        {
            const OUString gNeedle = u"\""_ustr + c.id + u"\""_ustr;
            if (grantsBody.indexOf(gNeedle) >= 0)
                c.granted = jsonBoolField(grantsBody, c.id, false);
        }
        c.granted = c.granted || jsonBoolField(obj, u"granted"_ustr, false);
        list.push_back(c);
    }
    return list;
}

OUString extractHostFromUrl(const OUString& url)
{
    OUString u = url.trim();
    sal_Int32 scheme = u.indexOf(u"://"_ustr);
    if (scheme < 0)
        return OUString();
    sal_Int32 hostStart = scheme + 3;
    sal_Int32 hostEnd = hostStart;
    while (hostEnd < u.getLength())
    {
        const sal_Unicode c = u[hostEnd];
        if (c == u'/' || c == u':' || c == u'?' || c == u'#')
            break;
        ++hostEnd;
    }
    if (hostEnd <= hostStart)
        return OUString();
    return u.copy(hostStart, hostEnd - hostStart).toAsciiLowerCase();
}

bool isIpv4Private(const OUString& host)
{
    // Simple dotted decimal check
    sal_Int32 parts[4] = { -1, -1, -1, -1 };
    sal_Int32 start = 0;
    for (int i = 0; i < 4; ++i)
    {
        sal_Int32 dot = (i < 3) ? host.indexOf(u'.', start) : host.getLength();
        if (i < 3 && dot < 0)
            return false;
        const OUString seg = host.copy(start, (i < 3 ? dot : host.getLength()) - start);
        if (seg.isEmpty())
            return false;
        for (sal_Int32 k = 0; k < seg.getLength(); ++k)
            if (seg[k] < u'0' || seg[k] > u'9')
                return false;
        parts[i] = seg.toInt32();
        if (parts[i] < 0 || parts[i] > 255)
            return false;
        start = dot + 1;
    }
    if (parts[0] == 10)
        return true;
    if (parts[0] == 192 && parts[1] == 168)
        return true;
    if (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31)
        return true;
    if (parts[0] == 127)
        return true;
    return false;
}

/// Shell-escape single quotes for use inside '...' (posix).
OString shellSingleQuote(const OString& s)
{
    OStringBuffer b;
    b.append('\'');
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const char c = s[i];
        if (c == '\'')
            b.append("'\\''");
        else
            b.append(c);
    }
    b.append('\'');
    return b.makeStringAndClear();
}

/// Private gateway HTTP via curl. GET or POST. Optional Bearer / custom header.
/// Secrets never logged. Hard-caps time and body size. No redirects.
struct PrivateGatewayHttpResult
{
    OUString body;
    bool usedAuth = false;
};

PrivateGatewayHttpResult privateGatewayHttp(const OUString& method, const OUString& fullUrl,
                                            const OUString& authHeaderLine,
                                            const OUString& postBody)
{
    PrivateGatewayHttpResult out;
    const OString url = OUStringToOString(fullUrl, RTL_TEXTENCODING_UTF8);
    const OString meth
        = OUStringToOString(method.isEmpty() ? u"GET"_ustr : method, RTL_TEXTENCODING_UTF8);

    // Body file for POST (avoids shell-injecting body content).
    char bodyTmpl[] = "/tmp/kqoffice-ec-body-XXXXXX";
    OString bodyPath;
    if (meth.equalsIgnoreAsciiCase("POST") && !postBody.isEmpty())
    {
        const int fd = ::mkstemp(bodyTmpl);
        if (fd >= 0)
        {
            const OString utf8 = OUStringToOString(postBody, RTL_TEXTENCODING_UTF8);
            if (::write(fd, utf8.getStr(), static_cast<size_t>(utf8.getLength())) < 0)
            { /* ignore */
            }
            ::close(fd);
            bodyPath = OString(bodyTmpl);
        }
    }

    OStringBuffer cmd;
    cmd.append("curl -sS --http1.1 --max-time 8 --connect-timeout 2 "
               "--max-filesize 262144 --max-redirs 0 "
               "-H 'Accept: application/json, text/plain, */*' "
               "-X ");
    cmd.append(shellSingleQuote(meth));
    if (!authHeaderLine.isEmpty())
    {
        const OString hdr = OUStringToOString(authHeaderLine, RTL_TEXTENCODING_UTF8);
        cmd.append(" -H ");
        cmd.append(shellSingleQuote(hdr));
        out.usedAuth = true;
    }
    if (!bodyPath.isEmpty())
    {
        cmd.append(" -H 'Content-Type: application/json' --data-binary @");
        cmd.append(shellSingleQuote(bodyPath));
    }
    cmd.append(' ');
    cmd.append(shellSingleQuote(url));
    cmd.append(" 2>/dev/null");

    FILE* pipe = ::popen(cmd.makeStringAndClear().getStr(), "r");
    if (!pipe)
    {
        if (!bodyPath.isEmpty())
            ::unlink(bodyPath.getStr());
        return out;
    }
    std::string body;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe) && body.size() < 262144)
        body.append(buf);
    ::pclose(pipe);
    if (!bodyPath.isEmpty())
        ::unlink(bodyPath.getStr());
    if (!body.empty())
        out.body = OUString::fromUtf8(std::string_view(body.data(), body.size()));
    return out;
}

OUString loadSecretForId(const OUString& id)
{
    if (id.isEmpty())
        return OUString();
    // Env: KQOFFICE_CONNECTOR_SECRET_<ID> with non-alnum → _
    {
        OStringBuffer envName;
        envName.append("KQOFFICE_CONNECTOR_SECRET_");
        const OString id8 = OUStringToOString(id, RTL_TEXTENCODING_UTF8);
        for (sal_Int32 i = 0; i < id8.getLength(); ++i)
        {
            const char c = id8[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                envName.append(static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c));
            else
                envName.append('_');
        }
        const char* v = std::getenv(envName.makeStringAndClear().getStr());
        if (v && *v)
            return OUString::fromUtf8(v);
    }
    const OUString body = readFileUtf8(DocumentAIEnterpriseConnectors::defaultSecretsPath());
    if (body.isEmpty())
        return OUString();
    return jsonStringField(body, id);
}

bool writeConnectorsCatalog(const std::vector<EnterpriseConnector>& list)
{
    OUStringBuffer b;
    b.append(u"{\n  \"connectors\": [\n"_ustr);
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& c = list[i];
        if (i)
            b.append(u",\n"_ustr);
        b.append(u"    {\n"_ustr);
        b.append(u"      \"id\": \""_ustr);
        b.append(c.id);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"nameZh\": \""_ustr);
        b.append(c.nameZh);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"baseUrl\": \""_ustr);
        b.append(c.baseUrl);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"scopeSummaryZh\": \""_ustr);
        b.append(c.scopeSummaryZh);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"enabled\": "_ustr);
        b.append(c.enabled ? u"true"_ustr : u"false"_ustr);
        b.append(u",\n"_ustr);
        b.append(u"      \"requiresNetwork\": "_ustr);
        b.append(c.requiresNetwork ? u"true"_ustr : u"false"_ustr);
        b.append(u",\n"_ustr);
        b.append(u"      \"privateOnly\": "_ustr);
        b.append(c.privateOnly ? u"true"_ustr : u"false"_ustr);
        b.append(u",\n"_ustr);
        b.append(u"      \"gatewayPath\": \""_ustr);
        b.append(c.gatewayPath);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"httpMethod\": \""_ustr);
        b.append(c.httpMethod);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"authScheme\": \""_ustr);
        b.append(c.authScheme);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"authHeaderName\": \""_ustr);
        b.append(c.authHeaderName);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"deviceAuthPath\": \""_ustr);
        b.append(c.deviceAuthPath);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"deviceTokenPath\": \""_ustr);
        b.append(c.deviceTokenPath);
        b.append(u"\",\n"_ustr);
        b.append(u"      \"clientId\": \""_ustr);
        b.append(c.clientId);
        b.append(u"\"\n"_ustr);
        b.append(u"    }"_ustr);
    }
    b.append(u"\n  ]\n}\n"_ustr);
    const OString utf8 = OUStringToOString(b.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    return writeFileUtf8(DocumentAIEnterpriseConnectors::defaultConfigPath(),
                         std::string(utf8.getStr(), static_cast<size_t>(utf8.getLength())));
}
} // namespace

OUString DocumentAIEnterpriseConnectors::defaultConfigPath()
{
    const OUString o = envOrEmpty("KQOFFICE_CONNECTORS_JSON");
    if (!o.isEmpty())
        return o;
    return homeConfigRoot() + u"/connectors.json"_ustr;
}

OUString DocumentAIEnterpriseConnectors::defaultGrantsPath()
{
    return homeConfigRoot() + u"/connector-grants.json"_ustr;
}

OUString DocumentAIEnterpriseConnectors::defaultSecretsPath()
{
    const OUString o = envOrEmpty("KQOFFICE_CONNECTOR_SECRETS");
    if (!o.isEmpty())
        return o;
    return homeConfigRoot() + u"/connector-secrets.json"_ustr;
}

bool DocumentAIEnterpriseConnectors::globalEnabled()
{
    if (envTruthy("KQOFFICE_CONNECTORS_ENABLE"))
        return true;
    // Prefs master switch (default false)
    const auto prefs = DocumentAIInputPrefs::load();
    return prefs.enterpriseConnectorsEnabled;
}

std::vector<EnterpriseConnector> DocumentAIEnterpriseConnectors::listConnectors()
{
    auto list = loadFromDisk();
    for (auto& c : list)
        c.hasLocalSecret = !loadSecretForId(c.id).isEmpty();
    return list;
}

OUString DocumentAIEnterpriseConnectors::listConnectorIdsJson()
{
    OUStringBuffer b;
    b.append(u"["_ustr);
    bool first = true;
    for (const auto& c : listConnectors())
    {
        if (!first)
            b.append(u',');
        first = false;
        b.append(u'"');
        b.append(c.id);
        b.append(u'"');
    }
    b.append(u"]"_ustr);
    return b.makeStringAndClear();
}

OUString DocumentAIEnterpriseConnectors::statusSummaryZh()
{
    OUStringBuffer b;
    b.append(u"企业连接器 · 总开关="_ustr);
    b.append(globalEnabled() ? u"开"_ustr : u"关（默认）"_ustr);
    b.append(u"\n"_ustr);
    const auto list = listConnectors();
    if (list.empty())
    {
        b.append(u"未配置连接器。路径："_ustr);
        b.append(defaultConfigPath());
        b.append(u"\n"_ustr);
        return b.makeStringAndClear();
    }
    for (const auto& c : list)
    {
        b.append(u"· "_ustr);
        b.append(c.id);
        b.append(u" · "_ustr);
        b.append(c.nameZh);
        b.append(u" · enabled="_ustr);
        b.append(c.enabled ? u"1"_ustr : u"0"_ustr);
        b.append(u" granted="_ustr);
        b.append(c.granted ? u"1"_ustr : u"0"_ustr);
        b.append(u"\n  范围："_ustr);
        b.append(c.scopeSummaryZh);
        b.append(u"\n"_ustr);
    }
    b.append(u"策略：无总开关 / 未启用 / 未授权 / 非私网 → 拒绝外联；"
             u"仅 private 网关（loopback/RFC1918/*.corp）可在批准后 HTTP。\n"_ustr);
    return b.makeStringAndClear();
}

ConnectorInvokeResult DocumentAIEnterpriseConnectors::invoke(const ConnectorInvokeRequest& rReq)
{
    ConnectorInvokeResult r;
    r.connectorId = rReq.connectorId;
    r.networkAttempted = false;

    if (!globalEnabled())
    {
        r.status = u"disabled"_ustr;
        r.messageZh
            = u"企业连接器总开关关闭（默认）· 未发起网络 · 可在偏好 enterpriseConnectorsEnabled 或 "
              u"KQOFFICE_CONNECTORS_ENABLE=1 开启后再逐项授权"_ustr;
        return r;
    }

    if (rReq.connectorId.isEmpty())
    {
        r.status = u"unknown"_ustr;
        r.messageZh = u"缺少 connectorId · 未发起网络"_ustr;
        return r;
    }

    EnterpriseConnector found;
    bool have = false;
    for (const auto& c : listConnectors())
    {
        if (c.id == rReq.connectorId)
        {
            found = c;
            have = true;
            break;
        }
    }
    if (!have)
    {
        r.status = u"unknown"_ustr;
        r.messageZh = u"未知连接器："_ustr + rReq.connectorId + u" · 未发起网络"_ustr;
        return r;
    }

    if (!found.enabled)
    {
        r.status = u"disabled"_ustr;
        r.messageZh = u"连接器未启用："_ustr + found.nameZh + u" · 未发起网络"_ustr;
        return r;
    }
    if (!found.granted)
    {
        r.status = u"not-granted"_ustr;
        r.messageZh = u"连接器未授权："_ustr + found.nameZh
                      + u" · 请先 setGranted · 范围："_ustr + found.scopeSummaryZh
                      + u" · 未发起网络"_ustr;
        return r;
    }

    // status op is local-only even when granted
    const OUString op = rReq.operation.trim().toAsciiLowerCase();
    if (op.isEmpty() || op == u"status"_ustr || op == u"list"_ustr)
    {
        r.success = true;
        r.status = u"ok"_ustr;
        r.messageZh = u"连接器就绪（本地状态）· "_ustr + found.nameZh + u" · 外联仍须显式操作批准"_ustr;
        r.content = statusSummaryZh();
        return r;
    }

    if (!rReq.explicitUserApproval)
    {
        r.status = u"policy-denied"_ustr;
        r.messageZh
            = u"拒绝外联：缺少 explicitUserApproval=true · 连接器="_ustr + found.id
              + u" · 范围="_ustr + found.scopeSummaryZh + u" · 未发起网络"_ustr;
        return r;
    }

    // Private gateway only — never silent public egress.
    if (found.baseUrl.isEmpty())
    {
        r.status = u"unsupported"_ustr;
        r.messageZh = u"连接器无 baseUrl · 未发起网络"_ustr;
        return r;
    }
    if (found.privateOnly && !isPrivateBaseUrl(found.baseUrl))
    {
        r.status = u"policy-denied"_ustr;
        r.messageZh
            = u"拒绝外联：baseUrl 非私网/内网主机（privateOnly）· "_ustr + found.baseUrl
              + u" · 未发起网络"_ustr;
        return r;
    }
    if (!found.privateOnly && !isPrivateBaseUrl(found.baseUrl))
    {
        // Even if privateOnly=false, refuse non-private hosts in this product build.
        r.status = u"policy-denied"_ustr;
        r.messageZh
            = u"拒绝公网连接器主机 · 仅允许 loopback / RFC1918 / *.local|*.corp|*.internal · "
              u"未发起网络"_ustr;
        return r;
    }

    OUString path = found.gatewayPath;
    if (path.isEmpty())
    {
        if (op == u"search"_ustr)
            path = u"/search?q={query}"_ustr;
        else if (op == u"fetch"_ustr)
            path = u"/fetch?id={query}"_ustr;
        else if (op == u"post"_ustr)
            path = u"/api"_ustr;
        else
            path = u"/"_ustr;
    }
    path = path.replaceAll(u"{query}"_ustr, rReq.query);
    path = path.replaceAll(u"{op}"_ustr, op);
    // Build full URL without double slash
    OUString base = found.baseUrl;
    while (base.endsWith(u"/"_ustr))
        base = base.copy(0, base.getLength() - 1);
    if (!path.startsWith(u"/"_ustr))
        path = u"/"_ustr + path;
    const OUString fullUrl = base + path;

    // Method: request override → connector config → heuristic
    OUString method = rReq.httpMethod.trim().toAsciiUpperCase();
    if (method.isEmpty())
        method = found.httpMethod.trim().toAsciiUpperCase();
    if (method.isEmpty())
    {
        if (op == u"post"_ustr || op == u"create"_ustr || op == u"write"_ustr
            || op == u"update"_ustr)
            method = u"POST"_ustr;
        else
            method = u"GET"_ustr;
    }
    if (method != u"GET"_ustr && method != u"POST"_ustr)
    {
        r.status = u"policy-denied"_ustr;
        r.messageZh = u"仅允许 GET/POST · method="_ustr + method + u" · 未发起网络"_ustr;
        return r;
    }
    r.httpMethod = method;

    // Auth from local secrets (never log secret value)
    OUString authLine;
    const OUString secret = loadSecretForId(found.id);
    const OUString scheme = found.authScheme.trim().toAsciiLowerCase();
    if (!secret.isEmpty() && scheme != u"none"_ustr)
    {
        OUString hdrName = found.authHeaderName.trim();
        if (hdrName.isEmpty())
            hdrName = u"Authorization"_ustr;
        if (scheme.isEmpty() || scheme == u"bearer"_ustr)
            authLine = hdrName + u": Bearer "_ustr + secret;
        else if (scheme == u"header"_ustr || scheme == u"raw"_ustr)
            authLine = hdrName + u": "_ustr + secret;
        else
            authLine = hdrName + u": Bearer "_ustr + secret;
    }

    OUString postBody = rReq.body;
    if (method == u"POST"_ustr && postBody.isEmpty())
    {
        // Minimal JSON body from query/op (no secret).
        OUStringBuffer jb;
        jb.append(u"{\"op\":\""_ustr);
        jb.append(op);
        jb.append(u"\",\"q\":\""_ustr);
        for (sal_Int32 i = 0; i < rReq.query.getLength(); ++i)
        {
            const sal_Unicode ch = rReq.query[i];
            if (ch == u'\\' || ch == u'"')
                jb.append(u'\\');
            if (ch == u'\n')
            {
                jb.append(u"\\n"_ustr);
                continue;
            }
            jb.append(ch);
        }
        jb.append(u"\"}"_ustr);
        postBody = jb.makeStringAndClear();
    }

    const auto http = privateGatewayHttp(method, fullUrl, authLine, postBody);
    r.networkAttempted = true;
    r.usedAuthHeader = http.usedAuth;
    if (http.body.isEmpty())
    {
        r.status = u"error"_ustr;
        r.success = false;
        r.messageZh
            = u"private 网关无响应或超时 · "_ustr + method + u" "_ustr + fullUrl
              + u" · auth="_ustr + (http.usedAuth ? u"1"_ustr : u"0"_ustr)
              + u" · 已尝试内网 HTTP · 未改主文档"_ustr;
        return r;
    }
    r.success = true;
    r.status = u"ok"_ustr;
    r.content = http.body.getLength() > 12000 ? http.body.copy(0, 12000) + u"…"_ustr : http.body;
    r.messageZh = u"private 网关成功 · "_ustr + found.id + u" · "_ustr + method + u" "_ustr + op
                  + u" · auth="_ustr + (http.usedAuth ? u"1"_ustr : u"0"_ustr)
                  + u" · 字节≈"_ustr + OUString::number(http.body.getLength());
    return r;
}

bool DocumentAIEnterpriseConnectors::isPrivateBaseUrl(const OUString& rBaseUrl)
{
    const OUString lower = rBaseUrl.trim().toAsciiLowerCase();
    if (!(lower.startsWith(u"http://"_ustr) || lower.startsWith(u"https://"_ustr)))
        return false;
    const OUString host = extractHostFromUrl(lower);
    if (host.isEmpty())
        return false;
    if (host == u"localhost"_ustr || host == u"127.0.0.1"_ustr || host == u"::1"_ustr
        || host == u"[::1]"_ustr)
        return true;
    if (isIpv4Private(host))
        return true;
    if (host.endsWith(u".local"_ustr) || host.endsWith(u".corp"_ustr)
        || host.endsWith(u".internal"_ustr) || host.endsWith(u".lan"_ustr)
        || host.endsWith(u".intranet"_ustr))
        return true;
    return false;
}

OUString DocumentAIEnterpriseConnectors::formatListRowZh(const EnterpriseConnector& c)
{
    OUStringBuffer b;
    b.append(c.id);
    b.append(u" · "_ustr);
    b.append(c.nameZh.isEmpty() ? u"(unnamed)"_ustr : c.nameZh);
    b.append(u" · en="_ustr);
    b.append(c.enabled ? u"1"_ustr : u"0"_ustr);
    b.append(u" grant="_ustr);
    b.append(c.granted ? u"1"_ustr : u"0"_ustr);
    b.append(c.privateOnly ? u" · private"_ustr : u" · !privateOnly"_ustr);
    b.append(c.hasLocalSecret ? u" · secret=1"_ustr : u" · secret=0"_ustr);
    if (!c.httpMethod.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(c.httpMethod);
    }
    if (!c.baseUrl.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(c.baseUrl);
    }
    return b.makeStringAndClear();
}

bool DocumentAIEnterpriseConnectors::hasLocalSecret(const OUString& rId)
{
    return !loadSecretForId(rId).isEmpty();
}

bool DocumentAIEnterpriseConnectors::setLocalSecret(const OUString& rId, const OUString& rSecret)
{
    if (rId.isEmpty())
        return false;
    // Merge into secrets JSON: rewrite known connector ids + this id.
    OUStringBuffer b;
    b.append(u"{\n"_ustr);
    bool first = true;
    bool replaced = false;
    auto appendEntry = [&](const OUString& id, const OUString& secret) {
        if (secret.isEmpty())
            return;
        if (!first)
            b.append(u",\n"_ustr);
        first = false;
        b.append(u"  \""_ustr);
        b.append(id);
        b.append(u"\": \""_ustr);
        for (sal_Int32 i = 0; i < secret.getLength(); ++i)
        {
            const sal_Unicode c = secret[i];
            if (c == u'\\' || c == u'"')
                b.append(u'\\');
            if (c == u'\n' || c == u'\r')
                continue;
            b.append(c);
        }
        b.append(u"\""_ustr);
    };
    for (const auto& c : listConnectors())
    {
        if (c.id == rId)
        {
            appendEntry(rId, rSecret);
            replaced = true;
        }
        else
        {
            appendEntry(c.id, loadSecretForId(c.id));
        }
    }
    if (!replaced)
        appendEntry(rId, rSecret);
    b.append(u"\n}\n"_ustr);
    const OString utf8 = OUStringToOString(b.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    const bool ok = writeFileUtf8(defaultSecretsPath(),
                                  std::string(utf8.getStr(), static_cast<size_t>(utf8.getLength())));
#if defined(__APPLE__) || defined(__unix__)
    if (ok)
    {
        const OString path8 = OUStringToOString(defaultSecretsPath(), RTL_TEXTENCODING_UTF8);
        ::chmod(path8.getStr(), 0600);
    }
#endif
    return ok;
}

bool DocumentAIEnterpriseConnectors::setGranted(const OUString& rId, bool bGranted)
{
    if (rId.isEmpty())
        return false;
    // Merge into grants file
    OUString body = readFileUtf8(defaultGrantsPath());
    if (body.isEmpty())
        body = u"{}"_ustr;
    // naive rewrite whole object
    OUStringBuffer b;
    b.append(u"{\n"_ustr);
    bool first = true;
    bool replaced = false;
    // preserve other keys roughly by re-listing known connectors
    for (const auto& c : listConnectors())
    {
        bool g = c.granted;
        if (c.id == rId)
        {
            g = bGranted;
            replaced = true;
        }
        if (!first)
            b.append(u",\n"_ustr);
        first = false;
        b.append(u"  \""_ustr);
        b.append(c.id);
        b.append(u"\": "_ustr);
        b.append(g ? u"true"_ustr : u"false"_ustr);
    }
    if (!replaced)
    {
        if (!first)
            b.append(u",\n"_ustr);
        b.append(u"  \""_ustr);
        b.append(rId);
        b.append(u"\": "_ustr);
        b.append(bGranted ? u"true"_ustr : u"false"_ustr);
    }
    b.append(u"\n}\n"_ustr);
    const OString utf8 = OUStringToOString(b.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    return writeFileUtf8(defaultGrantsPath(),
                         std::string(utf8.getStr(), static_cast<size_t>(utf8.getLength())));
}

bool DocumentAIEnterpriseConnectors::setEnabled(const OUString& rId, bool bEnabled)
{
    if (rId.isEmpty())
        return false;
    auto list = listConnectors();
    bool found = false;
    for (auto& c : list)
    {
        if (c.id == rId)
        {
            c.enabled = bEnabled;
            found = true;
            break;
        }
    }
    if (!found)
    {
        // Allow enabling unknown id by creating a stub private entry.
        EnterpriseConnector c;
        c.id = rId;
        c.nameZh = rId;
        c.enabled = bEnabled;
        c.granted = false;
        c.privateOnly = true;
        c.requiresNetwork = true;
        list.push_back(c);
    }
    return writeConnectorsCatalog(list);
}

OUString DocumentAIEnterpriseConnectors::defaultDeviceSessionsPath()
{
    return homeConfigRoot() + u"/connector-device-sessions.json"_ustr;
}

namespace
{
sal_Int64 nowUnix()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds);
}

OUString joinBasePath(const OUString& baseUrl, const OUString& pathIn)
{
    OUString base = baseUrl;
    while (base.endsWith(u"/"_ustr))
        base = base.copy(0, base.getLength() - 1);
    OUString path = pathIn;
    if (path.isEmpty())
        path = u"/"_ustr;
    if (!path.startsWith(u"/"_ustr))
        path = u"/"_ustr + path;
    return base + path;
}

bool saveDeviceSession(const ConnectorDeviceSession& s)
{
    // Single-object map by connectorId (simple rewrite of one entry).
    OUString existing = readFileUtf8(DocumentAIEnterpriseConnectors::defaultDeviceSessionsPath());
    if (existing.isEmpty())
        existing = u"{}"_ustr;
    // Drop prior object for this id if present — rewrite whole map from known sessions loosely.
    OUStringBuffer b;
    b.append(u"{\n"_ustr);
    b.append(u"  \""_ustr);
    b.append(s.connectorId);
    b.append(u"\": {\n"_ustr);
    auto field = [&](const OUString& k, const OUString& v, bool last = false) {
        b.append(u"    \""_ustr);
        b.append(k);
        b.append(u"\": \""_ustr);
        for (sal_Int32 i = 0; i < v.getLength(); ++i)
        {
            const sal_Unicode c = v[i];
            if (c == u'\\' || c == u'"')
                b.append(u'\\');
            if (c == u'\n')
            {
                b.append(u"\\n"_ustr);
                continue;
            }
            b.append(c);
        }
        b.append(u"\""_ustr);
        b.append(last ? u"\n"_ustr : u",\n"_ustr);
    };
    auto fieldInt = [&](const OUString& k, sal_Int64 v, bool last = false) {
        b.append(u"    \""_ustr);
        b.append(k);
        b.append(u"\": "_ustr);
        b.append(OUString::number(v));
        b.append(last ? u"\n"_ustr : u",\n"_ustr);
    };
    field(u"deviceCode"_ustr, s.deviceCode);
    field(u"userCode"_ustr, s.userCode);
    field(u"verificationUri"_ustr, s.verificationUri);
    field(u"verificationUriComplete"_ustr, s.verificationUriComplete);
    field(u"status"_ustr, s.status);
    field(u"messageZh"_ustr, s.messageZh);
    fieldInt(u"intervalSec"_ustr, s.intervalSec);
    fieldInt(u"expiresAtUnix"_ustr, s.expiresAtUnix, true);
    b.append(u"  }\n}\n"_ustr);
    (void)existing; // prior sessions other than this id intentionally not merged (v0 single-active)
    const OString utf8 = OUStringToOString(b.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    return writeFileUtf8(DocumentAIEnterpriseConnectors::defaultDeviceSessionsPath(),
                         std::string(utf8.getStr(), static_cast<size_t>(utf8.getLength())));
}

ConnectorDeviceSession loadDeviceSession(const OUString& id)
{
    ConnectorDeviceSession s;
    s.connectorId = id;
    s.status = u"none"_ustr;
    s.messageZh = u"无设备码会话"_ustr;
    if (id.isEmpty())
        return s;
    const OUString body = readFileUtf8(DocumentAIEnterpriseConnectors::defaultDeviceSessionsPath());
    if (body.isEmpty())
        return s;
    // Find object for id
    const OUString needle = u"\""_ustr + id + u"\""_ustr;
    sal_Int32 p = body.indexOf(needle);
    if (p < 0)
        return s;
    sal_Int32 brace = body.indexOf(u'{', p);
    if (brace < 0)
        return s;
    sal_Int32 depth = 0;
    sal_Int32 end = brace;
    for (; end < body.getLength(); ++end)
    {
        if (body[end] == u'{')
            ++depth;
        else if (body[end] == u'}')
        {
            --depth;
            if (depth == 0)
            {
                ++end;
                break;
            }
        }
    }
    const OUString obj = body.copy(brace, end - brace);
    s.deviceCode = jsonStringField(obj, u"deviceCode"_ustr);
    s.userCode = jsonStringField(obj, u"userCode"_ustr);
    s.verificationUri = jsonStringField(obj, u"verificationUri"_ustr);
    s.verificationUriComplete = jsonStringField(obj, u"verificationUriComplete"_ustr);
    s.status = jsonStringField(obj, u"status"_ustr);
    if (s.status.isEmpty())
        s.status = u"pending"_ustr;
    s.messageZh = jsonStringField(obj, u"messageZh"_ustr);
    {
        const OUString iv = jsonStringField(obj, u"intervalSec"_ustr);
        if (!iv.isEmpty())
            s.intervalSec = iv.toInt32();
        // also bare number
        sal_Int32 ip = obj.indexOf(u"\"intervalSec\""_ustr);
        if (ip >= 0)
        {
            sal_Int32 colon = obj.indexOf(u':', ip);
            if (colon >= 0)
            {
                sal_Int32 j = colon + 1;
                while (j < obj.getLength() && (obj[j] == u' ' || obj[j] == u'\t'))
                    ++j;
                sal_Int32 k = j;
                while (k < obj.getLength() && obj[k] >= u'0' && obj[k] <= u'9')
                    ++k;
                if (k > j)
                    s.intervalSec = obj.copy(j, k - j).toInt32();
            }
        }
        if (s.intervalSec < 1)
            s.intervalSec = 5;
    }
    {
        sal_Int32 ip = obj.indexOf(u"\"expiresAtUnix\""_ustr);
        if (ip >= 0)
        {
            sal_Int32 colon = obj.indexOf(u':', ip);
            if (colon >= 0)
            {
                sal_Int32 j = colon + 1;
                while (j < obj.getLength() && (obj[j] == u' ' || obj[j] == u'\t'))
                    ++j;
                sal_Int32 k = j;
                while (k < obj.getLength() && obj[k] >= u'0' && obj[k] <= u'9')
                    ++k;
                if (k > j)
                    s.expiresAtUnix = obj.copy(j, k - j).toInt64();
            }
        }
    }
    if (s.messageZh.isEmpty() && s.status == u"pending"_ustr)
        s.messageZh = u"等待用户在验证页输入代码"_ustr;
    return s;
}

bool findConnector(const OUString& id, EnterpriseConnector& out)
{
    for (const auto& c : DocumentAIEnterpriseConnectors::listConnectors())
    {
        if (c.id == id)
        {
            out = c;
            return true;
        }
    }
    return false;
}
} // namespace

ConnectorDeviceSession DocumentAIEnterpriseConnectors::deviceAuthStatus(const OUString& rId)
{
    return loadDeviceSession(rId);
}

bool DocumentAIEnterpriseConnectors::clearDeviceSession(const OUString& rId)
{
    if (rId.isEmpty())
        return false;
    ConnectorDeviceSession s;
    s.connectorId = rId;
    s.status = u"none"_ustr;
    s.messageZh = u"已清除设备码会话"_ustr;
    return saveDeviceSession(s);
}

ConnectorDeviceSession DocumentAIEnterpriseConnectors::startDeviceAuth(const OUString& rId,
                                                                       bool bExplicitUserApproval)
{
    ConnectorDeviceSession s;
    s.connectorId = rId;
    s.networkAttempted = false;
    if (!globalEnabled())
    {
        s.status = u"error"_ustr;
        s.messageZh = u"总开关关闭 · 未启动设备码 · 未发起网络"_ustr;
        return s;
    }
    if (!bExplicitUserApproval)
    {
        s.status = u"error"_ustr;
        s.messageZh = u"缺少 explicitUserApproval · 未启动设备码 · 未发起网络"_ustr;
        return s;
    }
    EnterpriseConnector c;
    if (!findConnector(rId, c))
    {
        s.status = u"error"_ustr;
        s.messageZh = u"未知连接器 · 未发起网络"_ustr;
        return s;
    }
    if (!c.enabled)
    {
        s.status = u"error"_ustr;
        s.messageZh = u"连接器未启用 · 未发起网络"_ustr;
        return s;
    }
    // Device start requires grant (same as invoke) for least-privilege
    if (!c.granted)
    {
        s.status = u"error"_ustr;
        s.messageZh = u"连接器未授权 · 请先 setGranted · 未发起网络"_ustr;
        return s;
    }
    if (c.baseUrl.isEmpty() || !isPrivateBaseUrl(c.baseUrl))
    {
        s.status = u"error"_ustr;
        s.messageZh = u"baseUrl 非私网 · 拒绝设备码 · 未发起网络"_ustr;
        return s;
    }

    OUString path = c.deviceAuthPath;
    if (path.isEmpty())
        path = u"/oauth/device/code"_ustr;
    const OUString url = joinBasePath(c.baseUrl, path);
    OUString body = u"{\"client_id\":\""_ustr;
    body += c.clientId.isEmpty() ? u"kqoffice"_ustr : c.clientId;
    body += u"\"}"_ustr;

    const auto http = privateGatewayHttp(u"POST"_ustr, url, OUString(), body);
    s.networkAttempted = true;
    if (http.body.isEmpty())
    {
        s.status = u"error"_ustr;
        s.messageZh
            = u"设备码端点无响应 · "_ustr + url
              + u" · 确认 private 网关实现 RFC8628 device_authorization"_ustr;
        saveDeviceSession(s);
        return s;
    }

    s.deviceCode = jsonStringField(http.body, u"device_code"_ustr);
    s.userCode = jsonStringField(http.body, u"user_code"_ustr);
    s.verificationUri = jsonStringField(http.body, u"verification_uri"_ustr);
    if (s.verificationUri.isEmpty())
        s.verificationUri = jsonStringField(http.body, u"verification_uri_complete"_ustr);
    s.verificationUriComplete = jsonStringField(http.body, u"verification_uri_complete"_ustr);
    {
        const OUString iv = jsonStringField(http.body, u"interval"_ustr);
        if (!iv.isEmpty())
            s.intervalSec = iv.toInt32();
        sal_Int32 ip = http.body.indexOf(u"\"interval\""_ustr);
        if (ip >= 0)
        {
            sal_Int32 colon = http.body.indexOf(u':', ip);
            if (colon >= 0)
            {
                sal_Int32 j = colon + 1;
                while (j < http.body.getLength()
                       && (http.body[j] == u' ' || http.body[j] == u'\t'))
                    ++j;
                sal_Int32 k = j;
                while (k < http.body.getLength() && http.body[k] >= u'0' && http.body[k] <= u'9')
                    ++k;
                if (k > j)
                    s.intervalSec = http.body.copy(j, k - j).toInt32();
            }
        }
        if (s.intervalSec < 1)
            s.intervalSec = 5;
    }
    sal_Int32 expiresIn = 600;
    {
        sal_Int32 ip = http.body.indexOf(u"\"expires_in\""_ustr);
        if (ip >= 0)
        {
            sal_Int32 colon = http.body.indexOf(u':', ip);
            if (colon >= 0)
            {
                sal_Int32 j = colon + 1;
                while (j < http.body.getLength()
                       && (http.body[j] == u' ' || http.body[j] == u'\t'))
                    ++j;
                sal_Int32 k = j;
                while (k < http.body.getLength() && http.body[k] >= u'0' && http.body[k] <= u'9')
                    ++k;
                if (k > j)
                    expiresIn = http.body.copy(j, k - j).toInt32();
            }
        }
    }
    s.expiresAtUnix = nowUnix() + expiresIn;
    if (s.deviceCode.isEmpty() || s.userCode.isEmpty())
    {
        s.status = u"error"_ustr;
        s.messageZh = u"设备码响应缺 device_code/user_code · 未保存令牌"_ustr;
        saveDeviceSession(s);
        return s;
    }
    s.status = u"pending"_ustr;
    s.messageZh = u"请在浏览器打开 "_ustr
                  + (s.verificationUriComplete.isEmpty() ? s.verificationUri
                                                         : s.verificationUriComplete)
                  + u" 并输入代码 "_ustr + s.userCode + u" · 然后 /connector-auth-poll "_ustr
                  + rId;
    saveDeviceSession(s);
    return s;
}

ConnectorDeviceSession DocumentAIEnterpriseConnectors::pollDeviceAuth(const OUString& rId,
                                                                      bool bExplicitUserApproval)
{
    ConnectorDeviceSession s = loadDeviceSession(rId);
    s.networkAttempted = false;
    if (!globalEnabled())
    {
        s.status = u"error"_ustr;
        s.messageZh = u"总开关关闭 · 未轮询 · 未发起网络"_ustr;
        return s;
    }
    if (!bExplicitUserApproval)
    {
        s.status = u"error"_ustr;
        s.messageZh = u"缺少 explicitUserApproval · 未轮询 · 未发起网络"_ustr;
        return s;
    }
    if (s.deviceCode.isEmpty() || s.status == u"none"_ustr)
    {
        s.status = u"error"_ustr;
        s.messageZh = u"无待轮询会话 · 请先 /connector-auth <id>"_ustr;
        return s;
    }
    if (s.expiresAtUnix > 0 && nowUnix() > s.expiresAtUnix)
    {
        s.status = u"expired"_ustr;
        s.messageZh = u"设备码已过期 · 请重新 start"_ustr;
        saveDeviceSession(s);
        return s;
    }
    if (s.status == u"authorized"_ustr)
    {
        s.messageZh = u"已授权（本地会话）· 令牌在 secrets 中"_ustr;
        return s;
    }

    EnterpriseConnector c;
    if (!findConnector(rId, c) || !c.enabled || !c.granted)
    {
        s.status = u"error"_ustr;
        s.messageZh = u"连接器未启用/未授权 · 未轮询"_ustr;
        return s;
    }
    if (c.baseUrl.isEmpty() || !isPrivateBaseUrl(c.baseUrl))
    {
        s.status = u"error"_ustr;
        s.messageZh = u"baseUrl 非私网 · 拒绝轮询"_ustr;
        return s;
    }

    OUString path = c.deviceTokenPath;
    if (path.isEmpty())
        path = u"/oauth/token"_ustr;
    const OUString url = joinBasePath(c.baseUrl, path);
    OUString body = u"{\"grant_type\":\"urn:ietf:params:oauth:grant-type:device_code\","
                    u"\"device_code\":\""_ustr;
    body += s.deviceCode;
    body += u"\",\"client_id\":\""_ustr;
    body += c.clientId.isEmpty() ? u"kqoffice"_ustr : c.clientId;
    body += u"\"}"_ustr;

    const auto http = privateGatewayHttp(u"POST"_ustr, url, OUString(), body);
    s.networkAttempted = true;
    if (http.body.isEmpty())
    {
        s.status = u"pending"_ustr;
        s.messageZh = u"轮询无响应 · 可稍后重试（间隔≈"_ustr + OUString::number(s.intervalSec)
                      + u"s）"_ustr;
        saveDeviceSession(s);
        return s;
    }

    // error responses
    const OUString err = jsonStringField(http.body, u"error"_ustr);
    if (err == u"authorization_pending"_ustr || err == u"slow_down"_ustr)
    {
        s.status = u"pending"_ustr;
        s.messageZh = u"仍等待用户授权（"_ustr + err + u"）· 间隔≈"_ustr
                      + OUString::number(s.intervalSec) + u"s"_ustr;
        if (err == u"slow_down"_ustr)
            s.intervalSec += 5;
        saveDeviceSession(s);
        return s;
    }
    if (err == u"access_denied"_ustr || err == u"expired_token"_ustr)
    {
        s.status = err == u"access_denied"_ustr ? u"denied"_ustr : u"expired"_ustr;
        s.messageZh = u"设备码失败："_ustr + err;
        saveDeviceSession(s);
        return s;
    }

    const OUString token = jsonStringField(http.body, u"access_token"_ustr);
    if (token.isEmpty())
    {
        s.status = u"pending"_ustr;
        s.messageZh = u"响应无 access_token · 仍视为 pending"_ustr;
        saveDeviceSession(s);
        return s;
    }

    // Store token locally — never log token value
    setLocalSecret(rId, token);
    s.status = u"authorized"_ustr;
    s.messageZh = u"设备码授权成功 · 令牌已写入本地 secrets · 可 connector_invoke"_ustr;
    // Clear device_code from disk after success (keep status)
    s.deviceCode = OUString();
    saveDeviceSession(s);
    return s;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
