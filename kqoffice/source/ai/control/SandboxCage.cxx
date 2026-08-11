/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: cage canaries).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SandboxCage.hxx"

#include "AiPaths.hxx"
#include "ExternalWriteDenier.hxx"
#include "PermissionCenter.hxx"
#include "WritebackPermission.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>

namespace kqoffice::ai::control
{

namespace
{
bool envTruthy(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return false;
    const OString s(v);
    return s.equalsIgnoreAsciiCase("1") || s.equalsIgnoreAsciiCase("true")
           || s.equalsIgnoreAsciiCase("yes") || s.equalsIgnoreAsciiCase("on");
}

bool pathLooksWritable(const OUString& systemPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return false;
    // Try create a unique probe file
    const OUString probe = kqofficePathJoin(systemPath, u".kq-cage-probe"_ustr);
    OUString probeUrl;
    if (osl::FileBase::getFileURLFromSystemPath(probe, probeUrl) != osl::FileBase::E_None)
        return false;
    osl::File f(probeUrl);
    if (f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create) != osl::FileBase::E_None)
        return false;
    f.close();
    (void)osl::File::remove(probeUrl);
    return true;
}

bool fileExistsSystem(const OUString& systemPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    return osl::DirectoryItem::get(url, item) == osl::FileBase::E_None;
}

CanaryCheck checkSshNotWritable()
{
    CanaryCheck c;
    c.id = u"ssh-not-writable"_ustr;
    c.titleZh = u"禁止写入 ~/.ssh"_ustr;
    const OUString home = kqofficeUserHomeDir();
    if (home.isEmpty())
    {
        c.result = CanaryResult::Skip;
        c.detailZh = u"无 HOME"_ustr;
        return c;
    }
    const OUString ssh = kqofficePathJoin(home, u".ssh"_ustr);
    if (!fileExistsSystem(ssh))
    {
        // Directory missing is OK — we must not create it as writable probe for cage.
        // Attempt write into a non-existent path should fail.
        c.result = CanaryResult::Pass;
        c.detailZh = u".ssh 不存在（未创建探测文件）"_ustr;
        return c;
    }
    if (pathLooksWritable(ssh))
    {
        c.result = CanaryResult::Fail;
        c.detailZh = u"可写入 ~/.ssh · 沙箱/权限配置危险"_ustr;
        return c;
    }
    c.result = CanaryResult::Pass;
    c.detailZh = u"无法写入 ~/.ssh（期望）"_ustr;
    return c;
}

CanaryCheck checkConfigWritable()
{
    CanaryCheck c;
    c.id = u"config-writable"_ustr;
    c.titleZh = u"可圈配置目录可写"_ustr;
    const OUString cfg = kqofficeAiConfigDir();
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(cfg, url) == osl::FileBase::E_None)
        (void)osl::Directory::createPath(url);
    if (!pathLooksWritable(cfg))
    {
        c.result = CanaryResult::Fail;
        c.detailZh = u"无法写入配置目录 · AI 会话/账本会失败"_ustr;
        return c;
    }
    c.result = CanaryResult::Pass;
    c.detailZh = cfg;
    return c;
}

CanaryCheck checkTempWritable()
{
    CanaryCheck c;
    c.id = u"temp-writable"_ustr;
    c.titleZh = u"临时目录可写"_ustr;
    const OUString tmp = kqofficeTempDir();
    if (!pathLooksWritable(tmp))
    {
        c.result = CanaryResult::Fail;
        c.detailZh = u"无法写入临时目录"_ustr;
        return c;
    }
    c.result = CanaryResult::Pass;
    c.detailZh = tmp;
    return c;
}

CanaryCheck checkHomeNotAutoAuthorized()
{
    CanaryCheck c;
    c.id = u"home-not-auto-auth"_ustr;
    c.titleZh = u"家目录非默认授权"_ustr;
    PermissionCenter pc;
    pc.load();
    const OUString home = kqofficeUserHomeDir();
    if (home.isEmpty())
    {
        c.result = CanaryResult::Skip;
        c.detailZh = u"无 HOME"_ustr;
        return c;
    }
    // Fresh center without grants should not authorize home — but load() may have vault.
    // Policy: home itself must not be an authorized root unless user granted exactly home
    // which is discouraged. We fail only if root path equals home and recursive grant
    // covers everything — still soft: if home is authorized, warn Fail for product policy.
    if (pc.isPathAuthorized(home))
    {
        // Check if any authorized root is exactly home or /
        bool fullHome = false;
        for (const auto& d : pc.authorizedDirectories())
        {
            if (d.path == home || d.path == u"/"_ustr)
            {
                fullHome = true;
                break;
            }
        }
        if (fullHome)
        {
            c.result = CanaryResult::Fail;
            c.detailZh = u"已授权整个家目录或 / · 违反资料盘最小授权"_ustr;
            return c;
        }
    }
    c.result = CanaryResult::Pass;
    c.detailZh = u"未将 $HOME 整树作为默认授权根"_ustr;
    return c;
}

CanaryCheck checkDenierSelfTest()
{
    CanaryCheck c;
    c.id = u"denier-self-test"_ustr;
    c.titleZh = u"外写拒绝器自检"_ustr;
    const auto push = ExternalWriteDenier::evaluateCommandLine(u"git push"_ustr);
    const auto status = ExternalWriteDenier::evaluateCommandLine(u"git status"_ustr);
    if (push.allowed || !status.allowed)
    {
        c.result = CanaryResult::Fail;
        c.detailZh = u"denier 逻辑异常（push 应拒 / status 应放行）"_ustr;
        return c;
    }
    c.result = CanaryResult::Pass;
    c.detailZh = u"git push 拒 · git status 放行"_ustr;
    return c;
}

