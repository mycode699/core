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

    /// Extract first spreadsheet formula (line starting with '=') from free text.
    static OUString extractLeadingFormula(const OUString& rText);

    /// Extract all formula lines (each starting with '=') in document order.
    static std::vector<OUString> extractAllFormulas(const OUString& rText);

    /// Build a single cell replace plan for Calc formula write-back.
    /// rCellTarget e.g. "cell:B2"; formula must start with '='.
    static ApplyPlan makeFormulaCellPlan(const OUString& rCellTarget, const OUString& rFormula,
                                         const OUString& rOldText = OUString());

    /// Build multi-cell plan from formulas + range/cell target.
    /// - "cell:B2" + N formulas → B2, B3, … (column-major down)
    /// - "range:A1:B3" + formulas → fill row-major A1,B1,A2… (cap 64 cells)
    /// - single formula on a range → write only the top-left cell
    static ApplyPlan makeFormulaRangePlan(const OUString& rPosition,
                                          const std::vector<OUString>& rFormulas,
                                          const OUString& rOldText = OUString());

    /// Parse outline-to-slides free text into insert ops with targets slide:1..N.
    /// Recognizes "## N. Title" / "N. Title" / "第N页" headings and following bullet lines.
    static ApplyPlan extractOutlineSlidePlan(const OUString& rText);

    /// True if free text looks like multi-slide outline suitable for Impress write-back.
    static bool looksLikeOutlineSlideContent(const OUString& rText);

    /// True if free text / scenario output asks for a chart (Calc).
    static bool looksLikeChartIntent(const OUString& rText);

    /// Stage-only plan: after human approval, open Insert Chart on current selection.
    /// opType = "chart_insert"; does not mutate cells itself.
    static ApplyPlan makeChartInsertPlan(const OUString& rRangeOrCell,
                                         const OUString& rAdvice = OUString());

    // —— Writer M-W1: structure outline + review fixes (approve-before-apply) ——

    /// True when free text contains Writer heading-outline write-back markers.
    /// Looks for ===可圈大纲写回=== / "para:N|H1|" lines / markdown # headings with para anchors.
    static bool looksLikeWriterHeadingOutline(const OUString& rText);

    /// Parse Writer outline write-back block into format ops:
    ///   para:N|H1|optional title   → format target=para:N newText="heading:1"
    ///   para:N|H2|…                → heading:2 (H3 → heading:3)
    /// Markdown fallback: "# Title" lines map to sequential para:1.. only when
    /// explicit "para:" anchors are present in the same block (no silent guess).
    static ApplyPlan extractWriterHeadingOutlinePlan(const OUString& rText);

    /// True when free text contains review fix write-back markers.
    /// Looks for ===可圈审阅修复=== / FIX|old|new lines.
    static bool looksLikeReviewFixList(const OUString& rText);

    /// Parse review fix lines into replace ops (oldText/newText; target=selection or empty).
    /// Cap at 12 fixes. planId = ap-review-fixes.
    static ApplyPlan extractReviewFixPlan(const OUString& rText);

    // —— Calc M-C1: explicit formula / clean write-back blocks ——

    /// True when free text has ===可圈公式写回=== / cell:A1|=… lines.
    static bool looksLikeCalcFormulaWriteback(const OUString& rText);

    /// Parse cell:X|=formula|note lines into replace ops. planId = ap-calc-formula-writeback.
    static ApplyPlan extractCalcFormulaWritebackPlan(const OUString& rText);

    /// True when free text has ===可圈清洗写回=== (adjacent-column clean formulas).
    static bool looksLikeCalcCleanWriteback(const OUString& rText);

    /// Parse clean write-back (same cell:X|=… lines). planId = ap-calc-clean-writeback.
    static ApplyPlan extractCalcCleanWritebackPlan(const OUString& rText);

    /// Suggest adjacent column target for clean formulas (e.g. range:A1:A10 → cell:B1).
    /// Returns empty if position cannot be parsed.
    static OUString adjacentColumnCell(const OUString& rPosition);

    // —— Impress M-I0: speaker notes write-back (notes page only) ——

    /// True when free text has ===可圈讲稿写回=== / slide:N|讲稿|… lines.
    static bool looksLikeImpressNotesWriteback(const OUString& rText);

    /// Parse slide:N|讲稿|text → insert ops with newText "讲稿：…" (notes-only fill).
    /// planId = ap-impress-notes.
    static ApplyPlan extractImpressNotesWritebackPlan(const OUString& rText);

private:
    /// Parse a single JSON fragment into a DiffOperation.
    static DiffOperation parseOperation(const OUString& jsonFragment);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
