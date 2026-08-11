/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: append-only ledger).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "EventLedger.hxx"

#include "AiPaths.hxx"
#include "ErrorClassifier.hxx"

#include <osl/file.hxx>
#include <osl/mutex.hxx>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>
#include <vector>

namespace kqoffice::ai::control
{

namespace
{
osl::Mutex& ledgerMutex()
{
    static osl::Mutex s;
    return s;
}

std::vector<EventLedger::Subscriber>& subscribers()
{
    static std::vector<EventLedger::Subscriber> s;
    return s;
}

sal_Int64 nowMs()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
           + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

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
        else if (c < 0x20)
            b.append(' ');
        else
            b.append(c);
    }
    b.append('"');
    return b.makeStringAndClear();
}

bool writeAppend(const OUString& systemPath, const OString& utf8)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    // open create or append
    auto rc = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (rc != osl::FileBase::E_None)
        rc = f.open(osl_File_OpenFlag_Write);
    if (rc != osl::FileBase::E_None)
        return false;
    sal_uInt64 size = 0;
    f.getSize(size);
    (void)f.setPos(osl_Pos_Absolut, static_cast<sal_Int64>(size));
    sal_uInt64 written = 0;
    rc = f.write(utf8.getStr(), static_cast<sal_uInt64>(utf8.getLength()), written);
    f.close();
    return rc == osl::FileBase::E_None && written == static_cast<sal_uInt64>(utf8.getLength());
}

OUString readAll(const OUString& systemPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return {};
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return {};
    sal_uInt64 size = 0;
    f.getSize(size);
    if (size == 0 || size > 8 * 1024 * 1024)
    {
        f.close();
        return {};
    }
    std::vector<char> buf(static_cast<size_t>(size) + 1, 0);
    sal_uInt64 n = 0;
    f.read(buf.data(), size, n);
    f.close();
    return OStringToOUString(OString(buf.data(), static_cast<sal_Int32>(n)), RTL_TEXTENCODING_UTF8);
}

sal_Int64 fieldInt(const OUString& line, const OUString& key)
{
    const OUString pat = u"\""_ustr + key + u"\":"_ustr;
    sal_Int32 p = line.indexOf(pat);
    if (p < 0)
        return 0;
    p += pat.getLength();
    while (p < line.getLength() && line[p] == ' ')
        ++p;
    sal_Int32 end = p;
    while (end < line.getLength()
           && ((line[end] >= '0' && line[end] <= '9') || line[end] == '-'))
        ++end;
    if (end == p)
        return 0;
    return line.copy(p, end - p).toInt64();
}

OUString fieldStr(const OUString& line, const OUString& key)
{
    const OUString pat = u"\""_ustr + key + u"\":\""_ustr;
    sal_Int32 p = line.indexOf(pat);
    if (p < 0)
        return {};
    p += pat.getLength();
    sal_Int32 end = p;
    while (end < line.getLength() && line[end] != '"')
    {
        if (line[end] == '\\' && end + 1 < line.getLength())
            end += 2;
        else
            ++end;
    }
    if (end > p)
        return line.copy(p, end - p);
    return {};
}
} // namespace

EventLedger::EventLedger()
    : m_rootDir(resolveRootDir())
{
}

EventLedger::EventLedger(const OUString& rootDir)
    : m_rootDir(rootDir)
{
}

OUString EventLedger::resolveRootDir()
{
    const char* env = std::getenv("KQOFFICE_AI_LEDGER_DIR");
    if (env && *env)
        return OUString::createFromAscii(env);
    return kqofficePathJoin(kqofficeAiConfigDir(), u"ledger"_ustr);
}

OUString EventLedger::eventsPathFor(const OUString& rootDir)
{
    return kqofficePathJoin(rootDir, u"events.jsonl"_ustr);
}

bool EventLedger::ensureDir() const
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(m_rootDir, url) != osl::FileBase::E_None)
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

