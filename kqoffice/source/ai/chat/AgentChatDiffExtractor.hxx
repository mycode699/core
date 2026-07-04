/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V4 M2 Day-1c — LLM output parser for ApplyPlan JSON extraction.
 * Parses LLM responses looking for ```json ... ``` blocks and
 * extracts operations into an ApplyPlan structure.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATDIFFEXTRACTOR_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_AGENTCHATDIFFEXTRACTOR_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// A single diff operation extracted from LLM output.
struct DiffOperation
{
    OUString opType;  ///< "insert", "delete", "replace", "format"
    OUString target;  ///< Target location in document (e.g., "para:3", "cell:B2")
    OUString oldText; ///< Original text (for replace/delete)
    OUString newText; ///< Replacement or inserted text (for insert/replace)

    /// Generate the inverse operation for undo.
    /// insert→delete, delete→insert(oldText), replace→replace(swap old/new), format→format
    DiffOperation reverse() const
    {
        DiffOperation inv;
        inv.target = target;
        if (opType == "insert")
        {
            inv.opType = "delete";
            inv.oldText = newText;
        }
        else if (opType == "delete")
        {
            inv.opType = "insert";
            inv.newText = oldText;
        }
        else if (opType == "replace")
        {
            inv.opType = "replace";
            inv.oldText = newText;
            inv.newText = oldText;
        }
        else // format
        {
            inv.opType = "format";
            inv.newText = newText; // preserve format spec
        }
        return inv;
    }
};

/// Complete apply plan parsed from LLM output.
struct ApplyPlan
{
    OUString planId;                     ///< Unique plan identifier
    std::vector<DiffOperation> operations; ///< Operations to apply
    OUString rawOutput;                  ///< Original LLM output text

    /// Generate the inverse plan for undo (Ctrl+Z).
    /// Reverses the operation order and inverts each operation.
    ApplyPlan inverse() const
    {
        ApplyPlan inv;
        inv.planId = planId + "-undo";
        inv.rawOutput = rawOutput;
        for (auto it = operations.rbegin(); it != operations.rend(); ++it)
            inv.operations.push_back(it->reverse());
        return inv;
    }
};

/// Parse LLM output into structured ApplyPlan.
class SAL_DLLPUBLIC_EXPORT AgentChatDiffExtractor
{
public:
    /// Extract ApplyPlan from raw LLM output.
    /// Looks for ```json ... ``` blocks containing operations array.
    static ApplyPlan extract(const OUString& llmOutput);

    /// Validate that all operations in the plan have required fields.
    /// Returns true if the plan is structurally valid.
    static bool validate(const ApplyPlan& plan);

    /// Serialize ApplyPlan back to JSON string.
    static OUString toJson(const ApplyPlan& plan);

private:
    /// Parse a single JSON fragment into a DiffOperation.
    static DiffOperation parseOperation(const OUString& jsonFragment);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
