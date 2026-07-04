/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M4: Control Plane — Session Store).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Modular session layout (NOT one big JSON):
 *   ${KQOFFICE_AI_SESSIONS_DIR}/manifest.json
 *   ${KQOFFICE_AI_SESSIONS_DIR}/workspaces/<id>.json
 *   ${KQOFFICE_AI_SESSIONS_DIR}/surfaces/<id>.json
 *   ${KQOFFICE_AI_SESSIONS_DIR}/scrollback/<id>.log
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_SESSIONSTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_SESSIONSTORE_HXX

#include <osl/mutex.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

struct SessionManifest
{
    OUString             version;
    std::vector<OUString> workspaceIds;
    sal_Int64            lastSavedMs   = 0;
    sal_Int64            totalSizeBytes = 0;
    bool                 isValid       = false;
};

class SessionStore
{
public:
    SessionStore();
    explicit SessionStore(const OUString& rootDir);

    // Manifest
    SessionManifest loadManifest();
    bool            saveManifest(const SessionManifest& m);

    // Workspace persistence
    bool saveWorkspace(const OUString& id, const OUString& json);
    bool loadWorkspace(const OUString& id, OUString& out);
    bool deleteWorkspace(const OUString& id);

    // Surface persistence
    bool saveSurface(const OUString& id, const OUString& json);
    bool loadSurface(const OUString& id, OUString& out);
    bool deleteSurface(const OUString& id);

    // Scrollback persistence
    bool appendScrollback(const OUString& surfaceId, const OUString& line);
    bool loadScrollback(const OUString& surfaceId, OUString& out,
                        sal_Int64 maxLines = 10000);
    bool rotateScrollback(const OUString& surfaceId, sal_Int64 maxSizeBytes);

    // Validation
    bool                  validateAll();
    std::vector<OUString> corruptedSessions();

private:
    OUString ensureDir(const OUString& subPath);
    OUString filePath(const OUString& subPath, const OUString& fileName);
    bool     writeFile(const OUString& path, const OUString& content);
    bool     readFile(const OUString& path, OUString& out);
    bool     fileExists(const OUString& path);
    sal_Int64 fileSize(const OUString& path);

    OUString m_rootDir;
    osl::Mutex m_mutex;
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */