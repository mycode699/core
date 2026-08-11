/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Local MCP-shaped tool bus (minimal). Same trust rules as in-app Agent:
 * read tools never mutate; apply_preview stages only; apply_approved requires
 * explicit humanApproval=true and still routes through DocumentAIApply.
 *
 * No network server here — callers (future MCP host / tests) invoke dispatch().
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIMCPTOOLS_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIMCPTOOLS_HXX

#include <AgentChatDiffExtractor.hxx>

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

struct MCPToolDescriptor
{
    OUString name;
    OUString descriptionZh;
    bool mutatesDocument = false; ///< true only for apply_approved
    bool requiresHumanApproval = false;
};

struct MCPToolCall
{
    OUString name;
    /// Free-form JSON-ish or key=value payload (kept simple for v0).
    OUString arguments;
    /// Required true for apply_approved.
    bool humanApproval = false;
    /// Optional staged plan JSON / raw content for apply_* tools.
    OUString planContent;
};

struct MCPToolResult
{
    bool success = false;
    OUString toolName;
    OUString content; ///< tool payload for model / host
    OUString error;
    bool mainDocumentMutation = false;
    OUString summaryZh;
};

/// Minimal tool surface aligned with document-tools + apply gate.
class SAL_DLLPUBLIC_EXPORT DocumentAIMCPTools
{
public:
    /// Catalog for schema export / listing (stable order).
    static std::vector<MCPToolDescriptor> listTools();

    /// Schema version string for contracts.
    static OUString schemaVersion();

    /// JSON array of tool names (ASCII, for harnesses).
    static OUString listToolNamesJson();

    /// Dispatch one tool call. Never auto-applies without humanApproval.
    static MCPToolResult dispatch(const MCPToolCall& rCall);

    /// Helpers used by dispatch / tests.
    static MCPToolResult toolReadSkeleton(sal_Int32 nMaxBlocks = 200);
    static MCPToolResult toolReadBlocks(sal_Int32 nStart, sal_Int32 nEnd);
    static MCPToolResult toolApplyPreview(const OUString& rPlanOrContent);
    static MCPToolResult toolApplyApproved(const OUString& rPlanOrContent, bool bHumanApproval);
    static MCPToolResult toolFormulaDryRun(const OUString& rTextOrPlan);
    static MCPToolResult toolSnapshotHash();
    static MCPToolResult toolVerifyPlan(const OUString& rPlanOrContent);

    /// Enterprise connectors (default OFF; no silent network).
    static MCPToolResult toolListConnectors();
    static MCPToolResult toolConnectorStatus();
    /// Hard-gated invoke (private GET/POST + auth).
    static MCPToolResult toolConnectorInvoke(const OUString& rArguments, bool bExplicitUserApproval);
    static MCPToolResult toolConnectorDeviceStart(const OUString& rArguments,
                                                  bool bExplicitUserApproval);
    static MCPToolResult toolConnectorDevicePoll(const OUString& rArguments,
                                                 bool bExplicitUserApproval);
    static MCPToolResult toolVisionStatus();
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
