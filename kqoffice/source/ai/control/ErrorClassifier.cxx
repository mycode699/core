/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: error decks).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ErrorClassifier.hxx"

#include <initializer_list>

namespace kqoffice::ai::control
{

namespace
{

bool containsCi(const OUString& hay, const OUString& needle)
{
    return hay.toAsciiLowerCase().indexOf(needle.toAsciiLowerCase()) >= 0;
}

bool containsAny(const OUString& hay, std::initializer_list<OUString> needles)
{
    const OUString lower = hay.toAsciiLowerCase();
    for (const OUString& n : needles)
    {
        if (lower.indexOf(n.toAsciiLowerCase()) >= 0)
            return true;
    }
    return false;
}

} // namespace

OUString ErrorClassifier::redact(const OUString& text)
{
    if (text.isEmpty())
        return {};
    OUString out = text;
    // Bearer / api keys / sk- / long hex tokens — replace with placeholder.
    // Keep simple and deterministic for unit tests (not a full secret scanner).
    auto maskPattern = [&out](const OUString& prefix, sal_Int32 minRest) {
        sal_Int32 pos = 0;
        while ((pos = out.indexOf(prefix, pos)) >= 0)
        {
            sal_Int32 end = pos + prefix.getLength();
            while (end < out.getLength())
            {
                const sal_Unicode c = out[end];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '-' || c == '_' || c == '.')
                    ++end;
                else
                    break;
            }
            if (end - pos - prefix.getLength() >= minRest)
            {
                const OUString replacement = prefix + u"[REDACTED]"_ustr;
                out = out.replaceAt(pos, end - pos, replacement);
                pos += replacement.getLength();
            }
            else
            {
                ++pos;
            }
        }
    };
    maskPattern(u"sk-"_ustr, 8);
    maskPattern(u"Bearer "_ustr, 8);
    maskPattern(u"bearer "_ustr, 8);
    maskPattern(u"api_key="_ustr, 6);
    maskPattern(u"apiKey="_ustr, 6);
    maskPattern(u"token="_ustr, 8);
    maskPattern(u"Authorization: "_ustr, 8);
    if (out.getLength() > 400)
        out = out.copy(0, 400) + u"…"_ustr;
    return out;
}

OUString ErrorClassifier::deckId(ErrorDeck deck)
{
    switch (deck)
    {
        case ErrorDeck::Auth:
            return u"auth"_ustr;
        case ErrorDeck::Network:
            return u"network"_ustr;
        case ErrorDeck::Provider:
            return u"provider"_ustr;
        case ErrorDeck::Apply:
            return u"apply"_ustr;
        case ErrorDeck::Permission:
            return u"permission"_ustr;
        case ErrorDeck::Resource:
            return u"resource"_ustr;
        case ErrorDeck::Crash:
            return u"crash"_ustr;
        case ErrorDeck::Unknown:
            return u"unknown"_ustr;
    }
    return u"unknown"_ustr;
}

OUString ErrorClassifier::deckLabelZh(ErrorDeck deck)
{
    switch (deck)
    {
        case ErrorDeck::Auth:
            return u"登录与会员"_ustr;
        case ErrorDeck::Network:
            return u"网络"_ustr;
        case ErrorDeck::Provider:
            return u"模型服务"_ustr;
        case ErrorDeck::Apply:
            return u"文档写回"_ustr;
        case ErrorDeck::Permission:
            return u"权限与确认"_ustr;
        case ErrorDeck::Resource:
            return u"资源与负载"_ustr;
        case ErrorDeck::Crash:
            return u"稳定性"_ustr;
        case ErrorDeck::Unknown:
            return u"其他"_ustr;
    }
    return u"其他"_ustr;
}

ClassifiedError ErrorClassifier::classify(const OUString& message)
{
    return classify(message, OUString());
}

