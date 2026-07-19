/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * W8-C / C1 skeleton: minimal native Calc ApplyEngine for cell-replace +
 * cell-formula only. Schema is independent of Writer's v2-w3-runtime-1.
 * Full multi-sheet / chart / pivot / format engines are out of scope.
 */

#pragma once

#include "scdllapi.h"

#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <optional>
#include <vector>

class ScDocShell;

namespace sc::intelligent
{
/// Locked schema token for Calc runtime JSON (do not reuse Writer paragraph ids).
inline constexpr char kCalcSchemaVersion[] = "v1-calc-runtime-1";

enum class PatchKind
{
    CellReplace, // "cell-replace" — set string/value text
    CellFormula, // "cell-formula" — set formula (leading '=')
};

enum class ApplyStatus
{
    Ok,
    ValidationFailed,
    PatchFailed,
    Unsupported,
    UndoException,
};

struct Patch
{
    OUString maPatchId; // "p1", ...
    PatchKind meKind = PatchKind::CellReplace;
    OUString maCellRef; // "A1" (no sheet — active tab only in C1)
    OUString maAfter;
    std::optional<OUString> maBefore;
};

struct ApplyPlan
{
    OUString maSchemaVersion; // kCalcSchemaVersion
    OUString maPlanId;
    bool mbPreviewOnly = false;
    std::vector<Patch> maPatches;
};

struct ApplyResult
{
    ApplyStatus meStatus = ApplyStatus::Ok;
    sal_Int32 mnAppliedCount = 0;
    OUString maError; // machine token and/or Chinese; empty on success
    std::optional<OUString> maFailedPatchId;
};

SC_DLLPUBLIC OString PatchKindToken(PatchKind eKind);
SC_DLLPUBLIC std::optional<PatchKind> PatchKindFromToken(std::string_view aToken);
SC_DLLPUBLIC OString ApplyStatusToken(ApplyStatus eStatus);

/// Shape-only validation (no document). Fails with Chinese user-facing text when possible.
SC_DLLPUBLIC bool ValidateCalcApplyPlanShape(const ApplyPlan& rPlan, OUString& rError);

/// Parse Calc runtime JSON (schema v1-calc-runtime-1). Returns nullopt on hard parse fail.
SC_DLLPUBLIC std::optional<ApplyPlan> TryParseCalcApplyPlanRuntimeJson(const OUString& rJsonBody);

/// Apply plan to active ScDocShell (active sheet only). Uses ScDocFunc with undo.
/// On failure: main document left unchanged for unapplied patches; partial multi-patch
/// is rolled back via undo list when any patch fails after some success.
SC_DLLPUBLIC ApplyResult ApplyCalcPlan(ScDocShell& rDocShell, const ApplyPlan& rPlan);

} // namespace sc::intelligent

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