CanaryCheck checkWritebackDefaultAsk()
{
    CanaryCheck c;
    c.id = u"writeback-default-ask"_ustr;
    c.titleZh = u"写回默认 Ask"_ustr;
    if (WritebackPermission::defaultTier() != WritebackTier::Ask)
    {
        c.result = CanaryResult::Fail;
        c.detailZh = u"默认档位不是 Ask"_ustr;
        return c;
    }
    // YOLO must not be product default; env may enable for tests
    c.result = CanaryResult::Pass;
    c.detailZh = WritebackPermission::yoloEnabled()
                     ? u"Ask 默认 · 注意 YOLO env 已开（仅测试/托管）"_ustr
                     : u"Ask 默认 · YOLO 关"_ustr;
    return c;
}

CanaryCheck checkSecretsDirNotWorld()
{
    CanaryCheck c;
    c.id = u"secrets-not-in-tmp-open"_ustr;
    c.titleZh = u"密钥不落公开 tmp 明文策略"_ustr;
    // Product rule: membership tokens live under config, not world-readable probe.
    // We only assert config dir exists/creatable and is not /tmp itself as sole store.
    const OUString cfg = kqofficeAiConfigDir();
    if (cfg.isEmpty())
    {
        c.result = CanaryResult::Fail;
        c.detailZh = u"配置路径为空"_ustr;
        return c;
    }
    if (cfg == u"/tmp"_ustr || cfg == u"/var/tmp"_ustr)
    {
        c.result = CanaryResult::Fail;
        c.detailZh = u"配置根不应等于系统 tmp"_ustr;
        return c;
    }
    c.result = CanaryResult::Pass;
    c.detailZh = u"配置根与系统 tmp 分离"_ustr;
    return c;
}
} // namespace

bool SandboxCage::bypassEnabled()
{
    return envTruthy("KQOFFICE_AI_CAGE_BYPASS");
}

OUString SandboxCage::resultId(CanaryResult r)
{
    switch (r)
    {
        case CanaryResult::Pass:
            return u"pass"_ustr;
        case CanaryResult::Fail:
            return u"fail"_ustr;
        case CanaryResult::Skip:
            return u"skip"_ustr;
    }
    return u"pass"_ustr;
}

OUString SandboxCage::policyHintZh()
{
    return u"AI 开跑前 canary：配置/临时可写、~/.ssh 不可写、家目录非整盘授权、"
           "外写 denier 自检、写回默认 Ask。失败则拒绝开跑（可用 KQOFFICE_AI_CAGE_BYPASS=1 仅测试绕过）。"_ustr;
}

CanaryCheck SandboxCage::runOne(const OUString& canaryId)
{
    if (canaryId == u"ssh-not-writable"_ustr)
        return checkSshNotWritable();
    if (canaryId == u"config-writable"_ustr)
        return checkConfigWritable();
    if (canaryId == u"temp-writable"_ustr)
        return checkTempWritable();
    if (canaryId == u"home-not-auto-auth"_ustr)
        return checkHomeNotAutoAuthorized();
    if (canaryId == u"denier-self-test"_ustr)
        return checkDenierSelfTest();
    if (canaryId == u"writeback-default-ask"_ustr)
        return checkWritebackDefaultAsk();
    if (canaryId == u"secrets-not-in-tmp-open"_ustr)
        return checkSecretsDirNotWorld();
    CanaryCheck c;
    c.id = canaryId;
    c.titleZh = u"未知 canary"_ustr;
    c.result = CanaryResult::Skip;
    c.detailZh = u"id 未注册"_ustr;
    return c;
}

CageReport SandboxCage::runCanaries()
{
    CageReport rep;
    const char* ids[] = { "ssh-not-writable",       "config-writable", "temp-writable",
                          "home-not-auto-auth",     "denier-self-test", "writeback-default-ask",
                          "secrets-not-in-tmp-open" };
    for (const char* id : ids)
        rep.checks.push_back(runOne(OUString::createFromAscii(id)));

    sal_Int32 nFail = 0;
    sal_Int32 nPass = 0;
    sal_Int32 nSkip = 0;
    for (const auto& c : rep.checks)
    {
        if (c.result == CanaryResult::Fail)
        {
            ++nFail;
            if (rep.failedCanaryId.isEmpty())
                rep.failedCanaryId = c.id;
        }
        else if (c.result == CanaryResult::Pass)
            ++nPass;
        else
            ++nSkip;
    }
    rep.mayStartAiRun = (nFail == 0) || bypassEnabled();
    OUStringBuffer s;
    s.append(u"沙箱 canary · 通过 "_ustr);
    s.append(OUString::number(nPass));
    s.append(u" · 失败 "_ustr);
    s.append(OUString::number(nFail));
    s.append(u" · 跳过 "_ustr);
    s.append(OUString::number(nSkip));
    if (nFail > 0)
    {
        s.append(u" · 首败="_ustr);
        s.append(rep.failedCanaryId);
        if (bypassEnabled())
            s.append(u" · BYPASS"_ustr);
        else
            s.append(u" · 拒绝开跑"_ustr);
    }
    else
        s.append(u" · 可开跑"_ustr);
    rep.summaryZh = s.makeStringAndClear();
    return rep;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
