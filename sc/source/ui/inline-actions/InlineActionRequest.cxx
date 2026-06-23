/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "InlineActionRequest.hxx"

#include <atomic>
#include <cstdio>
#include <ctime>

#include <osl/time.h>
#include <rtl/ustrbuf.hxx>

namespace sc::inline_actions {
namespace {

constexpr OUStringLiteral kSchemaVersion = u"v2-w4-1";
constexpr OUStringLiteral kSurface = u"calc-cell";

std::atomic<sal_uInt64> g_nRequestCounter{0};

void appendEscapedJsonString(OUStringBuffer& rBuf, const OUString& rValue)
{
    rBuf.append('"');
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        switch (c)
        {
            case '"':
                rBuf.append("\\\"");
                break;
            case '\\':
                rBuf.append("\\\\");
                break;
            case '\n':
                rBuf.append("\\n");
                break;
            case '\r':
                rBuf.append("\\r");
                break;
            case '\t':
                rBuf.append("\\t");
                break;
            default:
                if (c < 0x20)
                {
                    char aTmp[8];
                    std::snprintf(aTmp, sizeof(aTmp), "\\u%04x", static_cast<unsigned>(c));
                    rBuf.appendAscii(aTmp);
                }
                else
                {
                    rBuf.append(c);
                }
        }
    }
    rBuf.append('"');
}

OUString mintRequestId()
{
    const sal_uInt64 nTimer = osl_getGlobalTimer();
    const sal_uInt64 nSeq = g_nRequestCounter.fetch_add(1, std::memory_order_relaxed);
    char aBuf[24];
    std::snprintf(aBuf, sizeof(aBuf), "iar-%08x%08x",
                  static_cast<unsigned>(nTimer & 0xFFFFFFFFu),
                  static_cast<unsigned>(nSeq & 0xFFFFFFFFu));
    return OUString::createFromAscii(aBuf);
}

OUString isoTimestampUtc()
{
    TimeValue aTv;
    osl_getSystemTime(&aTv);
    std::time_t nSecs = static_cast<std::time_t>(aTv.Seconds);
    std::tm aUtc{};
    gmtime_r(&nSecs, &aUtc);
    char aBuf[24];
    std::snprintf(aBuf, sizeof(aBuf), "%04d-%02d-%02dT%02d:%02d:%02dZ", aUtc.tm_year + 1900,
                  aUtc.tm_mon + 1, aUtc.tm_mday, aUtc.tm_hour, aUtc.tm_min, aUtc.tm_sec);
    return OUString::createFromAscii(aBuf);
}

} // namespace

bool splitCalcCellRangeA1(const OUString& rCellRangeA1, OUString& rSheet, OUString& rRange)
{
    const sal_Int32 nDot = rCellRangeA1.indexOf('.');
    if (nDot < 0)
        return false;
    rSheet = rCellRangeA1.copy(0, nDot);
    rRange = rCellRangeA1.copy(nDot + 1);
    return !rSheet.isEmpty() && !rRange.isEmpty();
}

bool actionRoutesToDiff(CellAction eAction) { return eAction != CellAction::ExplainData; }

OUString buildCalcCellRequest(const OUString& rActionToken, const OUString& rSheet,
                              const OUString& rRange, const OUString& rServiceMode)
{
    OUStringBuffer aBody(384);
    aBody.append('{');
    aBody.append("\"schema_version\":");
    appendEscapedJsonString(aBody, kSchemaVersion);
    aBody.append(',');
    aBody.append("\"request_id\":");
    appendEscapedJsonString(aBody, mintRequestId());
    aBody.append(',');
    aBody.append("\"surface\":");
    appendEscapedJsonString(aBody, kSurface);
    aBody.append(',');
    aBody.append("\"action\":");
    appendEscapedJsonString(aBody, rActionToken);
    aBody.append(',');
    aBody.append("\"target\":{\"sheet\":");
    appendEscapedJsonString(aBody, rSheet);
    aBody.append(',');
    aBody.append("\"range\":");
    appendEscapedJsonString(aBody, rRange);
    aBody.append("},");
    aBody.append("\"service_mode\":");
    appendEscapedJsonString(aBody, rServiceMode);
    aBody.append(',');
    aBody.append("\"created_at\":");
    appendEscapedJsonString(aBody, isoTimestampUtc());

    const CellAction eAction = fromToken(rActionToken);
    if (actionRoutesToDiff(eAction))
    {
        aBody.append(',');
        aBody.append("\"expected_capability\":");
        appendEscapedJsonString(aBody, rActionToken);
    }

    aBody.append('}');
    return aBody.makeStringAndClear();
}

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */