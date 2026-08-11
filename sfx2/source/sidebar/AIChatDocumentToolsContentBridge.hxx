/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 M9: document-tools → content registry).
 *
 * Registers read-only document-tool outputs as workspace content objects.
 * Never mutates the main document.
 */

#pragma once

#include <rtl/ustring.hxx>

#include <vector>

namespace kqoffice::ai::chat
{
struct DocumentToolPrepResult;
}

namespace sfx2::sidebar
{

struct AIChatDocumentToolsContentBridgeResult
{
    bool Success = false;
    sal_Int32 RegisteredCount = 0;
    std::vector<OUString> ObjectIds;
    OUString Message;
    bool MainDocumentMutation = false; ///< always false
};

/// Bridge: document-tools prep → content registry (+ optional text sidecar).
class AIChatDocumentToolsContentBridge final
{
public:
    AIChatDocumentToolsContentBridgeResult
    RegisterPrepResult(const kqoffice::ai::chat::DocumentToolPrepResult& rPrep) const;

    static OUString MakeToolObjectId(const OUString& rToolName, const OUString& rPayloadHash);
    static OUString MakeToolEvidenceId(const OUString& rObjectId);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
