/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "swdllapi.h"

#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <optional>
#include <vector>

class SwDoc;
class SwDocShell;
class SwTextNode;

namespace sw::intelligent
{
// 7 patch kinds — order locked to W3 spec §"Patch Kinds（v1）" table.
// H7 harness greps for these literal strings; do not rename without
// also bumping the schema enum + harness EXPECTED_KINDS list.
enum class PatchKind
{
    ParagraphReplace,       // "paragraph-replace"
    ParagraphInsertAfter,   // "paragraph-insert-after"
    ParagraphDelete,        // "paragraph-delete"
    ParagraphFormat,        // "paragraph-format"
    ParagraphReformat,      // "paragraph-reformat"
    TextRangeReplace,       // "text-range-replace"
    TextFormat,             // "text-format"
};

struct PatchTarget
{
    OUString maParagraphId;          // "swpara-N"
    std::optional<OUString> maTextHash;  // sha256:... — idempotency check
};

struct PatchRange
{
    sal_Int32 mnStart = 0;
    sal_Int32 mnLength = 0;
};

struct Patch
{
    OUString maPatchId;              // "p1", "p2", ...
    PatchKind meKind;
    PatchTarget maTarget;
    OUString maSeverity;             // "minor" | "normal" | "major"
    OUString maRationale;
    std::optional<OUString> maBefore;
    std::optional<OUString> maAfter;
    std::optional<PatchRange> maRange;
    // format_changes / value carriers — kept as raw JSON until per-kind
    // SwUndoApplyPatch impls land.
    std::optional<OUString> maFormatChangesJson;
    // paragraph-delete: allow removing non-empty paragraphs when true.
    bool mbForce = false;
};

struct ApplyPlan
{
    OUString maSchemaVersion;        // const "v2-w3-runtime-1"
    OUString maPlanId;               // "ap-..."
    OUString maSourceDiagnosticId;   // "diag-..."
    OUString maDocSnapshotHash;      // "sha256:..."
    bool mbPreviewOnly = false;
    std::vector<Patch> maPatches;
};

// Failure modes — locked to W3 spec §"Failure Modes" table.
enum class ApplyStatus
{
    Ok,
    ValidationFailed,
    StaleSnapshot,
    PatchFailed,
    UndoException,
};

struct PatchResult
{
    OUString maPatchId;
    OUString maStatus;       // "ok" | "skipped-idempotent" | "failed"
    std::optional<OUString> maBeforeHash;
    std::optional<OUString> maAfterHash;
    std::optional<OUString> maErrorMessage;
};

struct ApplyResult
{
    ApplyStatus meStatus = ApplyStatus::Ok;
    OUString maStatusToken;          // kebab-case mirror of meStatus
    sal_Int32 mnAppliedCount = 0;
    std::optional<OUString> maFailedPatchId;
    std::optional<OUString> maEvidenceId;
    std::optional<OUString> maAfterSnapshotHash;
    std::vector<PatchResult> maPatchResults;
};

// Convert 7 PatchKind enum values to / from spec kind tokens.
// H7 harness greps the .cxx file for the 7 string literals.
SW_DLLPUBLIC OString PatchKindToken(PatchKind eKind);
SW_DLLPUBLIC std::optional<PatchKind> PatchKindFromToken(std::string_view aToken);

// Convert ApplyStatus enum to / from kebab-case token (W3 §"Failure Modes").
SW_DLLPUBLIC OString ApplyStatusToken(ApplyStatus eStatus);
SW_DLLPUBLIC std::optional<ApplyStatus> ApplyStatusFromToken(std::string_view aToken);

// Plan-shape validation (no SwDoc). Mirrors ApplyEngine::run step 1.
SW_DLLPUBLIC bool ValidateApplyPlanShape(const ApplyPlan& rPlan, OUString& rError);

// Deterministic sha256:... hashes for snapshot / paragraph text (W3 Day-1b D1).
SW_DLLPUBLIC OUString ComputeParagraphTextHash(std::u16string_view rText);
SW_DLLPUBLIC OUString ComputeDocSnapshotHash(const SwDocShell& rDocShell);

// Parse "swpara-N" (1-based body text paragraph index). Used by apply + qa.
SW_DLLPUBLIC bool ParseParagraphId(const OUString& rParagraphId, sal_uInt32& rnParagraph);

// Current body-text paragraph content for apply-plan construction (W4 Day-4).
SW_DLLPUBLIC std::optional<OUString> GetBodyParagraphText(const SwDocShell& rDocShell,
                                                        const OUString& rParagraphId);

// One paragraph-replace patch with live before text + doc snapshot hash (W4 Day-4).
SW_DLLPUBLIC std::optional<ApplyPlan>
BuildSingleParagraphReplacePlan(SwDocShell& rDocShell, const OUString& rPlanId,
                                const OUString& rParagraphId, const OUString& rAfterText,
                                const OUString& rSourceDiagnosticId);

// v2-w3-runtime-1 envelope parser (up to 32 patches; no SwDoc). Keeps
// doc_snapshot_hash from JSON when present. W3 Day-5.
SW_DLLPUBLIC std::optional<ApplyPlan> ParseApplyPlanRuntimeJson(const OUString& rJsonBody);

// Full apply entry: multi-patch envelope or inline flat paragraph-replace;
// always refreshes doc_snapshot_hash from live document. W4 Day-4 / W3 Day-5.
SW_DLLPUBLIC std::optional<ApplyPlan> TryParseApplyPlanRuntimeJson(const OUString& rJsonBody,
                                                                   SwDocShell& rDocShell);

// Day-1 linear scan of format_changes JSON for "style":"<name>" (W3 paragraph-format).
SW_DLLPUBLIC std::optional<OUString> ParseFormatChangesStyle(std::u16string_view rJson);

// Day-1 linear scan of format_changes JSON for "bold":true (W3 text-format).
SW_DLLPUBLIC std::optional<bool> ParseFormatChangesBold(std::u16string_view rJson);

// text-format precondition check (UTF-16 range + format_changes; no SwDoc). Returns patch
// status token: "ok" | "failed".
SW_DLLPUBLIC OString ValidateTextFormatPatch(std::u16string_view rParagraphText,
                                             const PatchRange& rRange,
                                             std::u16string_view rFormatChangesJson,
                                             const std::optional<OUString>& rTargetTextHash
                                                 = std::nullopt);

// paragraph-reformat: first_line_indent in twips (LO core MapUnit::MapTwip); line_spacing
// optional multiplier (1.5 → 150% proportional, SvxLineSpacingItem::SetPropLineSpace).
struct ParagraphReformatAttrs
{
    sal_Int32 mnFirstLineIndentTwips = 0;
    std::optional<double> mfLineSpacing;
};

SW_DLLPUBLIC std::optional<ParagraphReformatAttrs>
ParseFormatChangesReformat(std::u16string_view rJson);
SW_DLLPUBLIC OUString SerializeFormatChangesReformat(const ParagraphReformatAttrs& rAttrs);
SW_DLLPUBLIC bool ApplyParagraphReformatAttrs(SwTextNode& rNode,
                                              const ParagraphReformatAttrs& rAttrs);

// text-range-replace precondition check (UTF-16 range; no SwDoc). Returns patch
// status token: "ok" | "skipped-idempotent" | "failed".
SW_DLLPUBLIC OString ValidateTextRangeReplacePatch(std::u16string_view rParagraphText,
                                                   const PatchRange& rRange,
                                                   std::u16string_view rBefore,
                                                   std::u16string_view rAfter,
                                                   const std::optional<OUString>& rTargetTextHash
                                                       = std::nullopt);

// Apply engine — caller is SwDocShell::applyDiagnosticsPlan.
//
// Pipeline (W3 spec §"Apply Pipeline"):
//   1. validate(plan)            → fail → ValidationFailed / StaleSnapshot
//   2. EnterListAction("Apply AI Plan {plan_id}")
//   3. for each patch: apply or rollback-and-return PatchFailed
//   4. LeaveListAction
//   5. EvidenceRecorder.write
//   6. return ApplyResult{Ok, ...}
class SW_DLLPUBLIC ApplyEngine
{
public:
    explicit ApplyEngine(SwDocShell& rDocShell);
    ~ApplyEngine();

    ApplyResult run(const ApplyPlan& rPlan);

private:
    SwDocShell& mrDocShell;
};

} // namespace sw::intelligent

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
