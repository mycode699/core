/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (GA foundation: Permission Center).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Product-facing permission policy:
 *   - folders: only user-authorized roots; never full-disk by default
 *   - network: provider + payload scope must be disclosed before send
 *   - mic/screenshot: first-use grant, revocable in settings
 *   - plugins: signed allowlist with network scope + crash fuse
 *   - risky file ops (delete/overwrite) under authorized dirs require
 *     DuMate-like second confirmation: 拒绝 / 本次 / 本轮
 *     (PermissionDecision + process-lifetime PermissionGrant session cache)
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_PERMISSIONCENTER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_PERMISSIONCENTER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

#include "PermissionGrant.hxx"

namespace kqoffice::ai::control
{

enum class CapabilityPermission : sal_uInt8
{
    FolderScan = 0,
    NetworkEgress,
    Microphone,
    ScreenCapture,
    Camera,
    PluginRuntime,
};

enum class PermissionState : sal_uInt8
{
    NotRequested = 0,
    Denied,
    Granted,
    Revoked,
};

/// Destructive / mutating filesystem ops that need a second confirmation
/// even when the path sits under an authorized workspace root.
enum class RiskOperation : sal_uInt8
{
    Delete = 0, ///< Soft-delete / move-to-trash / permanent delete intent
    Overwrite, ///< Overwrite existing file contents
};

struct AuthorizedDirectory
{
    OUString path;
    sal_Int64 grantedAtMs = 0;
    bool recursive = true;
};

struct NetworkDisclosure
{
    OUString provider; // e.g. ollama:local, openai-compatible:corp
    OUString endpoint; // host or loopback URL
    OUString scopeSummaryZh; // human-readable payload scope
    bool userConfirmed = false;
};

struct PermissionEvent
{
    CapabilityPermission capability = CapabilityPermission::FolderScan;
    PermissionState state = PermissionState::NotRequested;
    OUString detail;
    sal_Int64 atMs = 0;
};

/// Side-effect free evaluation of a risky file operation.
struct RiskDecision
{
    bool pathAuthorized = false;
    /// True when path is authorized but no session grant covers this op yet.
    bool needsUserConfirm = false;
    /// True when a prior AllowSession already covers this root+op.
    bool sessionAlreadyAllows = false;
    OUString matchedRoot; ///< Authorized root that contains path (if any)
    /// PermissionGrant action key for this root+op (empty if unauthorized).
    OUString sessionActionId;
    OUString reasonZh;
};

/// Local-first permission store. Default denies network/mic/screenshot and
/// requires explicit authorized directories for any filesystem index.
/// Risky delete/overwrite under authorized dirs still needs 拒绝/本次/本轮.
class SAL_DLLPUBLIC_EXPORT PermissionCenter
{
public:
    PermissionCenter();
    explicit PermissionCenter(const OUString& rootDir);

    // ── Folder authorization (workspace / 工作区) ────────────────────

    /// Returns true only if path is under an authorized root.
    bool isPathAuthorized(const OUString& path) const;

    /// Grant a user-selected directory. Does not auto-grant parent/home.
    bool grantDirectory(const OUString& path, bool recursive = true);

    /// Revoke a previously granted directory.
    bool revokeDirectory(const OUString& path);

    /// Revoke every authorized directory (settings "清空工作区").
    bool revokeAllDirectories();

    std::vector<AuthorizedDirectory> authorizedDirectories() const;

    /// Empty means "no scan yet" — never treat as full-disk permission.
    bool hasAnyAuthorizedDirectory() const;

    /// Best matching authorized root for path (empty if unauthorized).
    OUString matchingAuthorizedRoot(const OUString& path) const;

    // ── Risk policy (delete / overwrite) ─────────────────────────────

    /// Build PermissionGrant action id for root+op (process session cache).
    static OUString riskSessionActionId(RiskOperation op, const OUString& authorizedRoot);

    /// Evaluate without applying a user choice.
    RiskDecision evaluateRisk(RiskOperation op, const OUString& path) const;

    /// True when authorized and session does not yet cover this op
    /// (UI should present 拒绝 / 本次 / 本轮).
    bool needsRiskConfirm(RiskOperation op, const OUString& path) const;

    /// True when a prior AllowSession covers this path+op (via PermissionGrant).
    bool hasSessionRiskGrant(RiskOperation op, const OUString& path) const;

    /// Apply user confirmation. Returns true if the op may proceed.
    /// - Deny: false (unless session already allows)
    /// - AllowOnce: true for this call only
    /// - AllowSession: stores process-lifetime grant for matched root + op
    bool resolveRiskyOp(RiskOperation op, const OUString& path, PermissionDecision choice);

    /// Drop all filesystem risk session grants (fs.delete@ / fs.overwrite@).
    void clearSessionRiskGrants();

    sal_Int32 sessionRiskGrantCount() const;

    // ── Capability grants ────────────────────────────────────────────

    PermissionState stateOf(CapabilityPermission cap) const;
    bool request(CapabilityPermission cap, const OUString& reasonZh);
    bool grant(CapabilityPermission cap);
    bool revoke(CapabilityPermission cap);
    bool isGranted(CapabilityPermission cap) const;

    // ── Network disclosure ───────────────────────────────────────────

    /// Records an outbound intent. Returns false if userConfirmed is false
    /// or NetworkEgress is not granted.
    bool discloseNetworkSend(const NetworkDisclosure& disclosure);

    /// Last disclosed (not necessarily sent) network intent summary.
    OUString lastNetworkDisclosureSummary() const;

    // ── Settings surface (pure logic for Options / 本地文件) ─────────

    /// One-line capability status for settings UI.
    /// OS-gated caps (mic/screenshot/camera) use「使用时系统会询问」when
    /// NotRequested so we never imply a silent grant.
    OUString settingsCapabilityStatusZh(CapabilityPermission cap) const;

    /// Network row: state + default-off policy + last disclosure (Chinese).
    OUString networkStatusLineZh() const;

    /// Multi-line settings summary: folders / network / mic / screenshot /
    /// risk three-way (拒绝/本次/本轮). No English product titles.
    OUString settingsSurfaceSummaryZh() const;

    /// Risk policy blurb shown under permission settings.
    static OUString riskPolicyHintZh();

    // ── Persistence / audit ──────────────────────────────────────────

    bool load();
    bool save() const;
    std::vector<PermissionEvent> recentEvents(sal_Int32 maxEvents = 50) const;

    static OUString capabilityLabelZh(CapabilityPermission cap);
    static OUString stateLabelZh(PermissionState state);
    static OUString riskOperationLabelZh(RiskOperation op);
    /// Alias of PermissionGrant::decisionLabelZh (拒绝 / 仅本次 / 本轮…).
    static OUString riskConfirmationLabelZh(PermissionDecision choice);
    /// Human-readable confirm prompt for dialogs.
    static OUString riskPromptZh(RiskOperation op, const OUString& path);

    /// Persist root: $KQOFFICE_AI_PERMISSION_DIR or ~/.config/kqoffice/permissions
    static OUString resolveRootDir();

    /// Display path of the permissions store file.
    OUString storePathForDisplay() const;

private:
    void appendEvent(CapabilityPermission cap, PermissionState state, const OUString& detail);
    OUString storePath() const;
    bool findMatchingRoot(const OUString& path, OUString& outRoot) const;

    OUString m_rootDir;
    std::vector<AuthorizedDirectory> m_dirs;
    PermissionState m_states[6] = {};
    OUString m_lastNetworkSummary;
    std::vector<PermissionEvent> m_events;
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
