/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Membership HTTP helpers for api.03122.com (sessionToken Bearer).
 */

#include "MembershipClient.hxx"
#include "ModelRoutingConfig.hxx"
#include "OpenAICompatibleAdapter.hxx"

#include <AiResourceEnvelope.hxx>

#include <osl/time.h>
#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <unistd.h>

namespace kqoffice::ai
{
namespace
{
std::mutex& MbrCacheMutex()
{
    static std::mutex m;
    return m;
}

MembershipBoostResult g_statusCache;
sal_Int64 g_statusCacheMs = 0;
bool g_statusCacheValid = false;

sal_Int64 NowMs()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
           + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

void InvalidateStatusCache()
{
    std::lock_guard<std::mutex> g(MbrCacheMutex());
    g_statusCacheValid = false;
    g_statusCacheMs = 0;
}

bool TryGetStatusCache(MembershipBoostResult& out)
{
    std::lock_guard<std::mutex> g(MbrCacheMutex());
    if (!g_statusCacheValid)
        return false;
    const sal_Int64 ttl = kqoffice::ai::control::AiResourceEnvelope::membershipStatusCacheTtlMs();
    if ((NowMs() - g_statusCacheMs) > ttl)
    {
        g_statusCacheValid = false;
        return false;
    }
    out = g_statusCache;
    return true;
}

void PutStatusCache(const MembershipBoostResult& r)
{
    std::lock_guard<std::mutex> g(MbrCacheMutex());
    g_statusCache = r;
    g_statusCacheMs = NowMs();
    g_statusCacheValid = true;
}

OUString membershipBaseUrl()
{
    const ModelRoutingSnapshot r = loadModelRoutingSnapshot();
    OUString base = r.baseUrl;
    if (base.isEmpty())
        base = u"https://api.03122.com"_ustr;
    while (base.endsWith(u"/"))
        base = base.copy(0, base.getLength() - 1);
    return base;
}

std::string curlJson(const OUString& rUrl, const OUString& rMethod, const OUString& rBodyJson)
{
    const OUString key = OpenAICompatibleAdapter::apiKeyFromEnv();
    if (key.isEmpty())
        return {};

    char outPath[] = "/tmp/kqoffice-mbr-XXXXXX";
    char hdrPath[] = "/tmp/kqoffice-mbr-hdr-XXXXXX";
    char bodyPath[] = "/tmp/kqoffice-mbr-body-XXXXXX";
    const int outFd = ::mkstemp(outPath);
    const int hdrFd = ::mkstemp(hdrPath);
    const int bodyFd = ::mkstemp(bodyPath);
    if (outFd < 0 || hdrFd < 0 || bodyFd < 0)
    {
        if (outFd >= 0)
            ::close(outFd);
        if (hdrFd >= 0)
            ::close(hdrFd);
        if (bodyFd >= 0)
            ::close(bodyFd);
        return {};
    }
    ::close(outFd);
    {
        OStringBuffer hb;
        hb.append("Authorization: Bearer ");
        hb.append(OUStringToOString(key, RTL_TEXTENCODING_UTF8));
        hb.append("\r\nAccept: application/json\r\n");
        if (!rBodyJson.isEmpty())
            hb.append("Content-Type: application/json\r\n");
        const OString hdr = hb.makeStringAndClear();
        (void)::write(hdrFd, hdr.getStr(), static_cast<size_t>(hdr.getLength()));
        ::close(hdrFd);
    }
    if (!rBodyJson.isEmpty())
    {
        const OString b = OUStringToOString(rBodyJson, RTL_TEXTENCODING_UTF8);
        (void)::write(bodyFd, b.getStr(), static_cast<size_t>(b.getLength()));
    }
    ::close(bodyFd);

    OStringBuffer cmd;
    cmd.append("curl -sS --http1.1 --max-time 15 -X ");
    cmd.append(OUStringToOString(rMethod, RTL_TEXTENCODING_ASCII_US));
    cmd.append(" -H @");
    cmd.append(hdrPath);
    if (!rBodyJson.isEmpty())
    {
        cmd.append(" --data-binary @");
        cmd.append(bodyPath);
    }
    cmd.append(" -o ");
    cmd.append(outPath);
    cmd.append(" '");
    cmd.append(OUStringToOString(rUrl, RTL_TEXTENCODING_UTF8));
    cmd.append("' 2>/dev/null");
    (void)::system(cmd.makeStringAndClear().getStr());

    std::string body;
    {
        std::ifstream in(outPath, std::ios::binary);
        if (in)
            body.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    ::unlink(outPath);
    ::unlink(hdrPath);
    ::unlink(bodyPath);
    return body;
}

OUString jsonStr(const std::string& body, const char* key)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t p = body.find(k);
    if (p == std::string::npos)
        return OUString();
    p = body.find(':', p + k.size());
    if (p == std::string::npos)
        return OUString();
    p = body.find('"', p + 1);
    if (p == std::string::npos)
        return OUString();
    size_t q = body.find('"', p + 1);
    if (q == std::string::npos || q <= p)
        return OUString();
    return OStringToOUString(OString(body.data() + p + 1, static_cast<sal_Int32>(q - p - 1)),
                             RTL_TEXTENCODING_UTF8);
}

sal_Int32 jsonNum(const std::string& body, const char* key)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t p = body.find(k);
    if (p == std::string::npos)
        return -1;
    p = body.find(':', p + k.size());
    if (p == std::string::npos)
        return -1;
    ++p;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t'))
        ++p;
    try
    {
        return static_cast<sal_Int32>(std::stol(body.substr(p)));
    }
    catch (...)
    {
        return -1;
    }
}

sal_Int32 dayFastRemFromMe(const std::string& body)
{
    size_t day = body.find("\"day\"");
    if (day != std::string::npos)
    {
        size_t fast = body.find("\"fast\"", day);
        if (fast != std::string::npos && fast < day + 400)
        {
            size_t rem = body.find("\"remaining\"", fast);
            if (rem != std::string::npos && rem < fast + 120)
            {
                size_t c = body.find(':', rem);
                if (c != std::string::npos)
                {
                    try
                    {
                        return static_cast<sal_Int32>(std::stol(body.substr(c + 1)));
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
    }
    // api.03122.com /auth/me: usage.fastUsed + usage.fastLimit
    const sal_Int32 used = jsonNum(body, "fastUsed");
    const sal_Int32 limit = jsonNum(body, "fastLimit");
    if (used >= 0 && limit >= 0)
        return limit > used ? (limit - used) : 0;
    // boost endpoint may expose remainingTodayRequests
    const sal_Int32 todayRem = jsonNum(body, "remainingTodayRequests");
    if (todayRem >= 0)
        return todayRem;
    return jsonNum(body, "fastRemaining");
}

sal_Int32 boostPacksFromBody(const std::string& body)
{
    size_t b = body.find("\"boost\"");
    if (b == std::string::npos)
        return jsonNum(body, "packs");
    size_t p = body.find("\"packs\"", b);
    if (p == std::string::npos || p > b + 300)
        return -1;
    size_t c = body.find(':', p);
    if (c == std::string::npos)
        return -1;
    try
    {
        return static_cast<sal_Int32>(std::stol(body.substr(c + 1)));
    }
    catch (...)
    {
        return -1;
    }
}
} // namespace

MembershipBoostResult membershipBoostAction(const OUString& rAction)
{
    MembershipBoostResult r;
    const OUString act = rAction.toAsciiLowerCase().trim();
    const OUString base = membershipBaseUrl();
    if (OpenAICompatibleAdapter::apiKeyFromEnv().isEmpty())
    {
        r.messageZh = u"未登录会员。请先 bin/kqoffice-cloud-login.sh 或网页 Device 授权。"_ustr;
        r.rawCode = u"sign_in_required"_ustr;
        return r;
    }

    const bool isStatus = (act == u"status"_ustr || act == u"me"_ustr || act.isEmpty());
    // Soft network envelope: status/chip reuse process cache (default 45s).
    if (isStatus && TryGetStatusCache(r))
        return r;

    std::string body;
    if (isStatus)
    {
        body = curlJson(base + u"/api/membership/auth/me"_ustr, u"GET"_ustr, OUString());
        if (body.empty())
            body = curlJson(base + u"/api/membership/boost"_ustr, u"GET"_ustr, OUString());
    }
    else if (act == u"checkin"_ustr || act == u"check-in"_ustr || act == u"签到"_ustr)
    {
        InvalidateStatusCache();
        body = curlJson(base + u"/api/membership/boost"_ustr, u"POST"_ustr,
                        u"{\"action\":\"checkin\"}"_ustr);
    }
    else if (act == u"rush"_ustr || act == u"rush_grab"_ustr || act == u"grab"_ustr
             || act == u"抢包"_ustr)
    {
        InvalidateStatusCache();
        body = curlJson(base + u"/api/membership/boost"_ustr, u"POST"_ustr,
                        u"{\"action\":\"rush_grab\"}"_ustr);
    }
    else
    {
        r.messageZh = u"未知动作（支持：status / checkin / rush）"_ustr;
        r.rawCode = u"invalid_action"_ustr;
        return r;
    }

    if (body.empty())
    {
        r.messageZh = u"会员接口无响应。请检查 https://api.03122.com 网络。"_ustr;
        return r;
    }

    r.ok = body.find("\"ok\":true") != std::string::npos || body.find("\"ok\": true") != std::string::npos;
    r.rawCode = jsonStr(body, "code");
    r.email = jsonStr(body, "email");
    r.packs = boostPacksFromBody(body);
    r.dayFastRem = dayFastRemFromMe(body);
    if (r.dayFastRem < 0)
        r.dayFastRem = jsonNum(body, "fastRemaining");

    OUString msg = jsonStr(body, "messageZh");
    if (msg.isEmpty())
        msg = jsonStr(body, "message");
    const sal_Int32 granted = jsonNum(body, "grantedPacks");

    OUStringBuffer out;
    if (act == u"checkin"_ustr || act == u"check-in"_ustr || act == u"签到"_ustr)
    {
        if (r.ok)
        {
            out.append(u"签到成功"_ustr);
            if (granted > 0)
            {
                out.append(u"：+"_ustr);
                out.append(granted);
                out.append(u" 加油包"_ustr);
            }
        }
        else if (msg.indexOf(u"Already") >= 0 || msg.indexOf(u"already") >= 0
                 || msg.indexOf(u"已签到") >= 0
                 || body.find("Already checked in") != std::string::npos
                 || body.find("checkedInToday") != std::string::npos)
        {
            // 409 invalid_payload + English message is expected when re-checkin same day
            r.ok = true; // soft success
            out.append(u"今日已签到"_ustr);
        }
        else
            out.append(u"签到失败"_ustr);
    }
    else if (act == u"rush"_ustr || act == u"rush_grab"_ustr || act == u"grab"_ustr
             || act == u"抢包"_ustr)
    {
        if (r.ok)
        {
            out.append(u"抢包成功"_ustr);
            if (granted > 0)
            {
                out.append(u"：+"_ustr);
                out.append(granted);
                out.append(u" 加油包"_ustr);
            }
        }
        else if (body.find("Already grabbed") != std::string::npos)
        {
            r.ok = true;
            out.append(u"本时段已抢过加油包"_ustr);
        }
        else
            out.append(u"抢包未成功（可能不在时段或已抢完）"_ustr);
    }
    else
    {
        out.append(u"会员状态"_ustr);
    }

    if (!msg.isEmpty() && !r.ok)
    {
        out.append(u"： "_ustr);
        out.append(msg);
    }
    if (r.packs >= 0)
    {
        out.append(u"\n加油包："_ustr);
        out.append(r.packs);
    }
    if (r.dayFastRem >= 0)
    {
        out.append(u" · 今日剩余："_ustr);
        out.append(r.dayFastRem);
    }
    if (!r.email.isEmpty())
    {
        out.append(u"\n账户："_ustr);
        out.append(r.email);
    }
    out.append(u"\n管理：https://www.03122.com/zh-CN/account/"_ustr);
    r.messageZh = out.makeStringAndClear();
    if (isStatus)
        PutStatusCache(r);
    else
    {
        // Mutations change packs/day; next status should re-fetch.
        InvalidateStatusCache();
    }
    return r;
}

OUString membershipQuotaChipZh()
{
    const MembershipBoostResult r = membershipBoostAction(u"status"_ustr);
    if (!r.ok && r.email.isEmpty() && r.packs < 0 && r.dayFastRem < 0)
        return u"会员：未登录或会话无效 · bin/kqoffice-cloud-login.sh"_ustr;
    OUStringBuffer line;
    line.append(u"会员"_ustr);
    if (!r.email.isEmpty())
    {
        OUString em = r.email;
        if (em.getLength() > 22)
            em = em.copy(0, 20) + u"…"_ustr;
        line.append(u" · "_ustr);
        line.append(em);
    }
    if (r.dayFastRem >= 0)
    {
        line.append(u" · 今日剩 "_ustr);
        line.append(r.dayFastRem);
    }
    if (r.packs >= 0)
    {
        line.append(u" · 加油包 "_ustr);
        line.append(r.packs);
    }
    return line.makeStringAndClear();
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
