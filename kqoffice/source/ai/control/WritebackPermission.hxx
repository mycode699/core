/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: document write-back ladder).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Default is Ask (no silent main-document mutation). Users may Allow once,
 * Allow for this session (process-lifetime via PermissionGrant), or Deny.
 * YOLO is opt-in only (env / explicit policy) — never the product default.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_WRITEBACKPERMISSION_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_WRITEBACKPERMISSION_HXX

#include "PermissionGrant.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::control
{

/// How much of the document the write-back may touch.
enum class WritebackScope : sal_uInt8
{
    Selection = 0, ///< Current selection / known block only
    Document, ///< Current document (beyond selection)
    Workspace, ///< Vault / workspace paths (still needs path auth)
};

/// Product default ladder (mirrors grok-app Ask / once / session / YOLO spirit).
enum class WritebackTier : sal_uInt8
{
    Ask = 0, ///< Always need explicit approve (default)
    AllowOnce,
    AllowSession,
    Yolo, ///< Unattended auto-apply — env/enterprise only
};

struct WritebackGateRequest
{
    /// True when the UI/MCP path already collected an explicit human yes
    /// for this single apply (checkbox / button / humanApproval=true).
    bool explicitHumanApproval = false;
    /// Optional UI three-way when showing 拒绝/本次/本轮 (headless tests).
    PermissionDecision uiDecision = PermissionDecision::Deny;
    WritebackScope scope = WritebackScope::Selection;
    /// writer | calc | impress | unknown
    OUString surface;
    /// Optional stable document key (url hash / shell id); empty → surface-only.
    OUString docKey;
};

struct WritebackGateResult
{
    bool allowed = false;
    /// True when caller should show 拒绝 / 仅本次 / 本轮对话均允许.
    bool needsUi = false;
    OUString actionId;
    WritebackTier effectiveTier = WritebackTier::Ask;
    PermissionDecision decision = PermissionDecision::Deny;
    bool fromSessionCache = false;
    bool fromYolo = false;
    OUString reasonZh;
    /// Stable machine code for logs / diagnostics (no secrets).
    OUString errorCode;
};

/// Document write-back permission ladder. Pure control-plane; UI owns dialogs.
class SAL_DLLPUBLIC_EXPORT WritebackPermission
{
public:
    /// Default product tier is always Ask.
    static WritebackTier defaultTier();

    /// YOLO only when KQOFFICE_AI_WRITEBACK_YOLO=1 (or true/yes/on).
    static bool yoloEnabled();

    /// Session action id, e.g. writeback.selection@writer or writeback.document@calc#docHash.
    static OUString actionId(WritebackScope scope, const OUString& surface,
                             const OUString& docKey = OUString());

    /// Infer scope from plan targets (selection* → Selection, else Document).
    static WritebackScope scopeFromPlanTargets(bool bAnySelectionTarget,
                                               bool bWorkspacePathOp = false);

    /// Evaluate without mutating session grants (except YOLO short-circuit).
    /// Order: YOLO → session cache → explicitHumanApproval → uiDecision → deny.
    static WritebackGateResult evaluate(const WritebackGateRequest& req);

    /// Apply a UI decision (AllowSession stores; AllowOnce/Deny do not).
    /// Returns the post-decision gate result for the same request.
    static WritebackGateResult resolve(const WritebackGateRequest& req,
                                       PermissionDecision choice);

    /// Convenience: may this apply proceed? (evaluate.allowed)
    static bool mayApply(const WritebackGateRequest& req);

    /// Drop all writeback.* session grants (tests / session reset).
    static void clearSessionWritebackGrants();

    static OUString scopeLabelZh(WritebackScope scope);
    static OUString tierLabelZh(WritebackTier tier);
    static OUString surfaceLabelZh(const OUString& surface);

    /// Clarify card copy for UI (title stays「需要确认」).
    static ClarificationPrompt clarifyPrompt(const WritebackGateRequest& req);

    /// One-line settings / diagnostics blurb.
    static OUString policyHintZh();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
