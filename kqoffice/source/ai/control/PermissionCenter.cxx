/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (GA foundation: Permission Center).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "PermissionCenter.hxx"

#include <osl/file.hxx>
#include <osl/security.hxx>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cstdlib>
#include <vector>

namespace kqoffice::ai::control
{

namespace
{

OUString toFileUrl(const OUString& systemOrUrl);

sal_Int64 nowMs()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
         + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

OUString normalizePath(const OUString& path)
{
    OUString result = path;
    while (result.endsWith("/") && result.getLength() > 1)
        result = result.copy(0, result.getLength() - 1);
    return result;
}

bool canonicalPath(const OUString& path, OUString& canonicalOut)
{
    if (path.isEmpty())
        return false;

    const OUString fileUrl = toFileUrl(normalizePath(path));
    OUString resolvedUrl;
    if (osl::FileBase::getAbsoluteFileURL(u"file:///"_ustr, fileUrl, resolvedUrl)
        != osl::FileBase::E_None)
        return false;

    OUString systemPath;
    if (osl::FileBase::getSystemPathFromFileURL(resolvedUrl, systemPath)
        != osl::FileBase::E_None)
        return false;

    canonicalOut = normalizePath(systemPath);
    return !canonicalOut.isEmpty();
}

bool isUnderRoot(const OUString& path, const OUString& root, bool recursive)
{
    const OUString p = normalizePath(path);
    const OUString r = normalizePath(root);
    if (p == r)
        return true;
    if (!recursive)
        return false;
    const OUString prefix = r + u"/"_ustr;
    return p.startsWith(prefix);
}

sal_uInt8 capIndex(CapabilityPermission cap)
{
    return static_cast<sal_uInt8>(cap);
}

OUString toFileUrl(const OUString& systemOrUrl)
{
    if (systemOrUrl.startsWith("file://"))
        return systemOrUrl;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemOrUrl, url) == osl::FileBase::E_None
        && !url.isEmpty())
        return url;
    return systemOrUrl;
}

bool storeFileExists(const OUString& systemPath)
{
    osl::FileStatus st(osl_FileStatus_Mask_Type);
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(toFileUrl(systemPath), item) != osl::FileBase::E_None)
        return false;
    if (item.getFileStatus(st) != osl::FileBase::E_None)
        return false;
    return st.isValid(osl_FileStatus_Mask_Type);
}

/// Best-effort copy of legacy store into the XDG path (once).
void migrateLegacyStoreIfNeeded(const OUString& newRoot)
{
    const OUString newStore = newRoot + u"/permissions.tsv"_ustr;
    if (storeFileExists(newStore))
        return;

    osl::Security security;
    OUString legacyRoot;
    OUString configDir;
    if (security.getConfigDir(configDir) && !configDir.isEmpty())
        legacyRoot = normalizePath(configDir) + u"/KQOffice/AI/permissions"_ustr;
    else
    {
        OUString homeDir;
        if (security.getHomeDir(homeDir) && !homeDir.isEmpty())
            legacyRoot = normalizePath(homeDir) + u"/.kqoffice/ai/permissions"_ustr;
    }
    if (legacyRoot.isEmpty())
        return;

    const OUString legacyStore = legacyRoot + u"/permissions.tsv"_ustr;
    if (!storeFileExists(legacyStore))
        return;

    osl::Directory::createPath(toFileUrl(newRoot));
    if (osl::File::copy(toFileUrl(legacyStore), toFileUrl(newStore)) == osl::FileBase::E_None)
    {
        SAL_INFO("kqoffice.ai.control",
                 "PermissionCenter: migrated grants from " << legacyStore << " to " << newStore);
    }
}

bool isFsRiskActionId(const OUString& actionId)
{
    return actionId.startsWith("fs.delete@") || actionId.startsWith("fs.overwrite@");
}

} // namespace

PermissionCenter::PermissionCenter()
    : m_rootDir(resolveRootDir())
{
    for (auto& s : m_states)
        s = PermissionState::NotRequested;
    load();
}

PermissionCenter::PermissionCenter(const OUString& rootDir)
    : m_rootDir(rootDir)
{
    for (auto& s : m_states)
        s = PermissionState::NotRequested;
    load();
}