ClassifiedError ErrorClassifier::classify(const OUString& message, const OUString& errorCodeHint)
{
    ClassifiedError out;
    const OUString msg = message;
    const OUString hint = errorCodeHint;
    const OUString redacted = redact(msg);
    out.detailZh = redacted;

    auto finish = [&](ErrorDeck deck, const OUString& code, const OUString& title,
                      const OUString& action, bool retriable) {
        out.deck = deck;
        out.code = code;
        out.titleZh = title;
        out.actionZh = action;
        out.retriable = retriable;
        return out;
    };

    // Hint first (stable machine codes from our own stack).
    if (!hint.isEmpty())
    {
        if (hint == u"human-approval-required"_ustr || hint.startsWith(u"writeback-denied")
            || containsCi(hint, u"permission"_ustr))
            return finish(ErrorDeck::Permission, hint, u"需要确认后才能改文档"_ustr,
                          u"在侧栏选择「仅本次」或「本轮允许」"_ustr, false);
        if (hint.indexOf(u"stale") >= 0 || hint.startsWith(u"apply-")
            || hint == u"invalid-plan"_ustr || hint == u"pre-verify-failed"_ustr)
            return finish(ErrorDeck::Apply, hint, u"写回未执行"_ustr,
                          u"重新生成计划后再批准"_ustr, true);
        if (hint.indexOf(u"401") >= 0 || containsCi(hint, u"auth"_ustr)
            || containsCi(hint, u"membership"_ustr))
            return finish(ErrorDeck::Auth, hint, u"登录或密钥无效"_ustr,
                          u"重新登录会员或检查 API Key"_ustr, true);
    }

    if (containsAny(msg, { u"human-approval-required"_ustr, u"需要确认"_ustr, u"拒绝写回"_ustr,
                           u"permission denied"_ustr, u"未授权"_ustr, u"path not authorized"_ustr,
                           u"isPathAuthorized"_ustr }))
        return finish(ErrorDeck::Permission, u"permission-required"_ustr,
                      u"需要确认或目录授权"_ustr, u"批准写回，或在资料盘设置中授权目录"_ustr,
                      false);

    if (containsAny(msg, { u"stale-document"_ustr, u"stale"_ustr, u"文档已变更"_ustr,
                           u"写回失败"_ustr, u"apply-blocked"_ustr, u"invalid-plan"_ustr,
                           u"No current document"_ustr }))
        return finish(ErrorDeck::Apply, u"apply-failed"_ustr, u"文档写回失败"_ustr,
                      u"确认选区与文档未改动后重试"_ustr, true);

    if (containsAny(msg, { u"401"_ustr, u"403"_ustr, u"unauthorized"_ustr,
                           u"invalid api key"_ustr, u"api key"_ustr, u"login"_ustr, u"登录"_ustr,
                           u"会员"_ustr, u"membership"_ustr, u"token expired"_ustr }))
        return finish(ErrorDeck::Auth, u"auth-failed"_ustr, u"登录与会员异常"_ustr,
                      u"重新登录或更新密钥（勿把密钥发到聊天）"_ustr, true);

    if (containsAny(msg, { u"timeout"_ustr, u"timed out"_ustr, u"dns"_ustr, u"econnrefused"_ustr,
                           u"connection refused"_ustr, u"network"_ustr, u"unreachable"_ustr,
                           u"网络"_ustr, u"连接失败"_ustr, u"ssl"_ustr, u"certificate"_ustr }))
        return finish(ErrorDeck::Network, u"network-failed"_ustr, u"网络不可用"_ustr,
                      u"检查网络与网关地址后重试"_ustr, true);

    if (containsAny(msg, { u"rate limit"_ustr, u"429"_ustr, u"model"_ustr, u"ollama"_ustr,
                           u"provider"_ustr, u"gateway"_ustr, u"context length"_ustr,
                           u"max tokens"_ustr, u"模型"_ustr }))
        return finish(ErrorDeck::Provider, u"provider-failed"_ustr, u"模型服务异常"_ustr,
                      u"稍后重试，或切换本地/云端模型"_ustr, true);

    if (containsAny(msg, { u"memory"_ustr, u"rss"_ustr, u"resource envelope"_ustr,
                           u"stream limit"_ustr, u"concurrent"_ustr, u"budget"_ustr, u"资源"_ustr,
                           u"负载过高"_ustr }))
        return finish(ErrorDeck::Resource, u"resource-pressure"_ustr, u"资源紧张"_ustr,
                      u"关闭多余文档或等待后台任务完成"_ustr, true);

    if (containsAny(msg, { u"sigabrt"_ustr, u"segfault"_ustr, u"crash"_ustr, u"lockfile"_ustr,
                           u"already running"_ustr, u"multi-instance"_ustr, u"abort"_ustr }))
        return finish(ErrorDeck::Crash, u"crash-or-lock"_ustr, u"进程异常或实例冲突"_ustr,
                      u"退出多余实例后重启；可导出诊断包"_ustr, false);

    return finish(ErrorDeck::Unknown, u"unknown"_ustr, u"未知错误"_ustr,
                  u"导出诊断包并联系支持（已脱敏）"_ustr, true);
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