sal_Int64 EventLedger::nextSequenceFromFile() const
{
    const OUString all = readAll(eventsPathFor(m_rootDir));
    if (all.isEmpty())
        return 1;
    sal_Int64 maxSeq = 0;
    sal_Int32 start = 0;
    while (start <= all.getLength())
    {
        sal_Int32 end = all.indexOf(u'\n', start);
        if (end < 0)
            end = all.getLength();
        const OUString line = all.copy(start, end - start).trim();
        start = end + 1;
        if (line.isEmpty())
            continue;
        const sal_Int64 s = fieldInt(line, u"sequence"_ustr);
        if (s > maxSeq)
            maxSeq = s;
        if (start > all.getLength())
            break;
    }
    return maxSeq + 1;
}

sal_Int64 EventLedger::lastSequence() const
{
    osl::MutexGuard g(ledgerMutex());
    const sal_Int64 n = nextSequenceFromFile();
    return n > 0 ? n - 1 : 0;
}

bool EventLedger::appendLine(const OUString& line) const
{
    const OUString withNl = line + u"\n"_ustr;
    const OString utf8 = OUStringToOString(withNl, RTL_TEXTENCODING_UTF8);
    return writeAppend(eventsPathFor(m_rootDir), utf8);
}

sal_Int64 EventLedger::append(const OUString& type, const OUString& payload, const OUString& runId)
{
    osl::MutexGuard g(ledgerMutex());
    if (!ensureDir())
        return 0;

    const sal_Int64 seq = nextSequenceFromFile();
    const sal_Int64 at = nowMs();
    const OUString safeType = type.isEmpty() ? u"system"_ustr : type;
    const OUString safePayload = ErrorClassifier::redact(payload);

    OUStringBuffer line;
    line.append(u"{\"sequence\":"_ustr);
    line.append(OUString::number(seq));
    line.append(u",\"runSequence\":"_ustr);
    line.append(OUString::number(seq)); // single stream v1
    line.append(u",\"runId\":"_ustr);
    line.append(jsonEscape(runId));
    line.append(u",\"type\":"_ustr);
    line.append(jsonEscape(safeType));
    line.append(u",\"payload\":"_ustr);
    line.append(jsonEscape(safePayload));
    line.append(u",\"atMs\":"_ustr);
    line.append(OUString::number(at));
    line.append(u"}"_ustr);

    if (!appendLine(line.makeStringAndClear()))
        return 0;

    LedgerEvent ev;
    ev.sequence = seq;
    ev.runSequence = seq;
    ev.runId = runId;
    ev.type = safeType;
    ev.payload = safePayload;
    ev.atMs = at;

    // Broadcast only after successful persist.
    for (const auto& sub : subscribers())
    {
        if (sub)
            sub(ev);
    }
    return seq;
}

std::vector<LedgerEvent> EventLedger::replaySince(sal_Int64 sinceSequence,
                                                    sal_Int32 maxEvents) const
{
    std::vector<LedgerEvent> out;
    const OUString all = readAll(eventsPathFor(m_rootDir));
    if (all.isEmpty())
        return out;
    sal_Int32 start = 0;
    while (start <= all.getLength()
           && static_cast<sal_Int32>(out.size()) < (maxEvents > 0 ? maxEvents : 500))
    {
        sal_Int32 end = all.indexOf(u'\n', start);
        if (end < 0)
            end = all.getLength();
        const OUString line = all.copy(start, end - start).trim();
        start = end + 1;
        if (!line.isEmpty())
        {
            const sal_Int64 seq = fieldInt(line, u"sequence"_ustr);
            if (seq > sinceSequence)
            {
                LedgerEvent ev;
                ev.sequence = seq;
                ev.runSequence = fieldInt(line, u"runSequence"_ustr);
                ev.runId = fieldStr(line, u"runId"_ustr);
                ev.type = fieldStr(line, u"type"_ustr);
                ev.payload = fieldStr(line, u"payload"_ustr);
                ev.atMs = fieldInt(line, u"atMs"_ustr);
                out.push_back(ev);
            }
        }
        if (start > all.getLength())
            break;
    }
    return out;
}

void EventLedger::subscribe(Subscriber fn)
{
    osl::MutexGuard g(ledgerMutex());
    subscribers().push_back(std::move(fn));
}

void EventLedger::clearSubscribersForTests()
{
    osl::MutexGuard g(ledgerMutex());
    subscribers().clear();
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
