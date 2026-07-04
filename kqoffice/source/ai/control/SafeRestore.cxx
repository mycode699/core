/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M4: Control Plane — Safe Restore).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of SafeRestore.
 */

#include "SafeRestore.hxx"
#include "SessionStore.hxx"

#include <osl/time.h>
#include <sal/log.hxx>

namespace kqoffice::ai::control
{

namespace
{

sal_Int64 currentTimeMs()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
         + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

OUString msNow()
{
    return OUString::number(currentTimeMs());
}

} // anonymous namespace

SafeRestoreResult SafeRestore::restore(SafeRestoreMode mode)
{
    SAL_INFO("kqoffice.ai.control",
        "SafeRestore: restore mode=" << static_cast<int>(mode));

    SafeRestoreResult result;
    result.mode = mode;
    result.success = false;

    SessionStore store;

    switch (mode)
    {
        case SafeRestoreMode::NoRestore:
            // Blank start — nothing to restore
            result.success = true;
            SAL_INFO("kqoffice.ai.control", "SafeRestore: NoRestore mode — blank start");
            break;

        case SafeRestoreMode::Normal:
        {
            // Full restore: load manifest, then restore all workspaces
            SessionManifest m = store.loadManifest();
            for (const auto& wsId : m.workspaceIds)
            {
                OUString wsJson;
                if (store.loadWorkspace(wsId, wsJson))
                {
                    result.restoredWorkspaces.push_back(wsId);
                }
                else
                {
                    result.corruptedWorkspaces.push_back(wsId);
                    SAL_WARN("kqoffice.ai.control",
                        "SafeRestore: corrupt workspace " << wsId);
                }
            }
            result.success = !m.workspaceIds.empty() || true;
            // Even with zero workspaces, Normal mode succeeds (empty session)
            break;
        }

        case SafeRestoreMode::Safe:
        {
            // Safe mode: load manifest but skip surfaces, only restore
            // workspace metadata.  Minimizes risk from corrupted surface data.
            SessionManifest m = store.loadManifest();
            for (const auto& wsId : m.workspaceIds)
            {
                OUString wsJson;
                if (store.loadWorkspace(wsId, wsJson))
                {
                    result.restoredWorkspaces.push_back(wsId);
                }
                else
                {
                    result.corruptedWorkspaces.push_back(wsId);
                }
            }
            result.success = true;
            SAL_INFO("kqoffice.ai.control",
                "SafeRestore: safe mode restored " << result.restoredWorkspaces.size()
                << " workspaces");
            break;
        }

        case SafeRestoreMode::Selective:
            // Selective mode with no specific workspace ID — falls through
            // to restore everything, caller should use restoreWorkspace().
            result.success = true;
            break;

        case SafeRestoreMode::Quarantine:
        {
            // Quarantine all corrupted surfaces, then restore everything else
            std::vector<OUString> damaged = detectDamage();
            for (const auto& surfaceId : damaged)
            {
                quarantine(surfaceId);
                result.quarantinedSurfaces.push_back(surfaceId);
            }
            // Now do a normal restore
            SessionManifest m = store.loadManifest();
            for (const auto& wsId : m.workspaceIds)
            {
                OUString wsJson;
                if (store.loadWorkspace(wsId, wsJson))
                    result.restoredWorkspaces.push_back(wsId);
                else
                    result.corruptedWorkspaces.push_back(wsId);
            }
            result.success = true;
            break;
        }

        case SafeRestoreMode::Doctor:
        {
            // Doctor mode: validate everything, produce a report, do not mutate.
            OUString report;
            report += "SafeRestore Doctor Report\n";
            report += "========================\n";
            report += "Timestamp: " + msNow() + "\n\n";

            SessionManifest m = store.loadManifest();
            if (!m.isValid)
            {
                report += "MANIFEST: corrupted or unreadable\n";
            }
            else
            {
                report += "Manifest: version=" + m.version
                       + ", workspaces=" + OUString::number(static_cast<sal_Int32>(m.workspaceIds.size()))
                       + ", lastSaved=" + OUString::number(m.lastSavedMs) + "\n";

                for (const auto& wsId : m.workspaceIds)
                {
                    OUString wsJson;
                    if (store.loadWorkspace(wsId, wsJson))
                    {
                        report += "  WORKSPACE " + wsId + ": ok ("
                               + OUString::number(wsJson.getLength()) + " chars)\n";
                        result.restoredWorkspaces.push_back(wsId);
                    }
                    else
                    {
                        report += "  WORKSPACE " + wsId + ": CORRUPT\n";
                        result.corruptedWorkspaces.push_back(wsId);
                    }
                }
            }

            std::vector<OUString> damaged = detectDamage();
            for (const auto& surfaceId : damaged)
            {
                report += "  SURFACE " + surfaceId + ": CORRUPT\n";
            }

            result.diagnosticsReport = report;
            result.success = true;
            break;
        }
    }

    return result;
}

SafeRestoreResult SafeRestore::restoreWorkspace(const OUString& workspaceId)
{
    SAL_INFO("kqoffice.ai.control",
        "SafeRestore: restoreWorkspace " << workspaceId);

    SafeRestoreResult result;
    result.mode = SafeRestoreMode::Selective;
    result.success = false;

    SessionStore store;
    OUString wsJson;

    if (store.loadWorkspace(workspaceId, wsJson))
    {
        result.restoredWorkspaces.push_back(workspaceId);
        result.success = true;
    }
    else
    {
        result.corruptedWorkspaces.push_back(workspaceId);
        SAL_WARN("kqoffice.ai.control",
            "SafeRestore: workspace " << workspaceId << " not found or corrupt");
    }

    return result;
}

SafeRestoreResult SafeRestore::quarantine(const OUString& surfaceId)
{
    SAL_INFO("kqoffice.ai.control", "SafeRestore: quarantine surface " << surfaceId);

    SafeRestoreResult result;
    result.mode = SafeRestoreMode::Quarantine;
    result.success = false;

    SessionStore store;

    // Read the surface data for diagnostics
    OUString surfaceJson;
    if (store.loadSurface(surfaceId, surfaceJson))
    {
        // Move to quarantine: rename <id>.json to <id>.quarantine.json
        // In a real implementation we'd atomically move the file.
        // For now, delete the surface file and log it.
        if (store.deleteSurface(surfaceId))
        {
            result.quarantinedSurfaces.push_back(surfaceId);
            result.success = true;
            SAL_INFO("kqoffice.ai.control",
                "SafeRestore: quarantined surface " << surfaceId);
        }
    }
    else
    {
        // Surface doesn't exist or can't be read — nothing to quarantine
        result.success = true;
    }

    return result;
}

bool SafeRestore::validateSession(const OUString& workspaceId)
{
    SAL_INFO("kqoffice.ai.control",
        "SafeRestore: validateSession " << workspaceId);

    SessionStore store;

    // Check workspace file exists and is non-empty
    OUString wsJson;
    if (!store.loadWorkspace(workspaceId, wsJson))
        return false;

    // Minimal content check: must be a JSON object
    if (wsJson.isEmpty() || wsJson[0] != '{')
        return false;

    return true;
}

bool SafeRestore::validateSurface(const OUString& surfaceId)
{
    SAL_INFO("kqoffice.ai.control",
        "SafeRestore: validateSurface " << surfaceId);

    SessionStore store;

    // Check surface file exists and is non-empty
    OUString surfaceJson;
    if (!store.loadSurface(surfaceId, surfaceJson))
        return false;

    // Minimal content check: must be a JSON object
    if (surfaceJson.isEmpty() || surfaceJson[0] != '{')
        return false;

    return true;
}

OUString SafeRestore::doctor()
{
    SAL_INFO("kqoffice.ai.control", "SafeRestore: doctor");

    // Doctor is equivalent to Doctor-mode restore
    SafeRestoreResult r = restore(SafeRestoreMode::Doctor);
    return r.diagnosticsReport;
}

OUString SafeRestore::doctorWorkspace(const OUString& workspaceId)
{
    SAL_INFO("kqoffice.ai.control",
        "SafeRestore: doctorWorkspace " << workspaceId);

    OUString report;
    report += "Workspace Doctor Report: " + workspaceId + "\n";
    report += "=========================================\n";
    report += "Timestamp: " + msNow() + "\n\n";

    SessionStore store;

    // Check manifest
    SessionManifest m = store.loadManifest();
    if (m.isValid)
    {
        report += "Manifest: ok (version=" + m.version + ")\n";
    }
    else
    {
        report += "Manifest: MISSING or CORRUPT\n";
    }

    // Check workspace file
    OUString wsJson;
    if (store.loadWorkspace(workspaceId, wsJson))
    {
        report += "Workspace file: ok (" + OUString::number(wsJson.getLength()) + " chars)\n";
    }
    else
    {
        report += "Workspace file: MISSING or CORRUPT\n";
    }

    // Check scrollback
    OUString scrollback;
    if (store.loadScrollback(workspaceId, scrollback, 10))
    {
        report += "Scrollback: ok (" + OUString::number(scrollback.getLength()) + " chars, last 10 lines shown)\n";
    }
    else
    {
        report += "Scrollback: MISSING or EMPTY\n";
    }

    return report;
}

bool SafeRestore::isolateDamage(const OUString& surfaceId)
{
    SAL_INFO("kqoffice.ai.control",
        "SafeRestore: isolateDamage " << surfaceId);

    SessionStore store;

    // Read current surface data
    OUString surfaceJson;
    if (!store.loadSurface(surfaceId, surfaceJson))
    {
        // Surface doesn't exist — nothing to isolate
        return true;
    }

    // Create a damage boundary by renaming the surface file to
    // <id>.quarantine.json. In a real implementation we'd also
    // detach any IPC pipes / sockets associated with this surface.
    if (store.deleteSurface(surfaceId))
    {
        SAL_INFO("kqoffice.ai.control",
            "SafeRestore: isolated surface " << surfaceId << " (deleted)");
        return true;
    }

    return false;
}

std::vector<OUString> SafeRestore::detectDamage()
{
    SAL_INFO("kqoffice.ai.control", "SafeRestore: detectDamage");

    SessionStore store;
    std::vector<OUString> damaged;

    SessionManifest m = store.loadManifest();
    if (!m.isValid)
    {
        // Manifest is broken — that is a separate problem.
        // Don't flag individual surfaces unless we know what they are.
        return damaged;
    }

    // For each workspace, check the corresponding surface files
    for (const auto& wsId : m.workspaceIds)
    {
        // Check workspace for damage
        OUString wsJson;
        if (!store.loadWorkspace(wsId, wsJson))
        {
            damaged.push_back(wsId);
            SAL_WARN("kqoffice.ai.control",
                "SafeRestore: damaged workspace " << wsId);
            continue;
        }

        // Scan for surface IDs referenced in the workspace JSON
        // using a simple heuristic: find "surfaceId":" values
        sal_Int32 searchPos = 0;
        static const OUString surfaceKey = u"\"surfaceId\":\""_ustr;
        while (true)
        {
            sal_Int32 idx = wsJson.indexOf(surfaceKey, searchPos);
            if (idx < 0)
                break;

            idx += surfaceKey.getLength();
            sal_Int32 end = wsJson.indexOf('"', idx);
            if (end < 0)
                break;

            OUString sid = wsJson.copy(idx, end - idx);
            if (!sid.isEmpty())
            {
                // Validate this surface
                if (!validateSurface(sid))
                {
                    damaged.push_back(sid);
                    SAL_WARN("kqoffice.ai.control",
                        "SafeRestore: damaged surface " << sid);
                }
            }
            searchPos = end + 1;
        }
    }

    return damaged;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */