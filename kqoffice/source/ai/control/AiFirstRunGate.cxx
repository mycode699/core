/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: first-run onboarding gate).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AiFirstRunGate.hxx"

#include "AiPaths.hxx"
#include "WritebackPermission.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>
#include <vector>

namespace kqoffice::ai::control
{

namespace
{
OUString& testRoot()
{
    static OUString s;
    return s;
}

OUString storePath()
{
    if (!testRoot().isEmpty())
        return kqofficePathJoin(testRoot(), u"ai-first-run.txt"_ustr);
    const char* env = std::getenv("KQOFFICE_AI_FIRSTRUN_DIR");
    if (env && *env)
        return kqofficePathJoin(OUString::createFromAscii(env), u"ai-first-run.txt"_ustr);
    return kqofficePathJoin(kqofficeAiConfigDir(), u"ai-first-run.txt"_ustr);
}

bool writeState(FirstRunState s)
{
    const OUString path = storePath();
    const OUString parent = kqofficeParentDir(path);
    OUString parentUrl;
    if (osl::FileBase::getFileURLFromSystemPath(parent, parentUrl) == osl::FileBase::E_None)
        (void)osl::Directory::createPath(parentUrl);

    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create) != osl::FileBase::E_None)
    {
        if (f.open(osl_File_OpenFlag_Write) != osl::FileBase::E_None)
            return false;
        f.setSize(0);
    }
    OUString body = u"v1\n"_ustr;
    switch (s)
    {
        case FirstRunState::Skipped:
            body += u"skipped\n"_ustr;
            break;
        case FirstRunState::Completed:
            body += u"completed\n"_ustr;
            break;
        case FirstRunState::Pending:
            body += u"pending\n"_ustr;
            break;
        default:
            body += u"unknown\n"_ustr;
            break;
    }
    const OString utf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);
    sal_uInt64 written = 0;
    const auto rc = f.write(utf8.getStr(), static_cast<sal_uInt64>(utf8.getLength()), written);
    f.close();
    return rc == osl::FileBase::E_None;
}

FirstRunState readState()
{
    const OUString path = storePath();
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return FirstRunState::Pending;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return FirstRunState::Pending;
    sal_uInt64 size = 0;
    f.getSize(size);
    if (size == 0 || size > 4096)
    {
        f.close();
        return FirstRunState::Pending;
    }
    std::vector<char> buf(static_cast<size_t>(size) + 1, 0);
    sal_uInt64 read = 0;
    f.read(buf.data(), size, read);
    f.close();
    const OString raw(buf.data(), static_cast<sal_Int32>(read));
    const OUString text = OStringToOUString(raw, RTL_TEXTENCODING_UTF8);
    if (text.indexOf(u"skipped") >= 0)
        return FirstRunState::Skipped;
    if (text.indexOf(u"completed") >= 0)
        return FirstRunState::Completed;
    if (text.indexOf(u"pending") >= 0)
        return FirstRunState::Pending;
    return FirstRunState::Pending;
}
} // namespace

void AiFirstRunGate::setRootDirForTests(const OUString& rootDir)
{
    testRoot() = rootDir;
}

FirstRunState AiFirstRunGate::state()
{
    return readState();
}

bool AiFirstRunGate::shouldShowWelcome()
{
    const FirstRunState s = state();
    return s == FirstRunState::Pending || s == FirstRunState::Unknown;
}

bool AiFirstRunGate::markSkipped()
{
    return writeState(FirstRunState::Skipped);
}

bool AiFirstRunGate::markCompleted()
{
    return writeState(FirstRunState::Completed);
}

bool AiFirstRunGate::resetForTests()
{
    return writeState(FirstRunState::Pending);
}

OUString AiFirstRunGate::stateLabelZh(FirstRunState s)
{
    switch (s)
    {
        case FirstRunState::Pending:
            return u"待引导"_ustr;
        case FirstRunState::Skipped:
            return u"已跳过"_ustr;
        case FirstRunState::Completed:
            return u"已完成"_ustr;
        default:
            return u"未知"_ustr;
    }
}

OUString AiFirstRunGate::storePathForDisplay()
{
    return storePath();
}

OUString AiFirstRunGate::welcomeMarkdownZh()
{
    OUStringBuffer b;
    b.append(u"## 欢迎使用可圈 AI\n\n"_ustr);
    b.append(u"可随时跳过；**不登录也能继续编辑文档**。\n\n"_ustr);
    b.append(u"### 30 秒上手\n"_ustr);
    b.append(u"1. 选中文字/单元格 → 侧栏输入改写或点意图芯片\n"_ustr);
    b.append(u"2. 查看建议 → **批准写回**（"_ustr);
    b.append(WritebackPermission::policyHintZh());
    b.append(u"）\n"_ustr);
    b.append(u"3. `/资料盘` 管理本地材料 · `/诊断导出` 打包脱敏日志\n\n"_ustr);
    b.append(u"### 命令\n"_ustr);
    b.append(u"- `/跳过引导` — 不再显示本卡片\n"_ustr);
    b.append(u"- `/完成引导` — 标记已完成\n"_ustr);
    b.append(u"- `/写回策略` · `/会员额度` · `/诊断导出`\n\n"_ustr);
    b.append(u"_本地优先 · 主文档默认不改 · 可撤销写回_\n"_ustr);
    return b.makeStringAndClear();
}

FirstRunSnapshot AiFirstRunGate::snapshot()
{
    FirstRunSnapshot snap;
    snap.state = state();
    snap.shouldShowWelcome = shouldShowWelcome();
    snap.welcomeMarkdownZh = welcomeMarkdownZh();
    snap.statusLineZh = u"引导："_ustr + stateLabelZh(snap.state);
    return snap;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
