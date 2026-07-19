/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (Wave D4: clarification + session permission UX).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Session-scoped action grants (Deny / 仅本次 / 本轮对话均允许) used by DuMate
 * clarify cards and future apply/delete paths. Distinct from PermissionCenter
 * (capability policy: network/mic/folders) — this is per-action UX consent for
 * the current process only (no disk persistence).
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_PERMISSIONGRANT_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_PERMISSIONGRANT_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <optional>
#include <vector>

namespace kqoffice::ai::control
{

/// User decision from the clarification / permission card.
enum class PermissionDecision : sal_uInt8
{
    Deny = 0, ///< 拒绝
    AllowOnce, ///< 仅本次
    AllowSession, ///< 本轮对话均允许 (process lifetime for actionId)
};

/// Input for a clarify card (message + optional multi-select options).
struct ClarificationPrompt
{
    /// Stable action key used for session allow cache, e.g. "apply.diff", "delete.range".
    OUString actionId;
    /// Primary message shown under title「需要确认」.
    OUString messageZh;
    /// Optional multi-select clarify options (labels in Chinese).
    std::vector<OUString> options;
    /// Optional per-option initial checked state (same order as options; missing → false).
    std::vector<bool> optionDefaults;
};

/// Result of presenting (or auto-resolving) a clarify card.
struct ClarificationResult
{
    PermissionDecision decision = PermissionDecision::Deny;
    /// Indices into ClarificationPrompt::options that were selected.
    std::vector<sal_Int32> selectedOptionIndices;
    /// Labels of selected options (parallel convenience).
    std::vector<OUString> selectedOptions;
    /// True when decision came from in-process session cache (no dialog).
    bool fromSessionCache = false;
};

/// In-process session allow store + pure decision helpers for Wave D4.
///
/// Thread-safety: methods take an internal mutex; safe for concurrent callers
/// on the control plane. UI must still be invoked on the main thread.
class SAL_DLLPUBLIC_EXPORT PermissionGrant
{
public:
    /// True if actionId was granted for this process (本轮对话均允许).
    static bool isSessionAllowed(const OUString& actionId);

    /// Record a session allow for actionId until process end (or clear).
    static void grantSession(const OUString& actionId);

    /// Remove a single session allow.
    static void revokeSession(const OUString& actionId);

    /// Clear all session allows (tests / session reset).
    static void clearAllSessionAllows();

    /// Snapshot of currently allowed action ids.
    static std::vector<OUString> sessionAllowedActions();

    /// Apply a non-UI decision: AllowSession stores; Deny/AllowOnce do not.
    static void applyDecision(const OUString& actionId, PermissionDecision decision);

    /// If actionId is session-allowed, return AllowSession; else nullopt (show UI).
    static std::optional<PermissionDecision> tryAutoAllow(const OUString& actionId);

    /// Chinese label for a decision (测试 / 日志).
    static OUString decisionLabelZh(PermissionDecision decision);

    /// Resolve without UI: session cache hit → AllowSession; else Deny.
    /// Useful for headless unit tests of apply/delete guard paths.
    static ClarificationResult resolveHeadless(const ClarificationPrompt& prompt);
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
