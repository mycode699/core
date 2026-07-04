/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M4: Control Plane — Session Store).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of SessionStore.
 */

#include "SessionStore.hxx"

#include <osl/file.hxx>
#include <osl/security.hxx>
#include <osl/thread.h>
#include <osl/time.h>
#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cstdlib>

namespace kqoffice::ai::control
{

namespace
{

OUString defaultSessionDir()
{
    const char* envDir = ::getenv("KQOFFICE_AI_SESSIONS_DIR");
    if (envDir && envDir[0] != '\0')
    {
        return OUString::createFromAscii(envDir);
    }
    // Fallback: ~/.kqoffice/ai/sessions
    OUString home;
    if (!osl::Security().getHomeDir(home))
    {
        // If home dir is unavailable, use a tmp path
        return u"/tmp/kqoffice-ai-sessions"_ustr;
    }
    return home + u"/.kqoffice/ai/sessions"_ustr;
}

} // anonymous namespace

SessionStore::SessionStore()
    : m_rootDir(defaultSessionDir())
{
}

SessionStore::SessionStore(const OUString& rootDir)
    : m_rootDir(rootDir)
{
}

// ---- Manifest -----------------------------------------------------------

SessionManifest SessionStore::loadManifest()
{
    osl::MutexGuard guard(m_mutex);

    SessionManifest m;
    OUString json;
    if (readFile(filePath(u""_ustr, u"manifest.json"_ustr), json))
    {
        // Minimal JSON scan: extract version, lastSavedMs, totalSizeBytes.
        // LibreOffice-style: cheap field-at-a-time without pulling in a full
        // JSON parser.  The manifest is always small (<4 KB).
        sal_Int32 idx;
        // version
        idx = json.indexOf(u"\"version\":\""_ustr);
        if (idx >= 0)
        {
            idx += 11; // skip past \"version\":\"
            sal_Int32 end = json.indexOf('"', idx);
            if (end > idx)
                m.version = json.copy(idx, end - idx);
        }
        // lastSavedMs
        idx = json.indexOf(u"\"lastSavedMs\":"_ustr);
        if (idx >= 0)
        {
            idx += 14;
            m.lastSavedMs = json.copy(idx).toInt64();
        }
        // totalSizeBytes
        idx = json.indexOf(u"\"totalSizeBytes\":"_ustr);
        if (idx >= 0)
        {
            idx += 17;
            m.totalSizeBytes = json.copy(idx).toInt64();
        }
        // workspaceIds — collect quoted strings inside the array
        idx = json.indexOf(u"\"workspaceIds\":["_ustr);
        if (idx >= 0)
        {
            idx += 16; // skip past \"workspaceIds\":[
            while (idx < json.getLength())
            {
                // skip whitespace
                while (idx < json.getLength()
                       && (json[idx] == ' ' || json[idx] == '\t'
                           || json[idx] == '\n' || json[idx] == '\r'))
                    ++idx;
                if (idx >= json.getLength() || json[idx] == ']')
                    break;
                if (json[idx] == '"')
                {
                    ++idx;
                    sal_Int32 start = idx;
                    while (idx < json.getLength() && json[idx] != '"')
                        ++idx;
                    if (idx > start)
                        m.workspaceIds.push_back(json.copy(start, idx - start));
                    ++idx; // skip closing quote
                }
                // skip comma
                while (idx < json.getLength()
                       && (json[idx] == ',' || json[idx] == ' '
                           || json[idx] == '\t' || json[idx] == '\n'
                           || json[idx] == '\r'))
                    ++idx;
            }
        }
        m.isValid = true;

        SAL_INFO("kqoffice.ai.control",
            "SessionStore: loaded manifest, version=" << m.version
            << ", workspaces=" << m.workspaceIds.size());
    }
    return m;
}

bool SessionStore::saveManifest(const SessionManifest& m)
{
    osl::MutexGuard guard(m_mutex);

    // Build the manifest JSON manually — small and predictable shape.
    OUStringBuffer buf(512);
    buf.append("{\n");
    buf.append("  \"version\": \"" + m.version + "\",\n");
    buf.append("  \"lastSavedMs\": " + OUString::number(m.lastSavedMs) + ",\n");
    buf.append("  \"totalSizeBytes\": " + OUString::number(m.totalSizeBytes) + ",\n");
    buf.append("  \"workspaceIds\": [\n");
    for (size_t i = 0; i < m.workspaceIds.size(); ++i)
    {
        if (i > 0)
            buf.append(",\n");
        buf.append("    \"" + m.workspaceIds[i] + "\"");
    }
    buf.append("\n  ]\n");
    buf.append("}\n");

    bool ok = writeFile(filePath(u""_ustr, u"manifest.json"_ustr), buf.makeStringAndClear());
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: save manifest -> " << (ok ? "ok" : "fail"));
    return ok;
}

// ---- Workspace ----------------------------------------------------------

bool SessionStore::saveWorkspace(const OUString& id, const OUString& json)
{
    osl::MutexGuard guard(m_mutex);

    OUString dir = ensureDir(u"workspaces"_ustr);
    if (dir.isEmpty())
        return false;

    OUString path = dir + u"/"_ustr + id + u".json"_ustr;
    bool ok = writeFile(path, json);
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: saveWorkspace " << id << " -> " << (ok ? "ok" : "fail"));
    return ok;
}

bool SessionStore::loadWorkspace(const OUString& id, OUString& out)
{
    osl::MutexGuard guard(m_mutex);

    OUString path = filePath(u"workspaces"_ustr, id + u".json"_ustr);
    bool ok = readFile(path, out);
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: loadWorkspace " << id << " -> " << (ok ? "ok" : "fail"));
    return ok;
}

bool SessionStore::deleteWorkspace(const OUString& id)
{
    osl::MutexGuard guard(m_mutex);

    OUString path = filePath(u"workspaces"_ustr, id + u".json"_ustr);
    osl::FileBase::RC rc = osl::File::remove(path);
    bool ok = (rc == osl::FileBase::E_None);
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: deleteWorkspace " << id << " -> " << (ok ? "ok" : "fail"));
    return ok;
}

// ---- Surface ------------------------------------------------------------

bool SessionStore::saveSurface(const OUString& id, const OUString& json)
{
    osl::MutexGuard guard(m_mutex);

    OUString dir = ensureDir(u"surfaces"_ustr);
    if (dir.isEmpty())
        return false;

    OUString path = dir + u"/"_ustr + id + u".json"_ustr;
    bool ok = writeFile(path, json);
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: saveSurface " << id << " -> " << (ok ? "ok" : "fail"));
    return ok;
}

bool SessionStore::loadSurface(const OUString& id, OUString& out)
{
    osl::MutexGuard guard(m_mutex);

    OUString path = filePath(u"surfaces"_ustr, id + u".json"_ustr);
    bool ok = readFile(path, out);
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: loadSurface " << id << " -> " << (ok ? "ok" : "fail"));
    return ok;
}

bool SessionStore::deleteSurface(const OUString& id)
{
    osl::MutexGuard guard(m_mutex);

    OUString path = filePath(u"surfaces"_ustr, id + u".json"_ustr);
    osl::FileBase::RC rc = osl::File::remove(path);
    bool ok = (rc == osl::FileBase::E_None);
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: deleteSurface " << id << " -> " << (ok ? "ok" : "fail"));
    return ok;
}

// ---- Scrollback ---------------------------------------------------------

bool SessionStore::appendScrollback(const OUString& surfaceId, const OUString& line)
{
    osl::MutexGuard guard(m_mutex);

    OUString dir = ensureDir(u"scrollback"_ustr);
    if (dir.isEmpty())
        return false;

    OUString path = dir + u"/"_ustr + surfaceId + u".log"_ustr;

    // Open for append (or create if missing)
    osl::File file(path);
    osl::FileBase::RC rc = file.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (rc == osl::FileBase::E_None)
    {
        // Seek to end for append
        file.setPos(osl_Pos_End, 0);

        OString utf8Line = OUStringToOString(line, RTL_TEXTENCODING_UTF8);
        utf8Line += "\n";
        sal_uInt64 nWritten = 0;
        rc = file.write(utf8Line.getStr(), utf8Line.getLength(), nWritten);
        file.close();

        if (rc == osl::FileBase::E_None)
        {
            SAL_INFO("kqoffice.ai.control",
                "SessionStore: appendScrollback " << surfaceId << " (" << nWritten << " bytes)");
            return true;
        }
    }
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: appendScrollback " << surfaceId << " -> fail (rc=" << static_cast<int>(rc) << ")");
    return false;
}

bool SessionStore::loadScrollback(const OUString& surfaceId, OUString& out,
                                    sal_Int64 maxLines)
{
    osl::MutexGuard guard(m_mutex);

    OUString path = filePath(u"scrollback"_ustr, surfaceId + u".log"_ustr);
    OUString raw;
    if (!readFile(path, raw))
        return false;

    // Split into lines and take the last maxLines
    std::vector<OUString> lines;
    sal_Int32 start = 0;
    while (start < raw.getLength())
    {
        sal_Int32 end = raw.indexOf('\n', start);
        if (end < 0)
            end = raw.getLength();
        lines.push_back(raw.copy(start, end - start));
        start = end + 1;
    }

    // Keep only the last maxLines
    sal_Int64 skip = static_cast<sal_Int64>(lines.size()) - maxLines;
    if (skip < 0)
        skip = 0;

    OUStringBuffer buf(static_cast<sal_Int32>(raw.getLength()));
    for (size_t i = static_cast<size_t>(skip); i < lines.size(); ++i)
    {
        if (i > static_cast<size_t>(skip))
            buf.append("\n");
        buf.append(lines[i]);
    }
    out = buf.makeStringAndClear();
    return true;
}

bool SessionStore::rotateScrollback(const OUString& surfaceId, sal_Int64 maxSizeBytes)
{
    osl::MutexGuard guard(m_mutex);

    OUString path = filePath(u"scrollback"_ustr, surfaceId + u".log"_ustr);

    sal_Int64 sz = fileSize(path);
    if (sz < 0 || sz <= maxSizeBytes)
    {
        // File is within budget — nothing to do
        return true;
    }

    // Read the whole file, trim from the start until it fits
    OUString raw;
    if (!readFile(path, raw))
        return false;

    // Estimate: each UTF-8 byte ~ one char in OUString. Trim greedily.
    // Drop lines from the top until we are under maxSizeBytes.
    sal_Int32 trimStart = 0;
    while (trimStart < raw.getLength())
    {
        sal_Int32 remaining = raw.getLength() - trimStart;
        if (remaining <= maxSizeBytes)
            break;
        sal_Int32 next = raw.indexOf('\n', trimStart);
        if (next < 0)
            break;
        trimStart = next + 1;
    }

    OUString trimmed = raw.copy(trimStart);
    bool ok = writeFile(path, trimmed);
    SAL_INFO("kqoffice.ai.control",
        "SessionStore: rotateScrollback " << surfaceId
        << " from " << sz << " bytes -> " << (ok ? "ok" : "fail"));
    return ok;
}

// ---- Validation ---------------------------------------------------------

bool SessionStore::validateAll()
{
    osl::MutexGuard guard(m_mutex);

    SAL_INFO("kqoffice.ai.control", "SessionStore: validateAll");
    return corruptedSessions().empty();
}

std::vector<OUString> SessionStore::corruptedSessions()
{
    osl::MutexGuard guard(m_mutex);

    std::vector<OUString> corrupt;

    // Check that the manifest exists and is parseable
    OUString manifestPath = filePath(u""_ustr, u"manifest.json"_ustr);
    if (!fileExists(manifestPath))
    {
        SAL_INFO("kqoffice.ai.control", "SessionStore: manifest missing -> no sessions");
        return corrupt; // fresh install, not corruption
    }

    SessionManifest m = loadManifest();
    if (!m.isValid)
    {
        // Manifest itself is broken — flag root
        corrupt.push_back(u"manifest.json"_ustr);
    }

    // Check each workspace file exists and is non-empty
    for (const auto& wsId : m.workspaceIds)
    {
        OUString wsPath = filePath(u"workspaces"_ustr, wsId + u".json"_ustr);
        if (!fileExists(wsPath) || fileSize(wsPath) <= 0)
        {
            corrupt.push_back(wsId);
            SAL_INFO("kqoffice.ai.control",
                "SessionStore: corrupt workspace " << wsId);
        }
    }

    // Check scrollback dir for consistency (surface IDs in scrollback/)
    // by scanning the scrollback directory listing
    OUString scrollDir = m_rootDir + u"/scrollback"_ustr;
    osl::Directory dir(scrollDir);
    if (dir.open() == osl::FileBase::E_None)
    {
        osl::DirectoryItem item;
        while (dir.getNextItem(item) == osl::FileBase::E_None)
        {
            osl::FileStatus stat(osl_FileStatus_Mask_FileName);
            if (item.getFileStatus(stat) != osl::FileBase::E_None)
                continue;
            OUString fileName = stat.getFileName();
            // fileName is the leaf; check it matches known surfaces
            // (no corresponding surface.json is a warning, not corruption)
            sal_Int32 dot = fileName.lastIndexOf('.');
            if (dot > 0)
            {
                OUString stem = fileName.copy(0, dot);
                OUString surfPath = filePath(u"surfaces"_ustr, stem + u".json"_ustr);
                if (!fileExists(surfPath))
                {
                    SAL_INFO("kqoffice.ai.control",
                        "SessionStore: orphan scrollback " << stem);
                }
            }
        }
        dir.close();
    }

    return corrupt;
}

// ---- Private helpers ----------------------------------------------------

OUString SessionStore::ensureDir(const OUString& subPath)
{
    OUString fullPath = m_rootDir;
    if (!subPath.isEmpty())
        fullPath += u"/"_ustr + subPath;

    osl::Directory::createPath(fullPath);
    return fullPath;
}

OUString SessionStore::filePath(const OUString& subPath, const OUString& fileName)
{
    OUString full = m_rootDir;
    if (!subPath.isEmpty())
        full += u"/"_ustr + subPath;
    full += u"/"_ustr + fileName;
    return full;
}

bool SessionStore::writeFile(const OUString& path, const OUString& content)
{
    OString utf8 = OUStringToOString(content, RTL_TEXTENCODING_UTF8);

    osl::File file(path);
    osl::FileBase::RC rc = file.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (rc != osl::FileBase::E_None)
        return false;

    sal_uInt64 nWritten = 0;
    rc = file.write(utf8.getStr(), utf8.getLength(), nWritten);
    file.close();

    return (rc == osl::FileBase::E_None && nWritten == static_cast<sal_uInt64>(utf8.getLength()));
}

bool SessionStore::readFile(const OUString& path, OUString& out)
{
    osl::File file(path);
    osl::FileBase::RC rc = file.open(osl_File_OpenFlag_Read);
    if (rc != osl::FileBase::E_None)
        return false;

    // Get file size
    sal_uInt64 size = 0;
    rc = file.getSize(size);
    if (rc != osl::FileBase::E_None || size == 0)
    {
        file.close();
        return false;
    }

    // Read entire file into a buffer
    sal_uInt64 nRead = 0;
    OStringBuffer buf(static_cast<sal_Int32>(size));
    // We read in chunks to avoid stack overflows for large files
    const sal_Int32 chunkSize = 65536;
    char chunk[chunkSize];
    while (nRead < size)
    {
        sal_uInt64 toRead = size - nRead;
        if (toRead > chunkSize)
            toRead = chunkSize;
        sal_uInt64 justRead = 0;
        rc = file.read(chunk, static_cast<sal_uInt64>(toRead), justRead);
        if (rc != osl::FileBase::E_None)
            break;
        if (justRead == 0)
            break;
        buf.append(chunk, static_cast<sal_Int32>(justRead));
        nRead += justRead;
    }
    file.close();

    if (nRead == 0)
        return false;

    out = OStringToOUString(buf.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    return true;
}

bool SessionStore::fileExists(const OUString& path)
{
    osl::DirectoryItem item;
    return (osl::DirectoryItem::get(path, item) == osl::FileBase::E_None);
}

sal_Int64 SessionStore::fileSize(const OUString& path)
{
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(path, item) != osl::FileBase::E_None)
        return -1;

    osl::FileStatus stat(osl_FileStatus_Mask_FileSize);
    if (item.getFileStatus(stat) != osl::FileBase::E_None)
        return -1;

    return static_cast<sal_Int64>(stat.getFileSize());
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */