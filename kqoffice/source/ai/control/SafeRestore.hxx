/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M4: Control Plane — Safe Restore).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Safe session restore with damage isolation, quarantine, and doctor mode.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_SAFERESTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_SAFERESTORE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

enum class SafeRestoreMode
{
    Normal,     // 正常恢复
    Safe,       // 安全模式 — 最小化恢复
    NoRestore,  // 不恢复 — 空白启动
    Selective,  // 选择性恢复 — 指定 workspace
    Quarantine, // 隔离模式 — 隔离损坏的 surface
    Doctor      // 诊断模式 — 验证并报告
};

struct SafeRestoreResult
{
    bool                   success;
    SafeRestoreMode        mode;
    std::vector<OUString>  restoredWorkspaces;
    std::vector<OUString>  corruptedWorkspaces;
    std::vector<OUString>  quarantinedSurfaces;
    OUString               diagnosticsReport;
};

class SafeRestore
{
public:
    // Restore modes
    static SafeRestoreResult restore(SafeRestoreMode mode = SafeRestoreMode::Normal);
    static SafeRestoreResult restoreWorkspace(const OUString& workspaceId);
    static SafeRestoreResult quarantine(const OUString& surfaceId);

    // Validation
    static bool validateSession(const OUString& workspaceId);
    static bool validateSurface(const OUString& surfaceId);

    // Doctor mode
    static OUString doctor();
    static OUString doctorWorkspace(const OUString& workspaceId);

    // Damage isolation
    static bool                  isolateDamage(const OUString& surfaceId);
    static std::vector<OUString> detectDamage();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */