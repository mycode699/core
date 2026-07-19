/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * W9-B / I1 skeleton: minimal native Impress ApplyEngine for
 * shape-text-replace (+ optional slide-insert token, UNO fallback).
 * Schema is independent of Writer/Calc runtime schemas.
 * Full master/layout/theme engines are out of scope.
 */

#pragma once

#include "sddllapi.h"

#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <optional>
#include <vector>

namespace sd
{
class DrawDocShell;
}

namespace sd::intelligent
{
/// Locked schema token for Impress runtime JSON (do not reuse Writer/Calc schemas).
inline constexpr char kImpressSchemaVersion[] = "v1-impress-runtime-1";

enum class PatchKind
{
    ShapeTextReplace, // "shape-text-replace" — set text on slide:N:shape:M
    SlideInsert, // "slide-insert" — I1: honest unsupported → DocumentAIApply UNO fallback
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
    PatchKind meKind = PatchKind::ShapeTextReplace;
    sal_Int32 mnSlide1Based = 0; // 1-based slide index
    sal_Int32 mnShape0Based = -1; // 0-based shape index; required for shape-text-replace
    OUString maAfter;
    std::optional<OUString> maBefore;
};

struct ApplyPlan
{
    OUString maSchemaVersion; // kImpressSchemaVersion
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

SD_DLLPUBLIC OString PatchKindToken(PatchKind eKind);
SD_DLLPUBLIC std::optional<PatchKind> PatchKindFromToken(std::string_view aToken);
SD_DLLPUBLIC OString ApplyStatusToken(ApplyStatus eStatus);

/// Shape-only validation (no document). Fails with Chinese user-facing text when possible.
SD_DLLPUBLIC bool ValidateImpressApplyPlanShape(const ApplyPlan& rPlan, OUString& rError);

/// Parse Impress runtime JSON (schema v1-impress-runtime-1). Returns nullopt on hard parse fail.
SD_DLLPUBLIC std::optional<ApplyPlan> TryParseImpressApplyPlanRuntimeJson(const OUString& rJsonBody);

/// Apply plan to active DrawDocShell. shape-text-replace uses SdrTextObj::SetText + undo.
/// On failure: main document left unchanged for unapplied patches; partial multi-patch
/// is rolled back via SfxUndoManager when any patch fails after some success.
SD_DLLPUBLIC ApplyResult ApplyImpressPlan(DrawDocShell& rDocShell, const ApplyPlan& rPlan);

} // namespace sd::intelligent

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