OUString PermissionCenter::resolveRootDir()
{
    if (const char* env = std::getenv("KQOFFICE_AI_PERMISSION_DIR"))
    {
        if (env[0] != '\0')
            return OUString::createFromAscii(env);
    }

    // Prefer XDG-style ~/.config/kqoffice/permissions (product requirement).
    osl::Security security;
    OUString homeDir;
    if (security.getHomeDir(homeDir) && !homeDir.isEmpty())
    {
        const OUString xdg = normalizePath(homeDir) + u"/.config/kqoffice/permissions"_ustr;
        migrateLegacyStoreIfNeeded(xdg);
        return xdg;
    }

    OUString configDir;
    if (security.getConfigDir(configDir) && !configDir.isEmpty())
        return normalizePath(configDir) + u"/KQOffice/AI/permissions"_ustr;

    return u".config/kqoffice/permissions"_ustr;
}

OUString PermissionCenter::storePath() const
{
    return m_rootDir + u"/permissions.tsv"_ustr;
}

OUString PermissionCenter::storePathForDisplay() const
{
    return storePath();
}

OUString PermissionCenter::riskSessionActionId(RiskOperation op, const OUString& authorizedRoot)
{
    const OUString root = normalizePath(authorizedRoot);
    switch (op)
    {
        case RiskOperation::Delete:
            return u"fs.delete@"_ustr + root;
        case RiskOperation::Overwrite:
            return u"fs.overwrite@"_ustr + root;
    }
    return u"fs.risk@"_ustr + root;
}

bool PermissionCenter::findMatchingRoot(const OUString& path, OUString& outRoot) const
{
    OUString canonical;
    if (!canonicalPath(path, canonical))
        return false;

    // Prefer the longest matching authorized root (most specific grant).
    OUString best;
    for (const auto& d : m_dirs)
    {
        OUString rootCanon;
        if (!canonicalPath(d.path, rootCanon))
            continue;
        if (!isUnderRoot(canonical, rootCanon, d.recursive))
            continue;
        if (best.isEmpty() || rootCanon.getLength() > best.getLength())
            best = rootCanon;
    }
    if (best.isEmpty())
        return false;
    outRoot = best;
    return true;
}

bool PermissionCenter::isPathAuthorized(const OUString& path) const
{
    OUString root;
    return findMatchingRoot(path, root);
}

OUString PermissionCenter::matchingAuthorizedRoot(const OUString& path) const
{
    OUString root;
    findMatchingRoot(path, root);
    return root;
}

bool PermissionCenter::grantDirectory(const OUString& path, bool recursive)
{
    OUString normalized;
    if (!canonicalPath(path, normalized))
        return false;

    // Refuse home / root-like broad grants as a single click (minimum privilege).
    osl::Security security;
    OUString homeDir;
    if (security.getHomeDir(homeDir) && !homeDir.isEmpty())
    {
        const OUString home = normalizePath(homeDir);
        if (normalized == home)
        {
            SAL_WARN("kqoffice.ai.control",
                     "PermissionCenter: refusing to grant entire home directory");
            appendEvent(CapabilityPermission::FolderScan, PermissionState::Denied,
                        u"refuse-home:"_ustr + normalized);
            save();
            return false;
        }
    }
    if (normalized == u"/"_ustr || normalized == u"/Users"_ustr || normalized == u"/home"_ustr)
    {
        appendEvent(CapabilityPermission::FolderScan, PermissionState::Denied,
                    u"refuse-broad:"_ustr + normalized);
        save();
        return false;
    }

    for (auto& d : m_dirs)
    {
        if (normalizePath(d.path) == normalized)
        {
            d.recursive = recursive;
            d.grantedAtMs = nowMs();
            appendEvent(CapabilityPermission::FolderScan, PermissionState::Granted,
                        u"refresh:"_ustr + normalized);
            save();
            return true;
        }
    }

    AuthorizedDirectory entry;
    entry.path = normalized;
    entry.grantedAtMs = nowMs();
    entry.recursive = recursive;
    m_dirs.push_back(entry);
    m_states[capIndex(CapabilityPermission::FolderScan)] = PermissionState::Granted;
    appendEvent(CapabilityPermission::FolderScan, PermissionState::Granted, normalized);
    return save();
}

