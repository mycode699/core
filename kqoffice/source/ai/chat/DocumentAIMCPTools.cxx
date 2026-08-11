/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "DocumentAIMCPTools.hxx"

#include "DocumentAIApply.hxx"
#include "DocumentAIDocumentTools.hxx"
#include "DocumentAIEnterpriseConnectors.hxx"
#include "DocumentAIFormulaDryRun.hxx"
#include "DocumentAIVerify.hxx"
#include "DocumentAIVisionEvidence.hxx"
#include "AgentChatSelectionCapture.hxx"
#include "WritebackPermission.hxx"
#include "ErrorClassifier.hxx"
#include "ExternalWriteDenier.hxx"
#include "EventLedger.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::chat
{
namespace
{
OUString lower(const OUString& s) { return s.toAsciiLowerCase(); }

sal_Int32 parseIntArg(const OUString& args, const OUString& key, sal_Int32 def)
{
    // key=123 or "key":123
    const OUString k1 = key + u"="_ustr;
    sal_Int32 p = args.indexOf(k1);
    if (p < 0)
    {
        const OUString k2 = u"\""_ustr + key + u"\""_ustr;
        p = args.indexOf(k2);
        if (p < 0)
            return def;
        p = args.indexOf(u':', p);
        if (p < 0)
            return def;
        ++p;
    }
    else
        p += k1.getLength();
    while (p < args.getLength() && (args[p] == u' ' || args[p] == u'"'))
        ++p;
    sal_Int32 end = p;
    while (end < args.getLength() && args[end] >= u'0' && args[end] <= u'9')
        ++end;
    if (end == p)
        return def;
    return args.copy(p, end - p).toInt32();
}

OUString parseStringArg(const OUString& args, const OUString& key)
{
    // "key":"value" or key=value
    const OUString quoted = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = args.indexOf(quoted);
    if (p >= 0)
    {
        p = args.indexOf(u':', p + quoted.getLength());
        if (p < 0)
            return OUString();
        sal_Int32 q1 = args.indexOf(u'"', p + 1);
        if (q1 < 0)
            return OUString();
        sal_Int32 q2 = q1 + 1;
        while (q2 < args.getLength())
        {
            if (args[q2] == u'\\' && q2 + 1 < args.getLength())
            {
                q2 += 2;
                continue;
            }
            if (args[q2] == u'"')
                break;
            ++q2;
        }
        if (q2 >= args.getLength())
            return OUString();
        return args.copy(q1 + 1, q2 - q1 - 1);
    }
    const OUString k1 = key + u"="_ustr;
    p = args.indexOf(k1);
    if (p < 0)
        return OUString();
    p += k1.getLength();
    sal_Int32 end = p;
    while (end < args.getLength() && args[end] != u' ' && args[end] != u',' && args[end] != u'}'
           && args[end] != u'\n' && args[end] != u'\r')
        ++end;
    return args.copy(p, end - p);
}

bool parseBoolArg(const OUString& args, const OUString& key, bool def)
{
    const OUString quoted = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = args.indexOf(quoted);
    if (p < 0)
    {
        const OUString k1 = key + u"="_ustr;
        p = args.indexOf(k1);
        if (p < 0)
            return def;
        p += k1.getLength();
    }
    else
    {
        p = args.indexOf(u':', p + quoted.getLength());
        if (p < 0)
            return def;
        ++p;
    }
    while (p < args.getLength() && (args[p] == u' ' || args[p] == u'"' || args[p] == u'\t'))
        ++p;
    if (p >= args.getLength())
        return def;
    const OUString rest = args.copy(p);
    if (rest.startsWith(u"true"_ustr) || rest.startsWith(u"1"_ustr) || rest.startsWith(u"yes"_ustr))
        return true;
    if (rest.startsWith(u"false"_ustr) || rest.startsWith(u"0"_ustr) || rest.startsWith(u"no"_ustr))
        return false;
    return def;
}

ApplyPlan planFromContent(const OUString& content)
{
    ApplyPlan plan = AgentChatDiffExtractor::extract(content);
    plan.rawOutput = content;
    if (!AgentChatDiffExtractor::validate(plan))
    {
        if (AgentChatDiffExtractor::looksLikeCalcFormulaWriteback(content))
            plan = AgentChatDiffExtractor::extractCalcFormulaWritebackPlan(content);
        else if (AgentChatDiffExtractor::looksLikeCalcCleanWriteback(content))
            plan = AgentChatDiffExtractor::extractCalcCleanWritebackPlan(content);
        else if (AgentChatDiffExtractor::looksLikeReviewFixList(content))
            plan = AgentChatDiffExtractor::extractReviewFixPlan(content);
        else if (AgentChatDiffExtractor::looksLikeWriterHeadingOutline(content))
            plan = AgentChatDiffExtractor::extractWriterHeadingOutlinePlan(content);
        else if (AgentChatDiffExtractor::looksLikeImpressNotesWriteback(content))
            plan = AgentChatDiffExtractor::extractImpressNotesWritebackPlan(content);
        else if (AgentChatDiffExtractor::looksLikeOutlineSlideContent(content))
            plan = AgentChatDiffExtractor::extractOutlineSlidePlan(content);
        plan.rawOutput = content;
    }
    return plan;
}
} // namespace

OUString DocumentAIMCPTools::schemaVersion() { return u"kqoffice-mcp-v0.1"_ustr; }

std::vector<MCPToolDescriptor> DocumentAIMCPTools::listTools()
{
    std::vector<MCPToolDescriptor> t;
    auto add = [&](const OUString& name, const OUString& zh, bool mut, bool appr) {
        MCPToolDescriptor d;
        d.name = name;
        d.descriptionZh = zh;
        d.mutatesDocument = mut;
        d.requiresHumanApproval = appr;
        t.push_back(d);
    };
    add(u"read_skeleton"_ustr, u"读取当前文档骨架（只读）"_ustr, false, false);
    add(u"read_blocks"_ustr, u"按块索引读取正文片段（只读）"_ustr, false, false);
    add(u"snapshot_hash"_ustr, u"当前文档结构快照哈希（只读）"_ustr, false, false);
    add(u"formula_dry_run"_ustr, u"公式静态 dry-run + 纯数字沙箱求值（不写表）"_ustr, false, false);
    add(u"verify_plan"_ustr, u"写回前软校验计划（不写主文档）"_ustr, false, false);
    add(u"apply_preview"_ustr, u"解析并预览 ApplyPlan（仅暂存语义，不写主文档）"_ustr, false,
        false);
    add(u"apply_approved"_ustr, u"在 humanApproval=true 时执行写回（可撤销）"_ustr, true, true);
    add(u"list_connectors"_ustr, u"列出企业连接器（默认关·只读·不外联）"_ustr, false, false);
    add(u"connector_status"_ustr, u"企业连接器总开关与授权状态（本地）"_ustr, false, false);
    add(u"connector_invoke"_ustr,
        u"调用企业连接器（须总开关+启用+授权+explicitUserApproval；private GET/POST）"_ustr, false,
        true);
    add(u"connector_device_start"_ustr,
        u"启动 private OAuth 设备码（须门禁+批准；用户在验证页输入代码）"_ustr, false, true);
    add(u"connector_device_poll"_ustr,
        u"轮询设备码令牌（成功则写入本地 secrets；须批准）"_ustr, false, true);
    add(u"vision_status"_ustr, u"本机 Vision 模型路由状态（不上传·不外联）"_ustr, false, false);
    add(u"list_tools"_ustr, u"列出本机 MCP 工具目录"_ustr, false, false);
    return t;
}

OUString DocumentAIMCPTools::listToolNamesJson()
{
    OUStringBuffer b;
    b.append(u"["_ustr);
    bool first = true;
    for (const auto& d : listTools())
    {
        if (!first)
            b.append(u","_ustr);
        first = false;
        b.append(u"\""_ustr);
        b.append(d.name);
        b.append(u"\""_ustr);
    }
    b.append(u"]"_ustr);
    return b.makeStringAndClear();
}

MCPToolResult DocumentAIMCPTools::toolReadSkeleton(sal_Int32 nMaxBlocks)
{
    MCPToolResult r;
    r.toolName = u"read_skeleton"_ustr;
    r.mainDocumentMutation = false;
    const auto sk = DocumentAIDocumentTools::buildSkeleton(nMaxBlocks);
    if (!sk.hasDocument)
    {
        r.success = false;
        r.error = u"no-document"_ustr;
        r.summaryZh = u"无打开文档"_ustr;
        return r;
    }
    r.success = true;
    r.content = sk.formatted.isEmpty() ? sk.statsLine : sk.formatted;
    r.summaryZh = u"骨架 · 表面="_ustr + sk.surface + u" · 块="_ustr
                  + OUString::number(sk.blockCount) + u" · hash="_ustr + sk.snapshotHash;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolReadBlocks(sal_Int32 nStart, sal_Int32 nEnd)
{
    MCPToolResult r;
    r.toolName = u"read_blocks"_ustr;
    r.mainDocumentMutation = false;
    const auto rb = DocumentAIDocumentTools::readBlocks(nStart, nEnd);
    r.success = rb.success;
    r.content = rb.content;
    r.error = rb.error;
    r.summaryZh = rb.summary.isEmpty()
                      ? (rb.success ? u"已读块"_ustr : u"读块失败"_ustr)
                      : rb.summary;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolSnapshotHash()
{
    MCPToolResult r;
    r.toolName = u"snapshot_hash"_ustr;
    r.mainDocumentMutation = false;
    r.content = DocumentAIDocumentTools::computeSnapshotHash();
    r.success = !r.content.isEmpty();
    r.summaryZh = r.success ? (u"snapshot="_ustr + r.content) : u"无快照"_ustr;
    if (!r.success)
        r.error = u"empty-hash"_ustr;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolFormulaDryRun(const OUString& rTextOrPlan)
{
    MCPToolResult r;
    r.toolName = u"formula_dry_run"_ustr;
    r.mainDocumentMutation = false;
    const auto snap = DocumentAIFormulaDryRun::captureSelectionSnapshot(256);
    const auto rep
        = DocumentAIFormulaDryRun::checkText(rTextOrPlan, snap.empty() ? nullptr : &snap);
    r.success = rep.badCount == 0; // checked==0 still ok
    if (rep.checked == 0)
    {
        r.success = true;
        r.content = u"checked=0"_ustr;
    }
    else
    {
        OUStringBuffer b;
        b.append(u"checked="_ustr);
        b.append(OUString::number(rep.checked));
        b.append(u" ok="_ustr);
        b.append(OUString::number(rep.okCount));
        b.append(u" bad="_ustr);
        b.append(OUString::number(rep.badCount));
        b.append(u" sandbox_ok="_ustr);
        b.append(OUString::number(rep.sandboxOk));
        b.append(u" sandbox_sheet="_ustr);
        b.append(OUString::number(rep.sandboxNeedsSheet));
        b.append(u" sandbox_err="_ustr);
        b.append(OUString::number(rep.sandboxError));
        for (const auto& it : rep.items)
        {
            b.append(u"\n"_ustr);
            b.append(it.formula);
            if (!it.ok)
            {
                b.append(u" → FAIL "_ustr);
                b.append(it.issue);
            }
            else if (!it.sandboxNote.isEmpty())
            {
                b.append(u" → sandbox "_ustr);
                b.append(it.sandboxNote);
            }
        }
        r.content = b.makeStringAndClear();
    }
    r.summaryZh = rep.summaryZh;
    if (!r.success)
        r.error = u"formula-dry-run-failed"_ustr;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolVerifyPlan(const OUString& rPlanOrContent)
{
    MCPToolResult r;
    r.toolName = u"verify_plan"_ustr;
    r.mainDocumentMutation = false;
    const ApplyPlan plan = planFromContent(rPlanOrContent);
    const auto v = DocumentAIVerify::verifyPlanBeforeApply(plan);
    r.success = v.ok;
    r.content = v.summaryZh;
    r.summaryZh = v.summaryZh;
    if (!v.ok)
        r.error = u"verify-soft-fail"_ustr;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolApplyPreview(const OUString& rPlanOrContent)
{
    MCPToolResult r;
    r.toolName = u"apply_preview"_ustr;
    r.mainDocumentMutation = false;
    const ApplyPlan plan = planFromContent(rPlanOrContent);
    if (!AgentChatDiffExtractor::validate(plan) || plan.operations.empty())
    {
        r.success = false;
        r.error = u"invalid-plan"_ustr;
        r.summaryZh = u"预览失败 · 无法解析写回计划 · 主文档未改"_ustr;
        return r;
    }
    const auto v = DocumentAIVerify::verifyPlanBeforeApply(plan);
    r.success = true;
    r.content = AgentChatDiffExtractor::toJson(plan);
    r.summaryZh = u"预览就绪 · plan="_ustr + plan.planId + u" · ops="_ustr
                  + OUString::number(static_cast<sal_Int32>(plan.operations.size()))
                  + u" · "_ustr + v.summaryZh + u" · 主文档未改"_ustr;
    if (!v.ok)
        r.summaryZh += u" · ⚠ 含 dry-run 问题"_ustr;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolApplyApproved(const OUString& rPlanOrContent,
                                                    bool bHumanApproval)
{
    MCPToolResult r;
    r.toolName = u"apply_approved"_ustr;
    r.mainDocumentMutation = false;
    const ApplyPlan plan = planFromContent(rPlanOrContent);
    if (!AgentChatDiffExtractor::validate(plan) || plan.operations.empty())
    {
        r.success = false;
        r.error = u"invalid-plan"_ustr;
        r.summaryZh = u"写回取消 · 计划无效 · 主文档未改"_ustr;
        return r;
    }

    // Write-back ladder (Ask default): explicit humanApproval OR session allow OR YOLO.
    bool bSelectionTarget = false;
    for (const auto& op : plan.operations)
    {
        if (op.target == u"selection"_ustr || op.target.startsWith(u"selection:"_ustr))
        {
            bSelectionTarget = true;
            break;
        }
    }
    const auto sel = AgentChatSelectionCapture::captureCurrent();
    kqoffice::ai::control::WritebackGateRequest gateReq;
    gateReq.explicitHumanApproval = bHumanApproval;
    gateReq.scope = kqoffice::ai::control::WritebackPermission::scopeFromPlanTargets(
        bSelectionTarget, /*bWorkspacePathOp*/ false);
    gateReq.surface = sel.surface;
    const auto gate = kqoffice::ai::control::WritebackPermission::evaluate(gateReq);
    if (!gate.allowed)
    {
        r.success = false;
        r.error = gate.errorCode.isEmpty() ? u"human-approval-required"_ustr : gate.errorCode;
        r.summaryZh = gate.reasonZh;
        const auto classified
            = kqoffice::ai::control::ErrorClassifier::classify(gate.reasonZh, r.error);
        if (!classified.actionZh.isEmpty())
            r.summaryZh += u" · "_ustr + classified.actionZh;
        return r;
    }

    const auto pre = DocumentAIVerify::verifyPlanBeforeApply(plan);
    if (!pre.ok)
    {
        r.success = false;
        r.error = u"pre-verify-failed"_ustr;
        r.summaryZh = pre.summaryZh + u" · 主文档未改"_ustr;
        return r;
    }
    auto apply = DocumentAIApply::applyApproved(plan);
    // One automatic retry on soft engine failure (approval already granted).
    if (!apply.success && apply.error.indexOf(u"stale"_ustr) < 0
        && !DocumentAIDocumentTools::isStaleApplyError(apply.error))
    {
        apply = DocumentAIApply::applyApproved(plan);
    }
    r.mainDocumentMutation = apply.success;
    r.success = apply.success;
    if (apply.success)
    {
        DocumentAIDocumentTools::clearSeen();
        const auto post = DocumentAIVerify::verifyAfterApply(plan, apply);
        r.content = u"applied="_ustr + OUString::number(apply.appliedCount) + u" engine="_ustr
                    + apply.engine;
        r.summaryZh = u"已写回 · "_ustr + post.summaryZh;
        if (!post.ok)
        {
            r.summaryZh += u" · 校验警告：可撤销后重试"_ustr;
        }
    }
    else
    {
        r.error = apply.error.isEmpty() ? u"apply-failed"_ustr : apply.error;
        r.summaryZh = u"写回失败 · 主文档未改 · "_ustr
                      + DocumentAIApply::userFacingErrorZh(apply.error, apply.engine,
                                                           apply.surface);
    }
    return r;
}

MCPToolResult DocumentAIMCPTools::toolListConnectors()
{
    MCPToolResult r;
    r.toolName = u"list_connectors"_ustr;
    r.mainDocumentMutation = false;
    r.success = true;
    r.content = DocumentAIEnterpriseConnectors::listConnectorIdsJson();
    const auto list = DocumentAIEnterpriseConnectors::listConnectors();
    r.summaryZh = u"连接器数="_ustr + OUString::number(static_cast<sal_Int32>(list.size()))
                  + u" · 总开关="_ustr
                  + (DocumentAIEnterpriseConnectors::globalEnabled() ? u"开"_ustr : u"关（默认）"_ustr)
                  + u" · 不外联"_ustr;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolConnectorStatus()
{
    MCPToolResult r;
    r.toolName = u"connector_status"_ustr;
    r.mainDocumentMutation = false;
    r.success = true;
    r.content = DocumentAIEnterpriseConnectors::statusSummaryZh();
    r.summaryZh = DocumentAIEnterpriseConnectors::globalEnabled()
                      ? u"企业连接器总开关=开 · 仍须逐项授权"_ustr
                      : u"企业连接器总开关=关（默认）· 未发起网络"_ustr;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolConnectorInvoke(const OUString& rArguments,
                                                      bool bExplicitUserApproval)
{
    MCPToolResult r;
    r.toolName = u"connector_invoke"_ustr;
    r.mainDocumentMutation = false;

    ConnectorInvokeRequest req;
    req.connectorId = parseStringArg(rArguments, u"connectorId"_ustr);
    if (req.connectorId.isEmpty())
        req.connectorId = parseStringArg(rArguments, u"id"_ustr);
    req.operation = parseStringArg(rArguments, u"operation"_ustr);
    if (req.operation.isEmpty())
        req.operation = parseStringArg(rArguments, u"op"_ustr);
    if (req.operation.isEmpty())
        req.operation = u"status"_ustr;
    req.query = parseStringArg(rArguments, u"query"_ustr);
    req.body = parseStringArg(rArguments, u"body"_ustr);
    if (req.body.isEmpty())
        req.body = parseStringArg(rArguments, u"json"_ustr);
    req.httpMethod = parseStringArg(rArguments, u"httpMethod"_ustr);
    if (req.httpMethod.isEmpty())
        req.httpMethod = parseStringArg(rArguments, u"method"_ustr);
    req.explicitUserApproval
        = bExplicitUserApproval
          || parseBoolArg(rArguments, u"explicitUserApproval"_ustr, false)
          || parseBoolArg(rArguments, u"humanApproval"_ustr, false);

    // External-write denier (pure): refuse publish/push/deploy style operations.
    {
        const auto deny = kqoffice::ai::control::ExternalWriteDenier::evaluateAction(req.operation);
        if (!deny.allowed)
        {
            r.success = false;
            r.error = deny.reasonCode;
            r.summaryZh = deny.reasonZh + u" · 主文档未改 · 外写已拒绝"_ustr;
            kqoffice::ai::control::EventLedger ledger;
            ledger.append(u"deny"_ustr, deny.reasonCode + u" "_ustr + req.operation,
                          req.connectorId);
            return r;
        }
        const OUString method = req.httpMethod.toAsciiLowerCase();
        if (method == u"post"_ustr || method == u"put"_ustr || method == u"patch"_ustr
            || method == u"delete"_ustr)
        {
            // Mutating HTTP still requires explicit approval; denier records intent.
            if (!req.explicitUserApproval)
            {
                r.success = false;
                r.error = u"human-approval-required"_ustr;
                r.summaryZh = u"拒绝外发变更请求 · 缺少明确人工批准 · 主文档未改"_ustr;
                return r;
            }
        }
    }

    const ConnectorInvokeResult inv = DocumentAIEnterpriseConnectors::invoke(req);
    r.success = inv.success;
    r.error = inv.success ? OUString() : inv.status;
    r.content = inv.content.isEmpty() ? inv.messageZh : inv.content;
    r.summaryZh = inv.messageZh;
    if (!inv.httpMethod.isEmpty())
        r.summaryZh += u" · "_ustr + inv.httpMethod;
    if (inv.usedAuthHeader)
        r.summaryZh += u" · auth=1"_ustr;
    if (inv.networkAttempted)
        r.summaryZh += u" · networkAttempted=1"_ustr;
    else
        r.summaryZh += u" · networkAttempted=0"_ustr;
    return r;
}

MCPToolResult DocumentAIMCPTools::toolConnectorDeviceStart(const OUString& rArguments,
                                                           bool bExplicitUserApproval)
{
    MCPToolResult r;
    r.toolName = u"connector_device_start"_ustr;
    r.mainDocumentMutation = false;
    OUString id = parseStringArg(rArguments, u"connectorId"_ustr);
    if (id.isEmpty())
        id = parseStringArg(rArguments, u"id"_ustr);
    const bool appr = bExplicitUserApproval
                      || parseBoolArg(rArguments, u"explicitUserApproval"_ustr, false)
                      || parseBoolArg(rArguments, u"humanApproval"_ustr, false);
    const auto sess = DocumentAIEnterpriseConnectors::startDeviceAuth(id, appr);
    r.success = sess.status == u"pending"_ustr || sess.status == u"authorized"_ustr;
    r.error = r.success ? OUString() : sess.status;
    OUStringBuffer content;
    content.append(u"status="_ustr);
    content.append(sess.status);
    content.append(u"\nuser_code="_ustr);
    content.append(sess.userCode);
    content.append(u"\nverification_uri="_ustr);
    content.append(sess.verificationUriComplete.isEmpty() ? sess.verificationUri
                                                          : sess.verificationUriComplete);
    content.append(u"\n"_ustr);
    content.append(sess.messageZh);
    r.content = content.makeStringAndClear();
    r.summaryZh = sess.messageZh
                  + (sess.networkAttempted ? u" · networkAttempted=1"_ustr
                                           : u" · networkAttempted=0"_ustr);
    return r;
}

MCPToolResult DocumentAIMCPTools::toolConnectorDevicePoll(const OUString& rArguments,
                                                          bool bExplicitUserApproval)
{
    MCPToolResult r;
    r.toolName = u"connector_device_poll"_ustr;
    r.mainDocumentMutation = false;
    OUString id = parseStringArg(rArguments, u"connectorId"_ustr);
    if (id.isEmpty())
        id = parseStringArg(rArguments, u"id"_ustr);
    const bool appr = bExplicitUserApproval
                      || parseBoolArg(rArguments, u"explicitUserApproval"_ustr, false)
                      || parseBoolArg(rArguments, u"humanApproval"_ustr, false);
    const auto sess = DocumentAIEnterpriseConnectors::pollDeviceAuth(id, appr);
    r.success = sess.status == u"authorized"_ustr || sess.status == u"pending"_ustr;
    r.error = (sess.status == u"error"_ustr || sess.status == u"denied"_ustr
               || sess.status == u"expired"_ustr)
                  ? sess.status
                  : OUString();
    r.content = u"status="_ustr + sess.status + u"\n"_ustr + sess.messageZh;
    r.summaryZh = sess.messageZh
                  + (sess.networkAttempted ? u" · networkAttempted=1"_ustr
                                           : u" · networkAttempted=0"_ustr);
    return r;
}

MCPToolResult DocumentAIMCPTools::toolVisionStatus()
{
    MCPToolResult r;
    r.toolName = u"vision_status"_ustr;
    r.mainDocumentMutation = false;
    r.success = true;
    r.content = DocumentAIVisionEvidence::formatVisionRouteStatusZh();
    r.summaryZh = u"Vision 路由 · "_ustr + DocumentAIVisionEvidence::resolveLocalVisionModel();
    return r;
}

MCPToolResult DocumentAIMCPTools::dispatch(const MCPToolCall& rCall)
{
    const OUString name = lower(rCall.name);
    if (name == u"list_tools"_ustr || name == u"tools/list"_ustr)
    {
        MCPToolResult r;
        r.toolName = u"list_tools"_ustr;
        r.success = true;
        r.mainDocumentMutation = false;
        r.content = listToolNamesJson();
        r.summaryZh = u"工具数="_ustr + OUString::number(static_cast<sal_Int32>(listTools().size()))
                      + u" · "_ustr + schemaVersion();
        return r;
    }
    if (name == u"read_skeleton"_ustr || name == u"read_document_skeleton"_ustr)
        return toolReadSkeleton(parseIntArg(rCall.arguments, u"max_blocks"_ustr, 200));
    if (name == u"read_blocks"_ustr)
    {
        const sal_Int32 a = parseIntArg(rCall.arguments, u"start"_ustr, 0);
        const sal_Int32 b = parseIntArg(rCall.arguments, u"end"_ustr, a);
        return toolReadBlocks(a, b);
    }
    if (name == u"snapshot_hash"_ustr)
        return toolSnapshotHash();
    if (name == u"formula_dry_run"_ustr)
        return toolFormulaDryRun(!rCall.planContent.isEmpty() ? rCall.planContent : rCall.arguments);
    if (name == u"verify_plan"_ustr)
        return toolVerifyPlan(!rCall.planContent.isEmpty() ? rCall.planContent : rCall.arguments);
    if (name == u"apply_preview"_ustr)
        return toolApplyPreview(!rCall.planContent.isEmpty() ? rCall.planContent : rCall.arguments);
    if (name == u"apply_approved"_ustr)
        return toolApplyApproved(!rCall.planContent.isEmpty() ? rCall.planContent : rCall.arguments,
                                 rCall.humanApproval);
    if (name == u"list_connectors"_ustr)
        return toolListConnectors();
    if (name == u"connector_status"_ustr)
        return toolConnectorStatus();
    if (name == u"connector_invoke"_ustr || name == u"invoke_connector"_ustr)
        return toolConnectorInvoke(rCall.arguments, rCall.humanApproval);
    if (name == u"connector_device_start"_ustr || name == u"device_auth_start"_ustr)
        return toolConnectorDeviceStart(rCall.arguments, rCall.humanApproval);
    if (name == u"connector_device_poll"_ustr || name == u"device_auth_poll"_ustr)
        return toolConnectorDevicePoll(rCall.arguments, rCall.humanApproval);
    if (name == u"vision_status"_ustr)
        return toolVisionStatus();
    MCPToolResult r;
    r.toolName = rCall.name;
    r.success = false;
    r.error = u"unknown-tool"_ustr;
    r.summaryZh = u"未知工具 · 主文档未改"_ustr;
    return r;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
