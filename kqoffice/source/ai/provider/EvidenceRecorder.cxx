/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1 Day-1: Evidence).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "EvidenceRecorder.hxx"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai
{
namespace
{
/// Monotonic counter — combined with a wall-clock timestamp gives a
/// collision-free 16-hex id within a single process.
std::atomic<std::uint64_t> g_counter{0};

OUString env(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v) return OUString();
    return OUString(v, std::strlen(v), RTL_TEXTENCODING_UTF8);
}

/// Minimal JSON string escape — the fields we write are our own
/// enum-like strings, but quoting is still correct behavior.
void appendEscaped(OUStringBuffer& buf, const OUString& s)
{
    buf.append('"');
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        sal_Unicode c = s[i];
        switch (c)
        {
            case '"':  buf.append("\\\""); break;
            case '\\': buf.append("\\\\"); break;
            case '\n': buf.append("\\n");  break;
            case '\r': buf.append("\\r");  break;
            case '\t': buf.append("\\t");  break;
            default:
                if (c < 0x20)
                {
                    char tmp[8];
                    std::snprintf(tmp, sizeof(tmp), "\\u%04x",
                                  static_cast<unsigned>(c));
                    buf.appendAscii(tmp);
                }
                else
                {
                    buf.append(c);
                }
        }
    }
    buf.append('"');
}

/// Build absolute system path of the monthly directory, creating it
/// if it does not yet exist. Returns empty on failure.
OUString ensureMonthDir(const OUString& root, const std::tm& utc)
{
    char ym[8];
    std::snprintf(ym, sizeof(ym), "%04d-%02d",
                  utc.tm_year + 1900, utc.tm_mon + 1);

    OUString dir = root + "/" + OUString::createFromAscii(ym);

    // Convert system path → file URL, then create recursively.
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url)
            != osl::FileBase::E_None)
    {
        return OUString();
    }

    osl::FileBase::RC rc = osl::Directory::createPath(url);
    if (rc != osl::FileBase::E_None && rc != osl::FileBase::E_EXIST)
    {
        return OUString();
    }
    return dir;
}

/// ISO-ish UTC timestamp — "2026-05-08T13:30:00Z".
OUString isoTimestamp(const std::tm& utc)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return OUString::createFromAscii(buf);
}

OUString mintId(std::uint64_t seconds)
{
    std::uint64_t c
        = g_counter.fetch_add(1, std::memory_order_relaxed);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "ev-%08x%08x",
                  static_cast<unsigned>(seconds & 0xFFFFFFFFu),
                  static_cast<unsigned>(c & 0xFFFFFFFFu));
    return OUString::createFromAscii(buf);
}

} // namespace

OUString EvidenceRecorder::resolveRootDir()
{
    // 1. explicit test / deployment override
    OUString d = env("KQOFFICE_AI_EVIDENCE_DIR");
    if (!d.isEmpty()) return d;

    // 2. TMPDIR (macOS always sets this; linux usually does)
    OUString tmp = env("TMPDIR");
    if (!tmp.isEmpty())
    {
        while (tmp.endsWith("/"))
            tmp = tmp.copy(0, tmp.getLength() - 1);
        return tmp + "/kqoffice-ai-evidence";
    }

    // 3. last resort
    return u"/tmp/kqoffice-ai-evidence"_ustr;
}

OUString EvidenceRecorder::record(const EvidenceRecord& rec)
{
    TimeValue tv;
    osl_getSystemTime(&tv);

    std::time_t secs = static_cast<std::time_t>(tv.Seconds);
    std::tm utc{};
    gmtime_r(&secs, &utc);

    OUString id = mintId(static_cast<std::uint64_t>(tv.Seconds));
    OUString root = resolveRootDir();
    OUString dir = ensureMonthDir(root, utc);
    if (dir.isEmpty())
        return OUString();

    OUString path = dir + "/" + id + ".json";

    // Build JSON body.
    OUStringBuffer body(256);
    body.append("{\n");
    body.append("  \"evidence_id\": ");
    appendEscaped(body, id);
    body.append(",\n  \"timestamp\": ");
    appendEscaped(body, isoTimestamp(utc));
    body.append(",\n  \"service_mode\": ");
    appendEscaped(body, rec.serviceMode);
    body.append(",\n  \"provider\": ");
    appendEscaped(body, rec.provider);
    body.append(",\n  \"capability\": ");
    appendEscaped(body, rec.capability);
    body.append(",\n  \"status\": ");
    appendEscaped(body, rec.status);
    body.append(",\n  \"request_size_bytes\": ");
    body.append(static_cast<sal_Int32>(rec.requestSizeBytes));
    body.append(",\n  \"response_size_bytes\": ");
    body.append(static_cast<sal_Int32>(rec.responseSizeBytes));
    body.append(",\n  \"duration_ms\": ");
    body.append(static_cast<sal_Int32>(rec.durationMs));
    body.append("\n}\n");

    OString utf8 = OUStringToOString(body.makeStringAndClear(),
                                     RTL_TEXTENCODING_UTF8);

    // Write via stdio — osl::File would also work, but stdio keeps
    // the dependency surface tiny for a pure-logic cppunit test.
    OString sysPath = OUStringToOString(path, RTL_TEXTENCODING_UTF8);
    std::FILE* fp = std::fopen(sysPath.getStr(), "wb");
    if (!fp)
        return OUString();
    std::fwrite(utf8.getStr(), 1, utf8.getLength(), fp);
    std::fclose(fp);

    return id;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