bool PermissionCenter::revokeDirectory(const OUString& path)
{
    OUString normalized;
    if (!canonicalPath(path, normalized))
        normalized = normalizePath(path);
    bool removed = false;
    std::vector<AuthorizedDirectory> kept;
    kept.reserve(m_dirs.size());
    for (const auto& d : m_dirs)
    {
        if (normalizePath(d.path) == normalized)
        {
            removed = true;
            continue;
        }
        kept.push_back(d);
    }
    if (!removed)
        return false;
    m_dirs.swap(kept);
    if (m_dirs.empty())
        m_states[capIndex(CapabilityPermission::FolderScan)] = PermissionState::Revoked;
    // Drop process session risk grants tied to the revoked root.
    PermissionGrant::revokeSession(riskSessionActionId(RiskOperation::Delete, normalized));
    PermissionGrant::revokeSession(riskSessionActionId(RiskOperation::Overwrite, normalized));
    appendEvent(CapabilityPermission::FolderScan, PermissionState::Revoked, normalized);
    return save();
}

bool PermissionCenter::revokeAllDirectories()
{
    if (m_dirs.empty())
    {
        clearSessionRiskGrants();
        return true;
    }
    for (const auto& d : m_dirs)
    {
        PermissionGrant::revokeSession(riskSessionActionId(RiskOperation::Delete, d.path));
        PermissionGrant::revokeSession(riskSessionActionId(RiskOperation::Overwrite, d.path));
    }
    m_dirs.clear();
    m_states[capIndex(CapabilityPermission::FolderScan)] = PermissionState::Revoked;
    appendEvent(CapabilityPermission::FolderScan, PermissionState::Revoked, u"revoke-all"_ustr);
    return save();
}

std::vector<AuthorizedDirectory> PermissionCenter::authorizedDirectories() const
{
    return m_dirs;
}

bool PermissionCenter::hasAnyAuthorizedDirectory() const
{
    return !m_dirs.empty();
}

RiskDecision PermissionCenter::evaluateRisk(RiskOperation op, const OUString& path) const
{
    RiskDecision d;
    OUString root;
    if (!findMatchingRoot(path, root))
    {
        d.pathAuthorized = false;
        d.needsUserConfirm = false;
        d.reasonZh = u"路径不在已授权工作区内，已拒绝（最小权限）。"_ustr;
        return d;
    }
    d.pathAuthorized = true;
    d.matchedRoot = root;
    d.sessionActionId = riskSessionActionId(op, root);
    if (PermissionGrant::isSessionAllowed(d.sessionActionId))
    {
        d.sessionAlreadyAllows = true;
        d.needsUserConfirm = false;
        d.reasonZh = u"本轮已授权该工作区的"_ustr + riskOperationLabelZh(op)
                     + u"操作。"_ustr;
        return d;
    }
    d.needsUserConfirm = true;
    d.reasonZh = riskPromptZh(op, path);
    return d;
}

bool PermissionCenter::needsRiskConfirm(RiskOperation op, const OUString& path) const
{
    const RiskDecision d = evaluateRisk(op, path);
    return d.needsUserConfirm;
}

bool PermissionCenter::hasSessionRiskGrant(RiskOperation op, const OUString& path) const
{
    OUString root;
    if (!findMatchingRoot(path, root))
        return false;
    return PermissionGrant::isSessionAllowed(riskSessionActionId(op, root));
}

bool PermissionCenter::resolveRiskyOp(RiskOperation op, const OUString& path,
                                      PermissionDecision choice)
{
    const RiskDecision decision = evaluateRisk(op, path);
    if (!decision.pathAuthorized)
    {
        appendEvent(CapabilityPermission::FolderScan, PermissionState::Denied,
                    u"risk-unauthorized:"_ustr + riskOperationLabelZh(op) + u":"_ustr + path);
        save();
        return false;
    }
    if (decision.sessionAlreadyAllows)
        return true;

    if (choice == PermissionDecision::AllowSession)
    {
        PermissionGrant::applyDecision(decision.sessionActionId, PermissionDecision::AllowSession);
        appendEvent(CapabilityPermission::FolderScan, PermissionState::Granted,
                    u"risk-session:"_ustr + riskOperationLabelZh(op) + u":"_ustr
                        + decision.matchedRoot);
        save();
        return true;
    }
    if (choice == PermissionDecision::AllowOnce)
    {
        PermissionGrant::applyDecision(decision.sessionActionId, PermissionDecision::AllowOnce);
        appendEvent(CapabilityPermission::FolderScan, PermissionState::Granted,
                    u"risk-once:"_ustr + riskOperationLabelZh(op) + u":"_ustr + path);
        save();
        return true;
    }

    appendEvent(CapabilityPermission::FolderScan, PermissionState::Denied,
                u"risk-deny:"_ustr + riskOperationLabelZh(op) + u":"_ustr + path);
    save();
    return false;
}

void PermissionCenter::clearSessionRiskGrants()
{
    const auto all = PermissionGrant::sessionAllowedActions();
    sal_Int32 cleared = 0;
    for (const auto& id : all)
    {
        if (isFsRiskActionId(id))
        {
            PermissionGrant::revokeSession(id);
            ++cleared;
        }
    }
    if (cleared > 0)
    {
        appendEvent(CapabilityPermission::FolderScan, PermissionState::Revoked,
                    u"risk-session-clear:"_ustr + OUString::number(cleared));
        save();
    }
}

sal_Int32 PermissionCenter::sessionRiskGrantCount() const
{
    sal_Int32 n = 0;
    for (const auto& id : PermissionGrant::sessionAllowedActions())
    {
        if (isFsRiskActionId(id))
            ++n;
    }
    return n;
}

PermissionState PermissionCenter::stateOf(CapabilityPermission cap) const
{
    return m_states[capIndex(cap)];
}

bool PermissionCenter::request(CapabilityPermission cap, const OUString& reasonZh)
{
    // First-use request records intent; UI is responsible for OS prompt.
    if (m_states[capIndex(cap)] == PermissionState::Granted)
        return true;
    m_states[capIndex(cap)] = PermissionState::NotRequested;
    appendEvent(cap, PermissionState::NotRequested,
                reasonZh.isEmpty() ? u"request"_ustr : reasonZh);
    return save();
}

bool PermissionCenter::grant(CapabilityPermission cap)
{
    m_states[capIndex(cap)] = PermissionState::Granted;
    appendEvent(cap, PermissionState::Granted, u"grant"_ustr);
    return save();
}

bool PermissionCenter::revoke(CapabilityPermission cap)
{
    m_states[capIndex(cap)] = PermissionState::Revoked;
    if (cap == CapabilityPermission::FolderScan)
    {
        m_dirs.clear();
        clearSessionRiskGrants();
    }
    appendEvent(cap, PermissionState::Revoked, u"revoke"_ustr);
    return save();
}

bool PermissionCenter::isGranted(CapabilityPermission cap) const
{
    return m_states[capIndex(cap)] == PermissionState::Granted;
}

bool PermissionCenter::discloseNetworkSend(const NetworkDisclosure& disclosure)
{
    if (!isGranted(CapabilityPermission::NetworkEgress))
    {
        appendEvent(CapabilityPermission::NetworkEgress, PermissionState::Denied,
                    u"blocked-no-grant"_ustr);
        return false;
    }
    if (!disclosure.userConfirmed)
    {
        appendEvent(CapabilityPermission::NetworkEgress, PermissionState::Denied,
                    u"blocked-no-confirm"_ustr);
        return false;
    }
    if (disclosure.provider.isEmpty() || disclosure.scopeSummaryZh.isEmpty())
    {
        appendEvent(CapabilityPermission::NetworkEgress, PermissionState::Denied,
                    u"blocked-incomplete-disclosure"_ustr);
        return false;
    }

    m_lastNetworkSummary = u"Provider="_ustr + disclosure.provider
        + u"; Endpoint="_ustr
        + (disclosure.endpoint.isEmpty() ? u"(local)"_ustr : disclosure.endpoint)
        + u"; Scope="_ustr + disclosure.scopeSummaryZh;
    appendEvent(CapabilityPermission::NetworkEgress, PermissionState::Granted,
                m_lastNetworkSummary);
    save();
    return true;
}

OUString PermissionCenter::lastNetworkDisclosureSummary() const
{
    return m_lastNetworkSummary;
}

OUString PermissionCenter::settingsCapabilityStatusZh(CapabilityPermission cap) const
{
    const PermissionState st = stateOf(cap);
    const OUString name = capabilityLabelZh(cap);

    // OS-gated capabilities: first use is an OS prompt, not an in-app silent grant.
    const bool osGated = (cap == CapabilityPermission::Microphone
                          || cap == CapabilityPermission::ScreenCapture
                          || cap == CapabilityPermission::Camera);

    switch (st)
    {
        case PermissionState::Granted:
            return name + u"：已授权（可在设置中撤销）"_ustr;
        case PermissionState::Denied:
            return name + u"：已拒绝"_ustr;
        case PermissionState::Revoked:
            return name + u"：已撤销"_ustr;
        case PermissionState::NotRequested:
        default:
            if (osGated)
                return name + u"：使用时系统会询问"_ustr;
            if (cap == CapabilityPermission::NetworkEgress)
                return name + u"：默认关闭，发送前会确认并披露"_ustr;
            if (cap == CapabilityPermission::FolderScan)
            {
                if (hasAnyAuthorizedDirectory())
                    return name + u"：已授权部分目录"_ustr;
                return name + u"：尚未授权目录（不会扫描全盘）"_ustr;
            }
            if (cap == CapabilityPermission::PluginRuntime)
                return name + u"：未启用（仅签名白名单）"_ustr;
            return name + u"：未请求"_ustr;
    }
}

OUString PermissionCenter::networkStatusLineZh() const
{
    const PermissionState st = stateOf(CapabilityPermission::NetworkEgress);
    OUString line = u"网络外发："_ustr + stateLabelZh(st);
    switch (st)
    {
        case PermissionState::Granted:
            line += u" · 仍须在发送前确认并披露提供方与范围"_ustr;
            break;
        case PermissionState::Denied:
            line += u" · 已拒绝外发"_ustr;
            break;
        case PermissionState::Revoked:
            line += u" · 已关闭，下次外发需重新授权"_ustr;
            break;
        case PermissionState::NotRequested:
        default:
            line += u" · 默认关闭，发送前会确认并披露"_ustr;
            break;
    }
    if (!m_lastNetworkSummary.isEmpty())
        line += u" · 最近披露: "_ustr + m_lastNetworkSummary;
    return line;
}

OUString PermissionCenter::riskPolicyHintZh()
{
    return u"高风险文件操作（删除 / 覆盖）在已授权目录内仍须确认："_ustr
           + riskConfirmationLabelZh(PermissionDecision::Deny) + u" / "_ustr
           + riskConfirmationLabelZh(PermissionDecision::AllowOnce) + u" / "_ustr
           + riskConfirmationLabelZh(PermissionDecision::AllowSession)
           + u"。"_ustr;
}

OUString PermissionCenter::settingsSurfaceSummaryZh() const
{
    OUStringBuffer buf;
    // Folders
    const auto dirs = authorizedDirectories();
    if (dirs.empty())
        buf.append(u"工作区：尚未授权目录（默认不扫全盘）\n"_ustr);
    else
    {
        buf.append(u"工作区：已授权 "_ustr);
        buf.append(OUString::number(static_cast<sal_Int32>(dirs.size())));
        buf.append(u" 个目录\n"_ustr);
    }
    buf.append(networkStatusLineZh());
    buf.append(u'\n');
    buf.append(settingsCapabilityStatusZh(CapabilityPermission::Microphone));
    buf.append(u'\n');
    buf.append(settingsCapabilityStatusZh(CapabilityPermission::ScreenCapture));
    buf.append(u'\n');
    buf.append(riskPolicyHintZh());
    const sal_Int32 sessionN = sessionRiskGrantCount();
    if (sessionN > 0)
    {
        buf.append(u"\n本轮写删授权 "_ustr);
        buf.append(OUString::number(sessionN));
        buf.append(u" 条（进程内，可清空）"_ustr);
    }
    return buf.makeStringAndClear();
}

bool PermissionCenter::load()
{
    m_dirs.clear();
    m_events.clear();
    m_lastNetworkSummary.clear();
    for (auto& s : m_states)
        s = PermissionState::NotRequested;

    OUString content;
    osl::File file(toFileUrl(storePath()));
    if (file.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return true; // first run

    sal_uInt64 size = 0;
    file.getSize(size);
    if (size == 0)
    {
        file.close();
        return true;
    }
    std::vector<char> buf(static_cast<size_t>(size) + 1, 0);
    sal_uInt64 read = 0;
    file.read(buf.data(), size, read);
    file.close();
    content = OUString(buf.data(), static_cast<sal_Int32>(read), RTL_TEXTENCODING_UTF8);

    sal_Int32 from = 0;
    while (from <= content.getLength())
    {
        sal_Int32 to = content.indexOf('\n', from);
        if (to < 0)
            to = content.getLength();
        OUString line = content.copy(from, to - from).trim();
        from = to + 1;
        if (line.isEmpty() || line.startsWith("#"))
            continue;

        // dir\tpath\trecursive\tgrantedAt
        // state\tcap\tstate
        // net\tsummary
        // event\tcap\tstate\tat\tdetail
        const sal_Int32 t1 = line.indexOf('\t');
        if (t1 < 0)
            continue;
        const OUString kind = line.copy(0, t1);
        const OUString rest = line.copy(t1 + 1);

        if (kind == "dir")
        {
            const sal_Int32 p1 = rest.indexOf('\t');
            if (p1 < 0)
                continue;
            const sal_Int32 p2 = rest.indexOf('\t', p1 + 1);
            AuthorizedDirectory d;
            d.path = rest.copy(0, p1);
            d.recursive = (p2 < 0) ? true : rest.copy(p1 + 1, p2 - p1 - 1) != u"0"_ustr;
            d.grantedAtMs = (p2 < 0) ? 0 : rest.copy(p2 + 1).toInt64();
            m_dirs.push_back(d);
            m_states[capIndex(CapabilityPermission::FolderScan)] = PermissionState::Granted;
        }
        else if (kind == "state")
        {
            const sal_Int32 p1 = rest.indexOf('\t');
            if (p1 < 0)
                continue;
            const sal_Int32 cap = rest.copy(0, p1).toInt32();
            const sal_Int32 st = rest.copy(p1 + 1).toInt32();
            if (cap >= 0 && cap <= 5 && st >= 0 && st <= 3)
                m_states[cap] = static_cast<PermissionState>(st);
        }
        else if (kind == "net")
        {
            m_lastNetworkSummary = rest;
        }
        else if (kind == "event")
        {
            const sal_Int32 p1 = rest.indexOf('\t');
            const sal_Int32 p2 = (p1 < 0) ? -1 : rest.indexOf('\t', p1 + 1);
            const sal_Int32 p3 = (p2 < 0) ? -1 : rest.indexOf('\t', p2 + 1);
            if (p1 < 0 || p2 < 0 || p3 < 0)
                continue;
            PermissionEvent ev;
            const sal_Int32 cap = rest.copy(0, p1).toInt32();
            const sal_Int32 st = rest.copy(p1 + 1, p2 - p1 - 1).toInt32();
            if (cap < 0 || cap > 5 || st < 0 || st > 3)
                continue;
            ev.capability = static_cast<CapabilityPermission>(cap);
            ev.state = static_cast<PermissionState>(st);
            ev.atMs = rest.copy(p2 + 1, p3 - p2 - 1).toInt64();
            ev.detail = rest.copy(p3 + 1);
            m_events.push_back(ev);
        }
    }
    return true;
}

bool PermissionCenter::save() const
{
    const OUString dirUrl = toFileUrl(m_rootDir);
    osl::Directory::createPath(dirUrl);

    OUString body
        = u"# kqoffice permission store v2 (dirs+caps; session risks via PermissionGrant, not persisted)\n"_ustr;
    for (const auto& d : m_dirs)
    {
        body += u"dir\t"_ustr + d.path + u"\t"_ustr
            + (d.recursive ? u"1"_ustr : u"0"_ustr) + u"\t"_ustr
            + OUString::number(d.grantedAtMs) + u"\n"_ustr;
    }
    for (sal_Int32 i = 0; i < 6; ++i)
    {
        body += u"state\t"_ustr + OUString::number(i) + u"\t"_ustr
            + OUString::number(static_cast<sal_Int32>(m_states[i])) + u"\n"_ustr;
    }
    if (!m_lastNetworkSummary.isEmpty())
        body += u"net\t"_ustr + m_lastNetworkSummary + u"\n"_ustr;

    const sal_Int32 start
        = (static_cast<sal_Int32>(m_events.size()) > 200)
              ? static_cast<sal_Int32>(m_events.size()) - 200
              : 0;
    for (sal_Int32 i = start; i < static_cast<sal_Int32>(m_events.size()); ++i)
    {
        const auto& ev = m_events[static_cast<size_t>(i)];
        body += u"event\t"_ustr
            + OUString::number(static_cast<sal_Int32>(ev.capability)) + u"\t"_ustr
            + OUString::number(static_cast<sal_Int32>(ev.state)) + u"\t"_ustr
            + OUString::number(ev.atMs) + u"\t"_ustr + ev.detail + u"\n"_ustr;
    }

    const OUString path = toFileUrl(storePath());
    osl::File::remove(path);
    osl::File out(path);
    osl::FileBase::RC rc = out.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (rc != osl::FileBase::E_None)
    {
        // Retry without Create in case of races.
        rc = out.open(osl_File_OpenFlag_Write);
    }
    if (rc != osl::FileBase::E_None)
    {
        SAL_WARN("kqoffice.ai.control", "PermissionCenter: failed to write " << path);
        return false;
    }
    const OString utf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);
    sal_uInt64 written = 0;
    out.write(utf8.getStr(), utf8.getLength(), written);
    out.close();
    // Even if disk write fails in restricted environments, keep in-memory policy.
    // Callers still see updated grants for the process lifetime.
    if (written == 0)
        SAL_WARN("kqoffice.ai.control", "PermissionCenter: empty write for " << path);
    return true;
}

std::vector<PermissionEvent> PermissionCenter::recentEvents(sal_Int32 maxEvents) const
{
    if (maxEvents <= 0 || m_events.empty())
        return {};
    if (static_cast<sal_Int32>(m_events.size()) <= maxEvents)
        return m_events;
    return std::vector<PermissionEvent>(
        m_events.end() - maxEvents, m_events.end());
}

void PermissionCenter::appendEvent(CapabilityPermission cap, PermissionState state,
                                   const OUString& detail)
{
    PermissionEvent ev;
    ev.capability = cap;
    ev.state = state;
    ev.detail = detail;
    ev.atMs = nowMs();
    m_events.push_back(ev);
    if (m_events.size() > 500)
        m_events.erase(m_events.begin(), m_events.begin() + 100);
}

OUString PermissionCenter::capabilityLabelZh(CapabilityPermission cap)
{
    switch (cap)
    {
        case CapabilityPermission::FolderScan:
            return u"文件夹扫描"_ustr;
        case CapabilityPermission::NetworkEgress:
            return u"网络外发"_ustr;
        case CapabilityPermission::Microphone:
            return u"麦克风"_ustr;
        case CapabilityPermission::ScreenCapture:
            return u"屏幕截图"_ustr;
        case CapabilityPermission::Camera:
            return u"相机"_ustr;
        case CapabilityPermission::PluginRuntime:
            return u"插件运行"_ustr;
    }
    return u"未知权限"_ustr;
}

OUString PermissionCenter::stateLabelZh(PermissionState state)
{
    switch (state)
    {
        case PermissionState::NotRequested:
            return u"未请求"_ustr;
        case PermissionState::Denied:
            return u"已拒绝"_ustr;
        case PermissionState::Granted:
            return u"已授权"_ustr;
        case PermissionState::Revoked:
            return u"已撤销"_ustr;
    }
    return u"未知"_ustr;
}

OUString PermissionCenter::riskOperationLabelZh(RiskOperation op)
{
    switch (op)
    {
        case RiskOperation::Delete:
            return u"删除"_ustr;
        case RiskOperation::Overwrite:
            return u"覆盖写入"_ustr;
    }
    return u"高风险操作"_ustr;
}

OUString PermissionCenter::riskConfirmationLabelZh(PermissionDecision choice)
{
    return PermissionGrant::decisionLabelZh(choice);
}

OUString PermissionCenter::riskPromptZh(RiskOperation op, const OUString& path)
{
    return u"即将在已授权工作区执行「"_ustr + riskOperationLabelZh(op)
        + u"」：\n"_ustr + path
        + u"\n\n请选择："_ustr + riskConfirmationLabelZh(PermissionDecision::Deny)
        + u" / "_ustr + riskConfirmationLabelZh(PermissionDecision::AllowOnce)
        + u" / "_ustr + riskConfirmationLabelZh(PermissionDecision::AllowSession)
        + u"（本轮内同类操作不再询问）。"_ustr;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
