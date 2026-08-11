/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: diagnostic export).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "DiagnosticBundle.hxx"

#include "AiPaths.hxx"
#include "AiResourceEnvelope.hxx"
#include "PermissionCenter.hxx"
#include "SafeRestore.hxx"
#include "WritebackPermission.hxx"
#include "AiFirstRunGate.hxx"
#include "ComposerQueue.hxx"
#include "EventLedger.hxx"
#include "ExternalWriteDenier.hxx"
#include "StallDetector.hxx"
#include "FactRouter.hxx"
#include "PolicyEvolutionGuard.hxx"
#include "ProviderSlotManifest.hxx"
#include "SandboxCage.hxx"
#include "HandoffBrief.hxx"

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdio>

namespace kqoffice::ai::control
{

namespace
{

OUString jsonEscape(const OUString& s)
{
    OUStringBuffer b;
    b.append('"');
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == '"' || c == '\\')
        {
            b.append('\\');
            b.append(c);
        }
        else if (c == '\n')
            b.append("\\n");
        else if (c == '\r')
            b.append("\\r");
        else if (c == '\t')
            b.append("\\t");
        else if (c < 0x20)
            b.append(' ');
        else
            b.append(c);
    }
    b.append('"');
    return b.makeStringAndClear();
}

bool writeTextFile(const OUString& systemPath, const OUString& content)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create) != osl::FileBase::E_None)
    {
        // Try truncate existing
        if (f.open(osl_File_OpenFlag_Write) != osl::FileBase::E_None)
            return false;
        f.setSize(0);
    }
    const OString utf8 = OUStringToOString(content, RTL_TEXTENCODING_UTF8);
    sal_uInt64 written = 0;
    const auto rc = f.write(utf8.getStr(), static_cast<sal_uInt64>(utf8.getLength()), written);
    f.close();
    return rc == osl::FileBase::E_None && written == static_cast<sal_uInt64>(utf8.getLength());
}

bool ensureDir(const OUString& systemPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

sal_Int64 nowMs()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
           + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

} // namespace

OUString DiagnosticBundle::schemaVersion()
{
    return u"kq-diag-1"_ustr;
}

OUString DiagnosticBundle::defaultOutputDir()
{
    const sal_Int64 t = nowMs();
    return kqofficePathJoin(kqofficeTempDir(),
                            u"kqoffice-diag-"_ustr + OUString::number(t));
}

OUString DiagnosticBundle::errorSampleJson(const DiagnosticErrorSample& sample)
{
    const ClassifiedError c = ErrorClassifier::classify(sample.message, sample.errorCodeHint);
    OUStringBuffer b;
    b.append('{');
    b.append(u"\"deck\":"_ustr + jsonEscape(ErrorClassifier::deckId(c.deck)));
    b.append(u",\"code\":"_ustr + jsonEscape(c.code));
    b.append(u",\"titleZh\":"_ustr + jsonEscape(c.titleZh));
    b.append(u",\"actionZh\":"_ustr + jsonEscape(c.actionZh));
    b.append(u",\"retriable\":"_ustr);
    b.append(c.retriable ? u"true"_ustr : u"false"_ustr);
    b.append(u",\"detailZh\":"_ustr + jsonEscape(c.detailZh));
    b.append(u",\"atMs\":"_ustr + OUString::number(sample.atMs));
    b.append('}');
    return b.makeStringAndClear();
}

DiagnosticBundleResult DiagnosticBundle::exportBundle(const DiagnosticBundleRequest& req)
{
    DiagnosticBundleResult out;
    OUString dir = req.outputDir;
    if (dir.isEmpty())
        dir = defaultOutputDir();
    out.outputDir = dir;

    if (!ensureDir(dir))
    {
        out.success = false;
        out.error = u"cannot-create-output-dir"_ustr;
        out.summaryZh = u"无法创建诊断目录"_ustr;
        return out;
    }

    sal_Int32 files = 0;

    // README
    {
        const OUString readme
            = u"# 可圈办公 · 诊断包\n\n"
              "本目录由应用导出，**已脱敏**（无 API Key / 无文档正文）。\n"
              "schema: "_ustr
              + schemaVersion()
              + u"\n\n"
                "- manifest.json — 清单\n"
                "- env.json — 版本与平台（无密钥）\n"
                "- errors.jsonl — 最近错误（分类后）\n"
                "- permissions.txt — 权限策略摘要\n"
                "- resource-envelope.txt — 资源包络\n"
                "- safe-restore-doctor.txt — 会话医生报告（若有）\n"
                "- writeback-policy.txt — 写回/外写/卡顿/Slot 策略\n"
                "- event-ledger.txt — 本地事件账本尾部\n"
                "- cage-canaries.txt — 沙箱 canary\n"
                "- handoff-brief.md — 账本交接简报\n"
                "- notes.txt — 附加说明\n"_ustr;
        if (writeTextFile(kqofficePathJoin(dir, u"README.md"_ustr), readme))
            ++files;
    }

    // env.json
    {
        OUStringBuffer b;
        b.append(u"{\n  \"schemaVersion\": "_ustr);
        b.append(jsonEscape(schemaVersion()));
        b.append(u",\n  \"productVersion\": "_ustr);
        b.append(jsonEscape(ErrorClassifier::redact(req.productVersion)));
        b.append(u",\n  \"buildId\": "_ustr);
        b.append(jsonEscape(ErrorClassifier::redact(req.buildId)));
        b.append(u",\n  \"platformHint\": "_ustr);
        b.append(jsonEscape(req.platformHint));
        b.append(u",\n  \"exportedAtMs\": "_ustr);
        b.append(OUString::number(nowMs()));
        b.append(u",\n  \"writebackYoloEnv\": "_ustr);
        b.append(WritebackPermission::yoloEnabled() ? u"true"_ustr : u"false"_ustr);
        b.append(u"\n}\n"_ustr);
        if (writeTextFile(kqofficePathJoin(dir, u"env.json"_ustr), b.makeStringAndClear()))
            ++files;
    }

    // errors.jsonl
    {
        OUStringBuffer b;
        for (const auto& e : req.recentErrors)
        {
            b.append(errorSampleJson(e));
            b.append('\n');
        }
        if (req.recentErrors.empty())
            b.append(u"{\"deck\":\"unknown\",\"code\":\"none\",\"titleZh\":\"无最近错误\","
                     "\"actionZh\":\"\",\"retriable\":false,\"detailZh\":\"\",\"atMs\":0}\n"_ustr);
        if (writeTextFile(kqofficePathJoin(dir, u"errors.jsonl"_ustr), b.makeStringAndClear()))
            ++files;
    }

    if (req.includePermissionSummary)
    {
        PermissionCenter pc;
        pc.load();
        OUString text = pc.settingsSurfaceSummaryZh();
        text += u"\n\n"_ustr + WritebackPermission::policyHintZh() + u"\n"_ustr;
        if (writeTextFile(kqofficePathJoin(dir, u"permissions.txt"_ustr), text))
            ++files;
    }

    if (req.includeResourceEnvelope)
    {
        const OUString text = AiResourceEnvelope::summaryLineZh() + u"\n"_ustr;
        if (writeTextFile(kqofficePathJoin(dir, u"resource-envelope.txt"_ustr), text))
            ++files;
    }

    if (req.includeSafeRestoreDoctor)
    {
        const OUString doctor = SafeRestore::doctor();
        if (writeTextFile(kqofficePathJoin(dir, u"safe-restore-doctor.txt"_ustr),
                          ErrorClassifier::redact(doctor) + u"\n"_ustr))
            ++files;
    }

    {
        const OUString text
            = WritebackPermission::policyHintZh() + u"\ndefaultTier="_ustr
              + WritebackPermission::tierLabelZh(WritebackPermission::defaultTier())
              + u"\nyoloEnabled="_ustr
              + (WritebackPermission::yoloEnabled() ? u"true"_ustr : u"false"_ustr) + u"\n"_ustr
              + u"firstRun="_ustr
              + AiFirstRunGate::stateLabelZh(AiFirstRunGate::state()) + u"\n"_ustr
              + u"composerQueueDefaultMax="_ustr
              + OUString::number(ComposerQueue::kDefaultMaxItems) + u"\n"_ustr
              + u"externalWrite="_ustr + ExternalWriteDenier::policyHintZh() + u"\n"_ustr
              + u"stall="_ustr + StallDetector::policyHintZh() + u"\n"_ustr
              + u"evolution="_ustr + PolicyEvolutionGuard::policyHintZh() + u"\n"_ustr
              + u"providers="_ustr
              + ProviderSlotRegistry::summaryLineZh(ProviderSlotRegistry::loadAll()) + u"\n"_ustr
              + u"routeDirectMaxChars="_ustr + OUString::number(FactRouter::directMaxChars())
              + u"\n"_ustr;
        if (writeTextFile(kqofficePathJoin(dir, u"writeback-policy.txt"_ustr), text))
            ++files;
    }

    // Event ledger tail (redacted already at append)
    {
        EventLedger ledger;
        const auto evs = ledger.replaySince(0, 40);
        OUStringBuffer b;
        b.append(u"# event ledger tail (persist-before-broadcast)\n"_ustr);
        b.append(u"root="_ustr);
        b.append(ledger.rootDir());
        b.append(u"\nlastSequence="_ustr);
        b.append(OUString::number(ledger.lastSequence()));
        b.append(u"\n"_ustr);
        for (const auto& e : evs)
        {
            b.append(u"#"_ustr);
            b.append(OUString::number(e.sequence));
            b.append(u" "_ustr);
            b.append(e.type);
            b.append(u" "_ustr);
            b.append(e.payload);
            b.append(u"\n"_ustr);
        }
        if (writeTextFile(kqofficePathJoin(dir, u"event-ledger.txt"_ustr), b.makeStringAndClear()))
            ++files;
    }

    {
        const auto cage = SandboxCage::runCanaries();
        OUStringBuffer b;
        b.append(cage.summaryZh);
        b.append(u"\n"_ustr);
        for (const auto& c : cage.checks)
        {
            b.append(c.id);
            b.append(u"\t"_ustr);
            b.append(SandboxCage::resultId(c.result));
            b.append(u"\t"_ustr);
            b.append(c.detailZh);
            b.append(u"\n"_ustr);
        }
        if (writeTextFile(kqofficePathJoin(dir, u"cage-canaries.txt"_ustr), b.makeStringAndClear()))
            ++files;
    }

    {
        const auto brief = HandoffBriefBuilder::fromLedgerTail(25);
        if (writeTextFile(kqofficePathJoin(dir, u"handoff-brief.md"_ustr), brief.markdownZh))
            ++files;
    }

    if (!req.notes.empty())
    {
        OUStringBuffer b;
        for (const auto& n : req.notes)
        {
            b.append(ErrorClassifier::redact(n));
            b.append('\n');
        }
        if (writeTextFile(kqofficePathJoin(dir, u"notes.txt"_ustr), b.makeStringAndClear()))
            ++files;
    }

    // manifest.json
    {
        OUStringBuffer b;
        b.append(u"{\n  \"schemaVersion\": "_ustr);
        b.append(jsonEscape(schemaVersion()));
        b.append(u",\n  \"outputDir\": "_ustr);
        b.append(jsonEscape(dir));
        b.append(u",\n  \"fileCount\": "_ustr);
        b.append(OUString::number(files + 1)); // +1 for manifest itself
        b.append(u",\n  \"redacted\": true,\n  \"containsDocumentBody\": false,\n"
                 "  \"containsApiKeys\": false\n}\n"_ustr);
        const OUString manPath = kqofficePathJoin(dir, u"manifest.json"_ustr);
        if (writeTextFile(manPath, b.makeStringAndClear()))
        {
            ++files;
            out.manifestPath = manPath;
        }
    }

    out.fileCount = files;
    out.success = files >= 3;
    if (out.success)
    {
        out.summaryZh = u"诊断包已导出（已脱敏）· "_ustr + dir + u" · 文件数="_ustr
                        + OUString::number(files);
    }
    else
    {
        out.error = u"incomplete-export"_ustr;
        out.summaryZh = u"诊断包导出不完整"_ustr;
    }
    return out;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
