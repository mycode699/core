/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Static formula dry-run (no sheet evaluation / no main-doc mutation).
 * Used before Calc write-back staging and as soft post-apply verify.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIFORMULADRYRUN_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIFORMULADRYRUN_HXX

#include <AgentChatDiffExtractor.hxx>

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

struct FormulaDryRunItem
{
    OUString formula;
    OUString target; ///< optional cell:A1
    bool ok = false;
    OUString issue; ///< empty when ok; machine-ish code + short zh
    /// Q3 sandbox: pure numeric eval when no cell refs (no LO sheet).
    /// status: none | ok | needs-sheet | error | ok-snapshot
    OUString sandboxStatus;
    double sandboxValue = 0.0;
    OUString sandboxNote; ///< e.g. "SUM=6" / "依赖单元格 · 需写入后求值"
};

struct FormulaDryRunReport
{
    sal_Int32 checked = 0;
    sal_Int32 okCount = 0;
    sal_Int32 badCount = 0;
    sal_Int32 sandboxOk = 0;
    sal_Int32 sandboxNeedsSheet = 0;
    sal_Int32 sandboxError = 0;
    sal_Int32 snapshotCells = 0; ///< cells used from selection snapshot (if any)
    std::vector<FormulaDryRunItem> items;
    /// One-line Chinese summary for sidebar / transcript.
    OUString summaryZh;
    bool allOk() const { return badCount == 0 && checked > 0; }
    bool hasWork() const { return checked > 0; }
};

/// One cell value from a local snapshot (no mutation, no upload).
struct CellSnapshotEntry
{
    OUString addr; ///< A1-style (no $)
    double number = 0.0;
    bool isNumber = false;
    OUString text; ///< formula or string fallback
};

/// Bounded selection/range snapshot for sandbox substitution.
struct CellValueSnapshot
{
    std::vector<CellSnapshotEntry> cells;
    OUString rangeLabel; ///< cell:A1 or range:A1:B3
    bool empty() const { return cells.empty(); }
    sal_Int32 size() const { return static_cast<sal_Int32>(cells.size()); }

    /// Lookup A1 / $A$1 (case-insensitive). Returns false if missing or non-numeric.
    bool lookupNumber(const OUString& rAddr, double& rOut) const;
};

/// Pure static checks + numeric sandbox — no network, no mutation.
/// Optional CellValueSnapshot enables evaluating formulas that reference selected cells.
class SAL_DLLPUBLIC_EXPORT DocumentAIFormulaDryRun
{
public:
    /// Capture numeric/text values from current Calc selection (cap nMaxCells).
    /// Empty when not on Calc or no selection. Never mutates.
    static CellValueSnapshot captureSelectionSnapshot(sal_Int32 nMaxCells = 256);

    /// Dry-run a single formula string (leading '=' optional after normalize).
    static FormulaDryRunItem checkFormula(const OUString& rFormula,
                                          const OUString& rTarget = OUString(),
                                          const CellValueSnapshot* pSnap = nullptr);

    /// Dry-run all formula-like ops in a plan (newText starts with = / ＝).
    static FormulaDryRunReport checkPlan(const ApplyPlan& rPlan,
                                         const CellValueSnapshot* pSnap = nullptr);

    /// Extract formula lines from free text and dry-run each (cap 32).
    static FormulaDryRunReport checkText(const OUString& rText,
                                         const CellValueSnapshot* pSnap = nullptr);

    /// True if text looks like a spreadsheet formula candidate.
    static bool looksLikeFormula(const OUString& rText);

    /// Normalize fullwidth ＝ and ensure leading '='.
    static OUString normalizeFormula(const OUString& rFormula);

    /// Evaluate pure numeric / SUM|… / IF / ROUND; with snapshot, substitute A1 refs.
    /// Returns sandboxStatus in item; does not mutate documents.
    static void sandboxEvaluate(FormulaDryRunItem& rItem,
                                const CellValueSnapshot* pSnap = nullptr);

    /// Substitute known cells into formula body (A1 → number). Empty if unresolved refs remain
    /// and bRequireAll is true.
    static OUString substituteSnapshot(const OUString& rFormulaBody, const CellValueSnapshot& rSnap,
                                       bool bRequireAll, bool& rHadRefs, bool& rAllResolved);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
