/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: stall / loop detector).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "StallDetector.hxx"

#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>

namespace kqoffice::ai::control
{

namespace
{
sal_Int32 envInt(const char* name, sal_Int32 def)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return def;
    const sal_Int32 n = OString(v).toInt32();
    return n > 0 ? n : def;
}
} // namespace

sal_Int32 StallDetector::idleUnitsForSoftStall()
{
    return envInt("KQOFFICE_AI_STALL_IDLE_UNITS", 4);
}

sal_Int32 StallDetector::identicalFailsForLoop()
{
    return envInt("KQOFFICE_AI_STALL_FAIL_REPEAT", 2);
}

sal_Int32 StallDetector::readOnlyBurstForSuspect()
{
    return envInt("KQOFFICE_AI_STALL_READONLY_BURST", 12);
}

OUString StallDetector::verdictId(StallVerdict v)
{
    switch (v)
    {
        case StallVerdict::None:
            return u"none"_ustr;
        case StallVerdict::SoftStall:
            return u"soft-stall"_ustr;
        case StallVerdict::LoopSuspect:
            return u"loop-suspect"_ustr;
        case StallVerdict::ReadNoWrite:
            return u"read-no-write"_ustr;
    }
    return u"none"_ustr;
}

OUString StallDetector::normalizeFingerprint(const OUString& action, const OUString& detail)
{
    OUStringBuffer b;
    auto feed = [&](const OUString& s) {
        for (sal_Int32 i = 0; i < s.getLength(); ++i)
        {
            const sal_Unicode c = s[i];
            if (c >= '0' && c <= '9')
                b.append('#');
            else if (c == '/' || c == '\\')
            {
                // keep only after last slash eventually — append marker
                b.append('/');
            }
            else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_'
                     || c == '.' || c == ':' || c == '=')
                b.append(c);
            else if (c == ' ' || c == '\t')
                b.append('_');
        }
    };
    feed(action);
    b.append('|');
    // last path component of detail
    OUString d = detail;
    const sal_Int32 sl1 = d.lastIndexOf('/');
    const sal_Int32 sl2 = d.lastIndexOf('\\');
    const sal_Int32 sl = sl1 > sl2 ? sl1 : sl2;
    if (sl >= 0 && sl + 1 < d.getLength())
        d = d.copy(sl + 1);
    if (d.getLength() > 64)
        d = d.copy(0, 64);
    feed(d);
    OUString out = b.makeStringAndClear();
    if (out.getLength() > 120)
        out = out.copy(0, 120);
    return out;
}

StallDecision StallDetector::evaluate(const std::vector<DetectorEvent>& history)
{
    StallDecision d;
    if (history.empty())
        return d;

    // Work on a suffix of at most 24 events
    const size_t nAll = history.size();
    const size_t start = nAll > 24 ? nAll - 24 : 0;

    // Progress resets
    sal_Int32 idleRun = 0;
    sal_Int32 readOnlyRun = 0;
    bool sawProgressAfter = false;

    // Walk newest-last: scan from start to end
    OUString lastFailFp;
    sal_Int32 identicalFails = 0;
    sal_Int32 lastProgressUnit = -1;

    for (size_t i = start; i < nAll; ++i)
    {
        const DetectorEvent& e = history[i];
        if (e.kind == DetectorSignalKind::Progress || e.kind == DetectorSignalKind::ToolOk)
        {
            lastProgressUnit = e.timeUnit;
            idleRun = 0;
            readOnlyRun = 0;
            identicalFails = 0;
            lastFailFp.clear();
            sawProgressAfter = true;
            (void)sawProgressAfter;
            continue;
        }
        if (e.kind == DetectorSignalKind::IdleTick)
        {
            ++idleRun;
            continue;
        }
        if (e.kind == DetectorSignalKind::ReadOnly)
        {
            ++readOnlyRun;
            continue;
        }
        if (e.kind == DetectorSignalKind::ToolFail)
        {
            const OUString fp = normalizeFingerprint(e.action, e.detail);
            if (!lastFailFp.isEmpty() && fp == lastFailFp)
                ++identicalFails;
            else
            {
                lastFailFp = fp;
                identicalFails = 1;
            }
            // Changing error detail is not a loop (flaky infra) — handled by fp including detail
            continue;
        }
    }

    // Loop: same fail fingerprint twice without progress between
    if (identicalFails >= identicalFailsForLoop())
    {
        // Ensure no progress after the fail streak start — already reset on progress
        d.verdict = StallVerdict::LoopSuspect;
        d.reasonCode = u"identical-tool-fail"_ustr;
        d.reasonZh = u"同一工具连续失败 · 疑似空转 · 可停止或改指令"_ustr;
        d.shouldNudge = true;
        d.shouldStop = identicalFails >= identicalFailsForLoop() + 1;
        return d;
    }

    // Soft stall: idle units without progress
    if (idleRun >= idleUnitsForSoftStall())
    {
        // Long build defense: if last events are IdleTick only after ToolOk with
        // action containing compile/build markers — caller should send Progress.
        // Pure detector: IdleTick alone after Progress is stall only if idleRun large.
        d.verdict = StallVerdict::SoftStall;
        d.reasonCode = u"idle-no-progress"_ustr;
        d.reasonZh = u"较久无输出 · 可取消或等待（长编译请发 Progress）"_ustr;
        d.shouldNudge = true;
        d.shouldStop = false;
        return d;
    }

    // Many reads without edit/progress
    if (readOnlyRun >= readOnlyBurstForSuspect() && lastProgressUnit < 0)
    {
        d.verdict = StallVerdict::ReadNoWrite;
        d.reasonCode = u"read-only-burst"_ustr;
        d.reasonZh = u"大量只读无进展 · 可能在空转检索"_ustr;
        d.shouldNudge = true;
        d.shouldStop = false;
        return d;
    }

    return d;
}

OUString StallDetector::policyHintZh()
{
    return u"卡顿检测：同失败指纹≥2 → 疑似空转；空闲 tick≥4 → 软卡顿；"
           "纯只读连发可能告警。TDD 红绿、换错信息的重试、有 Progress 的长构建不告警。"_ustr;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
