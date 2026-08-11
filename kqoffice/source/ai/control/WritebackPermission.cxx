/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: document write-back ladder).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "WritebackPermission.hxx"

#include <cstdlib>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::control
{

namespace
{

OUString normalizeSurface(const OUString& surface)
{
    if (surface.isEmpty())
        return u"unknown"_ustr;
    OUString s = surface.toAsciiLowerCase();
    if (s == u"swriter" || s == u"text" || s == u"writer")
        return u"writer"_ustr;
    if (s == u"scalc" || s == u"spreadsheet" || s == u"calc")
        return u"calc"_ustr;
    if (s == u"simpress" || s == u"sdraw" || s == u"presentation" || s == u"impress")
        return u"impress"_ustr;
    return s;
}

OUString sanitizeDocKey(const OUString& docKey)
{
    if (docKey.isEmpty())
        return {};
    // Keep short stable token; drop path separators / whitespace (no secrets).
    OUStringBuffer b;
    const sal_Int32 n = docKey.getLength();
    for (sal_Int32 i = 0; i < n && b.getLength() < 64; ++i)
    {
        const sal_Unicode c = docKey[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.')
            b.append(c);
        else if (c == '/' || c == '\\' || c == ':' || c == ' ')
            b.append('-');
    }
    return b.makeStringAndClear();
}

OUString scopeToken(WritebackScope scope)
{
    switch (scope)
    {
        case WritebackScope::Selection:
            return u"selection"_ustr;
        case WritebackScope::Document:
            return u"document"_ustr;
        case WritebackScope::Workspace:
            return u"workspace"_ustr;
    }
    return u"selection"_ustr;
}

bool envTruthy(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return false;
    const OString s(v);
    return s.equalsIgnoreAsciiCase("1") || s.equalsIgnoreAsciiCase("true")
           || s.equalsIgnoreAsciiCase("yes") || s.equalsIgnoreAsciiCase("on");
}

} // namespace

WritebackTier WritebackPermission::defaultTier()
{
    return WritebackTier::Ask;
}

bool WritebackPermission::yoloEnabled()
{
    return envTruthy("KQOFFICE_AI_WRITEBACK_YOLO");
}

OUString WritebackPermission::actionId(WritebackScope scope, const OUString& surface,
                                       const OUString& docKey)
{
    OUString id = u"writeback."_ustr + scopeToken(scope) + u"@"_ustr + normalizeSurface(surface);
    const OUString key = sanitizeDocKey(docKey);
    if (!key.isEmpty())
        id += u"#"_ustr + key;
    return id;
}

WritebackScope WritebackPermission::scopeFromPlanTargets(bool bAnySelectionTarget,
                                                         bool bWorkspacePathOp)
{
    if (bWorkspacePathOp)
        return WritebackScope::Workspace;
    if (bAnySelectionTarget)
        return WritebackScope::Selection;
    return WritebackScope::Document;
}

WritebackGateResult WritebackPermission::evaluate(const WritebackGateRequest& req)
{
    WritebackGateResult out;
    out.actionId = actionId(req.scope, req.surface, req.docKey);
    out.effectiveTier = defaultTier();

    if (yoloEnabled())
    {
        out.allowed = true;
        out.needsUi = false;
        out.effectiveTier = WritebackTier::Yolo;
        out.decision = PermissionDecision::AllowSession;
        out.fromYolo = true;
        out.reasonZh = u"YOLO 已开启（KQOFFICE_AI_WRITEBACK_YOLO）· 跳过确认 · 仅测试/托管环境"_ustr;
        out.errorCode = u"writeback-yolo"_ustr;
        return out;
    }

    if (auto cached = PermissionGrant::tryAutoAllow(out.actionId))
    {
        out.allowed = true;
        out.needsUi = false;
        out.effectiveTier = WritebackTier::AllowSession;
        out.decision = *cached;
        out.fromSessionCache = true;
        out.reasonZh = u"本轮已允许写回 · "_ustr + scopeLabelZh(req.scope) + u" · "
                       + surfaceLabelZh(req.surface);
        out.errorCode = u"writeback-session-allowed"_ustr;
        return out;
    }

    if (req.explicitHumanApproval)
    {
        out.allowed = true;
        out.needsUi = false;
        out.effectiveTier = WritebackTier::AllowOnce;
        out.decision = PermissionDecision::AllowOnce;
        out.reasonZh = u"已获明确人工批准 · 仅本次写回"_ustr;
        out.errorCode = u"writeback-human-approval"_ustr;
        return out;
    }

    // Headless path: honor uiDecision without separate resolve() call.
    if (req.uiDecision == PermissionDecision::AllowOnce)
    {
        out.allowed = true;
        out.needsUi = false;
        out.effectiveTier = WritebackTier::AllowOnce;
        out.decision = PermissionDecision::AllowOnce;
        out.reasonZh = u"用户选择仅本次 · 主文档将按计划写回"_ustr;
        out.errorCode = u"writeback-allow-once"_ustr;
        return out;
    }
    if (req.uiDecision == PermissionDecision::AllowSession)
    {
        PermissionGrant::applyDecision(out.actionId, PermissionDecision::AllowSession);
        out.allowed = true;
        out.needsUi = false;
        out.effectiveTier = WritebackTier::AllowSession;
        out.decision = PermissionDecision::AllowSession;
        out.reasonZh = u"用户选择本轮对话均允许 · 同范围后续写回不再询问"_ustr;
        out.errorCode = u"writeback-allow-session"_ustr;
        return out;
    }

    out.allowed = false;
    out.needsUi = true;
    out.effectiveTier = WritebackTier::Ask;
    out.decision = PermissionDecision::Deny;
    out.reasonZh = u"需要确认 · 默认 Ask · 请选择拒绝 / 仅本次 / 本轮对话均允许 · 主文档未改"_ustr;
    out.errorCode = u"human-approval-required"_ustr;
    return out;
}

WritebackGateResult WritebackPermission::resolve(const WritebackGateRequest& req,
                                                 PermissionDecision choice)
{
    WritebackGateRequest r = req;
    r.uiDecision = choice;
    r.explicitHumanApproval = false;
    if (choice == PermissionDecision::Deny)
    {
        WritebackGateResult out;
        out.actionId = actionId(req.scope, req.surface, req.docKey);
        out.allowed = false;
        out.needsUi = false;
        out.decision = PermissionDecision::Deny;
        out.effectiveTier = WritebackTier::Ask;
        out.reasonZh = u"用户拒绝写回 · 主文档未改"_ustr;
        out.errorCode = u"writeback-denied"_ustr;
        return out;
    }
    if (choice == PermissionDecision::AllowSession)
        PermissionGrant::applyDecision(actionId(req.scope, req.surface, req.docKey),
                                       PermissionDecision::AllowSession);
    return evaluate(r);
}

bool WritebackPermission::mayApply(const WritebackGateRequest& req)
{
    return evaluate(req).allowed;
}

void WritebackPermission::clearSessionWritebackGrants()
{
    const auto all = PermissionGrant::sessionAllowedActions();
    for (const auto& id : all)
    {
        if (id.startsWith(u"writeback."))
            PermissionGrant::revokeSession(id);
    }
}

OUString WritebackPermission::scopeLabelZh(WritebackScope scope)
{
    switch (scope)
    {
        case WritebackScope::Selection:
            return u"选区"_ustr;
        case WritebackScope::Document:
            return u"当前文档"_ustr;
        case WritebackScope::Workspace:
            return u"资料盘/工作区"_ustr;
    }
    return u"选区"_ustr;
}

OUString WritebackPermission::tierLabelZh(WritebackTier tier)
{
    switch (tier)
    {
        case WritebackTier::Ask:
            return u"询问（默认）"_ustr;
        case WritebackTier::AllowOnce:
            return u"仅本次"_ustr;
        case WritebackTier::AllowSession:
            return u"本轮对话均允许"_ustr;
        case WritebackTier::Yolo:
            return u"全自动（YOLO）"_ustr;
    }
    return u"询问（默认）"_ustr;
}

OUString WritebackPermission::surfaceLabelZh(const OUString& surface)
{
    const OUString s = normalizeSurface(surface);
    if (s == u"writer")
        return u"文字"_ustr;
    if (s == u"calc")
        return u"表格"_ustr;
    if (s == u"impress")
        return u"演示"_ustr;
    return u"文档"_ustr;
}

ClarificationPrompt WritebackPermission::clarifyPrompt(const WritebackGateRequest& req)
{
    ClarificationPrompt p;
    p.actionId = actionId(req.scope, req.surface, req.docKey);
    p.messageZh = u"AI 将修改"_ustr + surfaceLabelZh(req.surface) + u"的"_ustr
                  + scopeLabelZh(req.scope)
                  + u"。可撤销写回；默认需确认。请选择："_ustr
                  + PermissionGrant::decisionLabelZh(PermissionDecision::Deny) + u" / "_ustr
                  + PermissionGrant::decisionLabelZh(PermissionDecision::AllowOnce) + u" / "_ustr
                  + PermissionGrant::decisionLabelZh(PermissionDecision::AllowSession);
    return p;
}

OUString WritebackPermission::policyHintZh()
{
    return u"文档写回默认 Ask：拒绝 / 仅本次 / 本轮对话均允许。"
           "不默认 YOLO；主文档在批准前不改。"_ustr;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
