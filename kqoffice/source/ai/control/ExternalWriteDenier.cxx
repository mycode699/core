/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: external-write denier).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ExternalWriteDenier.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::control
{

namespace
{
OUString lower(const OUString& s) { return s.toAsciiLowerCase(); }

OUString basenameOf(const OUString& program)
{
    OUString p = program;
    sal_Int32 slash = p.lastIndexOf('/');
    const sal_Int32 bslash = p.lastIndexOf('\\');
    if (bslash > slash)
        slash = bslash;
    if (slash >= 0 && slash + 1 < p.getLength())
        p = p.copy(slash + 1);
    if (p.getLength() > 4)
    {
        const OUString tail = lower(p.copy(p.getLength() - 4));
        if (tail == u".exe"_ustr)
            p = p.copy(0, p.getLength() - 4);
    }
    return lower(p);
}

bool hasArg(const std::vector<OUString>& args, const OUString& needle)
{
    const OUString n = lower(needle);
    for (const auto& a : args)
    {
        if (lower(a) == n)
            return true;
    }
    return false;
}

bool hasArgPrefix(const std::vector<OUString>& args, const OUString& prefix)
{
    const OUString p = lower(prefix);
    for (const auto& a : args)
    {
        if (lower(a).startsWith(p))
            return true;
    }
    return false;
}

DenyDecision deny(const OUString& code, const OUString& rule, const OUString& zh)
{
    DenyDecision d;
    d.allowed = false;
    d.reasonCode = code;
    d.matchedRule = rule;
    d.reasonZh = zh;
    return d;
}

DenyDecision allow()
{
    DenyDecision d;
    d.allowed = true;
    return d;
}
} // namespace

DenyDecision ExternalWriteDenier::evaluate(const OUString& program,
                                           const std::vector<OUString>& args)
{
    const OUString base = basenameOf(program);
    if (base.isEmpty())
        return allow();

    // git: block irreversible remote writes; allow local + clone/fetch
    if (base == u"git"_ustr)
    {
        if (hasArg(args, u"push"_ustr))
        {
            if (hasArg(args, u"--force"_ustr) || hasArg(args, u"-f"_ustr)
                || hasArgPrefix(args, u"--force-"_ustr))
                return deny(u"git-force-push"_ustr, u"git push --force"_ustr,
                            u"拒绝强制推送 · 不可撤销的远端写"_ustr);
            return deny(u"git-push"_ustr, u"git push"_ustr,
                        u"拒绝 git push · V1 不自动外发分支（可手工）"_ustr);
        }
        if (hasArg(args, u"publish"_ustr))
            return deny(u"git-publish"_ustr, u"git publish"_ustr, u"拒绝 git publish"_ustr);
        // local ok: status, commit, add, clone, fetch, checkout, diff, log
        return allow();
    }

    if (base == u"gh"_ustr || base == u"hub"_ustr)
    {
        if (hasArg(args, u"pr"_ustr)
            && (hasArg(args, u"create"_ustr) || hasArg(args, u"merge"_ustr)))
            return deny(u"gh-pr-write"_ustr, u"gh pr"_ustr, u"拒绝自动创建/合并 PR"_ustr);
        if (hasArg(args, u"release"_ustr) && hasArg(args, u"create"_ustr))
            return deny(u"gh-release"_ustr, u"gh release create"_ustr, u"拒绝自动发 Release"_ustr);
    }

    if (base == u"npm"_ustr || base == u"pnpm"_ustr || base == u"yarn"_ustr || base == u"bun"_ustr)
    {
        if (hasArg(args, u"publish"_ustr))
            return deny(u"npm-publish"_ustr, u"npm publish"_ustr, u"拒绝 npm/pnpm publish"_ustr);
    }

    if (base == u"docker"_ustr || base == u"podman"_ustr)
    {
        if (hasArg(args, u"push"_ustr))
            return deny(u"docker-push"_ustr, u"docker push"_ustr, u"拒绝镜像 push"_ustr);
    }

    if (base == u"curl"_ustr || base == u"wget"_ustr)
    {
        // Mutating HTTP methods
        if (hasArg(args, u"-X"_ustr) || hasArg(args, u"--request"_ustr))
        {
            for (size_t i = 0; i + 1 < args.size(); ++i)
            {
                const OUString a = lower(args[static_cast<sal_Int32>(i)]);
                if (a == u"-x"_ustr || a == u"--request"_ustr)
                {
                    const OUString m = lower(args[static_cast<sal_Int32>(i + 1)]);
                    if (m == u"post"_ustr || m == u"put"_ustr || m == u"patch"_ustr
                        || m == u"delete"_ustr)
                        return deny(u"curl-mutate"_ustr, u"curl -X mutate"_ustr,
                                    u"拒绝 curl/wget 变更方法外发"_ustr);
                }
            }
        }
        if (hasArgPrefix(args, u"-d"_ustr) || hasArg(args, u"--data"_ustr)
            || hasArg(args, u"--data-raw"_ustr) || hasArg(args, u"--data-binary"_ustr)
            || hasArg(args, u"-F"_ustr) || hasArg(args, u"--form"_ustr)
            || hasArg(args, u"--upload-file"_ustr) || hasArg(args, u"-T"_ustr))
            return deny(u"curl-body"_ustr, u"curl body"_ustr, u"拒绝带 body 的外发请求"_ustr);
        // plain GET/HEAD ok
        return allow();
    }

    if (base == u"scp"_ustr || base == u"rsync"_ustr)
    {
        // Heuristic: remote target contains :
        for (const auto& a : args)
        {
            if (a.indexOf(u':') > 0 && !a.startsWith(u"/"_ustr))
                return deny(u"scp-remote"_ustr, base, u"拒绝 scp/rsync 远端写入"_ustr);
        }
    }

    if (base == u"kubectl"_ustr || base == u"helm"_ustr)
    {
        if (hasArg(args, u"apply"_ustr) || hasArg(args, u"create"_ustr)
            || hasArg(args, u"delete"_ustr) || hasArg(args, u"upgrade"_ustr)
            || hasArg(args, u"install"_ustr))
            return deny(u"k8s-mutate"_ustr, base, u"拒绝集群变更命令"_ustr);
    }

    return allow();
}

DenyDecision ExternalWriteDenier::evaluateCommandLine(const OUString& commandLine)
{
    const OUString t = commandLine.trim();
    if (t.isEmpty())
        return allow();
    std::vector<OUString> parts;
    OUStringBuffer cur;
    bool inQ = false;
    for (sal_Int32 i = 0; i < t.getLength(); ++i)
    {
        const sal_Unicode c = t[i];
        if (c == '"')
        {
            inQ = !inQ;
            continue;
        }
        if (!inQ && (c == ' ' || c == '\t'))
        {
            if (!cur.isEmpty())
            {
                parts.push_back(cur.makeStringAndClear());
            }
            continue;
        }
        cur.append(c);
    }
    if (!cur.isEmpty())
        parts.push_back(cur.makeStringAndClear());
    if (parts.empty())
        return allow();
    const OUString prog = parts.front();
    std::vector<OUString> args(parts.begin() + 1, parts.end());
    return evaluate(prog, args);
}

DenyDecision ExternalWriteDenier::evaluateAction(const OUString& actionName)
{
    const OUString a = lower(actionName);
    if (a.isEmpty())
        return allow();
    if (a.indexOf(u"force-push"_ustr) >= 0 || a.indexOf(u"force_push"_ustr) >= 0)
        return deny(u"action-force-push"_ustr, actionName, u"拒绝强制推送动作"_ustr);
    if (a.indexOf(u"git-push"_ustr) >= 0 || a == u"push"_ustr || a == u"push_branch"_ustr
        || a.indexOf(u"push-branch"_ustr) >= 0)
        return deny(u"action-push"_ustr, actionName, u"拒绝自动推送分支"_ustr);
    if (a.indexOf(u"publish"_ustr) >= 0 || a.indexOf(u"deploy"_ustr) >= 0
        || a.indexOf(u"release"_ustr) >= 0 || a.indexOf(u"npm-publish"_ustr) >= 0)
        return deny(u"action-publish"_ustr, actionName, u"拒绝发布/部署动作"_ustr);
    if (a.indexOf(u"open-pr"_ustr) >= 0 || a.indexOf(u"create_pr"_ustr) >= 0
        || a.indexOf(u"create-pr"_ustr) >= 0 || a.indexOf(u"merge-pr"_ustr) >= 0)
        return deny(u"action-pr"_ustr, actionName, u"拒绝自动 PR 动作"_ustr);
    // Local document apply is not external write
    if (a.indexOf(u"apply"_ustr) >= 0 || a.indexOf(u"writeback"_ustr) >= 0)
        return allow();
    return allow();
}

OUString ExternalWriteDenier::policyHintZh()
{
    return u"外写拒绝：git push/force、npm publish、docker push、变更类 curl、集群 apply。"
           "本地 git commit/clone 与文档写回（人批）允许。"_ustr;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
