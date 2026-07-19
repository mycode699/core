/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * W8-C / C1: minimal IntelligentCalcApplyEngine — cell-replace + cell-formula.
 */

#include <IntelligentCalcApplyEngine.hxx>

#include <address.hxx>
#include <docfunc.hxx>
#include <docsh.hxx>
#include <document.hxx>
#include <formula/grammar.hxx>
#include <formulacell.hxx>
#include <tabvwsh.hxx>
#include <viewdata.hxx>

#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <sfx2/objsh.hxx>
#include <svl/undo.hxx>

#include <algorithm>
#include <string_view>

namespace sc::intelligent
{
namespace
{
constexpr sal_Int32 kMaxRuntimePatches = 64;

constexpr const char kKindCellReplace[] = "cell-replace";
constexpr const char kKindCellFormula[] = "cell-formula";

constexpr const char kStatusOk[] = "ok";
constexpr const char kStatusValidationFailed[] = "validation-failed";
constexpr const char kStatusPatchFailed[] = "patch-failed";
constexpr const char kStatusUnsupported[] = "unsupported";
constexpr const char kStatusUndoException[] = "undo-exception";

std::optional<OUString> lcl_extractJsonStringFieldFromNeedle(const OUString& rJson,
                                                             const OUString& rNeedle)
{
    const sal_Int32 nStart = rJson.indexOf(rNeedle);
    if (nStart < 0)
        return std::nullopt;

    sal_Int32 i = nStart + rNeedle.getLength();
    OUStringBuffer aValue;
    for (; i < rJson.getLength(); ++i)
    {
        const sal_Unicode c = rJson[i];
        if (c == '\\' && i + 1 < rJson.getLength())
        {
            const sal_Unicode e = rJson[++i];
            switch (e)
            {
                case '"':
                    aValue.append('"');
                    break;
                case '\\':
                    aValue.append('\\');
                    break;
                case 'n':
                    aValue.append('\n');
                    break;
                case 'r':
                    aValue.append('\r');
                    break;
                case 't':
                    aValue.append('\t');
                    break;
                default:
                    aValue.append(e);
                    break;
            }
            continue;
        }
        if (c == '"')
            return aValue.makeStringAndClear();
        aValue.append(c);
    }
    return std::nullopt;
}

std::optional<OUString> lcl_extractJsonStringField(const OUString& rJson, std::u16string_view rKey)
{
    OUStringBuffer aNeedle(24);
    aNeedle.append('"');
    aNeedle.append(rKey.data(), static_cast<sal_Int32>(rKey.size()));
    aNeedle.append("\":\"");
    if (auto oVal = lcl_extractJsonStringFieldFromNeedle(rJson, aNeedle.makeStringAndClear()))
        return oVal;

    aNeedle.setLength(0);
    aNeedle.append('"');
    aNeedle.append(rKey.data(), static_cast<sal_Int32>(rKey.size()));
    aNeedle.append("\": \"");
    return lcl_extractJsonStringFieldFromNeedle(rJson, aNeedle.makeStringAndClear());
}

std::optional<bool> lcl_parseJsonBoolKey(std::u16string_view rJson, std::u16string_view rKey)
{
    OUStringBuffer aNeedle(24);
    aNeedle.append('"');
    aNeedle.append(rKey.data(), static_cast<sal_Int32>(rKey.size()));
    aNeedle.append("\":");
    const OUString aKey = aNeedle.makeStringAndClear();
    const OUString aJson(rJson.data(), static_cast<sal_Int32>(rJson.size()));
    sal_Int32 nPos = aJson.indexOf(aKey);
    if (nPos < 0)
    {
        aNeedle.setLength(0);
        aNeedle.append('"');
        aNeedle.append(rKey.data(), static_cast<sal_Int32>(rKey.size()));
        aNeedle.append("\": ");
        nPos = aJson.indexOf(aNeedle.makeStringAndClear());
        if (nPos < 0)
            return std::nullopt;
        nPos += static_cast<sal_Int32>(rKey.size()) + 4; // "key":
    }
    else
        nPos += aKey.getLength();

    while (nPos < aJson.getLength()
           && (aJson[nPos] == u' ' || aJson[nPos] == u'\t' || aJson[nPos] == u'\n'
               || aJson[nPos] == u'\r'))
        ++nPos;
    if (nPos + 4 <= aJson.getLength() && aJson.match(u"true"_ustr, nPos))
        return true;
    if (nPos + 5 <= aJson.getLength() && aJson.match(u"false"_ustr, nPos))
        return false;
    return std::nullopt;
}

sal_Int32 lcl_findMatchingJsonBrace(const OUString& rJson, sal_Int32 nOpen)
{
    if (nOpen < 0 || nOpen >= rJson.getLength() || rJson[nOpen] != u'{')
        return -1;
    sal_Int32 nDepth = 0;
    bool bInString = false;
    bool bEscape = false;
    for (sal_Int32 i = nOpen; i < rJson.getLength(); ++i)
    {
        const sal_Unicode c = rJson[i];
        if (bInString)
        {
            if (bEscape)
                bEscape = false;
            else if (c == u'\\')
                bEscape = true;
            else if (c == u'"')
                bInString = false;
            continue;
        }
        if (c == u'"')
        {
            bInString = true;
            continue;
        }
        if (c == u'{')
            ++nDepth;
        else if (c == u'}')
        {
            --nDepth;
            if (nDepth == 0)
                return i;
        }
    }
    return -1;
}

/// Normalize target cell token: "A1", "cell:A1", "sheet!A1" → "A1" (sheet rejected later).
OUString lcl_normalizeCellRef(const OUString& rRaw)
{
    OUString s = rRaw.trim();
    if (s.startsWith(u"cell:"_ustr))
        s = s.copy(5).trim();
    // Reject explicit sheet / multi-sheet syntax in C1.
    if (s.indexOf(u'!') >= 0 || s.indexOf(u':') >= 0 || s.indexOf(u'[') >= 0)
        return OUString();
    return s;
}

std::optional<Patch> lcl_parsePatchObject(const OUString& rPatchJson)
{
    const std::optional<OUString> oPatchId = lcl_extractJsonStringField(rPatchJson, u"patch_id");
    const std::optional<OUString> oKind = lcl_extractJsonStringField(rPatchJson, u"kind");
    if (!oPatchId || !oKind)
        return std::nullopt;

    // Accept target.cell or top-level "cell" / "target" string.
    std::optional<OUString> oCell = lcl_extractJsonStringField(rPatchJson, u"cell");
    if (!oCell)
        oCell = lcl_extractJsonStringField(rPatchJson, u"target");
    if (!oCell)
        return std::nullopt;

    const OString aKindUtf8 = OUStringToOString(*oKind, RTL_TEXTENCODING_UTF8);
    const std::optional<PatchKind> oPatchKind = PatchKindFromToken(aKindUtf8);
    if (!oPatchKind)
        return std::nullopt;

    const OUString aCell = lcl_normalizeCellRef(*oCell);
    if (aCell.isEmpty())
        return std::nullopt;

    Patch aPatch;
    aPatch.maPatchId = *oPatchId;
    aPatch.meKind = *oPatchKind;
    aPatch.maCellRef = aCell;
    if (const std::optional<OUString> oBefore = lcl_extractJsonStringField(rPatchJson, u"before"))
        aPatch.maBefore = *oBefore;
    if (const std::optional<OUString> oAfter = lcl_extractJsonStringField(rPatchJson, u"after"))
        aPatch.maAfter = *oAfter;
    // also accept "formula" / "new_text" aliases
    if (aPatch.maAfter.isEmpty())
    {
        if (const std::optional<OUString> oF = lcl_extractJsonStringField(rPatchJson, u"formula"))
            aPatch.maAfter = *oF;
    }
    if (aPatch.maAfter.isEmpty())
    {
        if (const std::optional<OUString> oN = lcl_extractJsonStringField(rPatchJson, u"new_text"))
            aPatch.maAfter = *oN;
    }
    return aPatch;
}

std::optional<std::vector<Patch>> lcl_parsePatchesArray(const OUString& rJsonBody)
{
    const sal_Int32 nPatchesKey = rJsonBody.indexOf(u"\"patches\""_ustr);
    if (nPatchesKey < 0)
        return std::nullopt;

    const sal_Int32 nArrayStart = rJsonBody.indexOf('[', nPatchesKey);
    if (nArrayStart < 0)
        return std::nullopt;

    std::vector<Patch> aPatches;
    sal_Int32 nPos = nArrayStart + 1;
    static constexpr OUString kPatchIdKey = u"\"patch_id\""_ustr;

    while (aPatches.size() < kMaxRuntimePatches)
    {
        const sal_Int32 nKeyPos = rJsonBody.indexOf(kPatchIdKey, nPos);
        if (nKeyPos < 0)
            break;

        sal_Int32 nPatchStart = nKeyPos;
        while (nPatchStart > nArrayStart && rJsonBody[nPatchStart] != u'{')
            --nPatchStart;
        if (nPatchStart <= nArrayStart || rJsonBody[nPatchStart] != u'{')
            return std::nullopt;

        const sal_Int32 nPatchEnd = lcl_findMatchingJsonBrace(rJsonBody, nPatchStart);
        if (nPatchEnd < 0)
            return std::nullopt;

        const OUString aPatchJson = rJsonBody.copy(nPatchStart, nPatchEnd - nPatchStart + 1);
        const std::optional<Patch> oPatch = lcl_parsePatchObject(aPatchJson);
        if (!oPatch)
            return std::nullopt;
        aPatches.push_back(*oPatch);
        nPos = nPatchEnd + 1;
    }

    if (aPatches.empty())
        return std::nullopt;
    return aPatches;
}

bool lcl_parseA1(const ScDocument& rDoc, const OUString& rCellRef, SCTAB nTab, ScAddress& rOut)
{
    ScAddress aPos(0, 0, nTab);
    const ScRefFlags nFlags = aPos.Parse(rCellRef, rDoc, rDoc.GetAddressConvention());
    if (!(nFlags & ScRefFlags::VALID) || !(nFlags & ScRefFlags::COL_VALID)
        || !(nFlags & ScRefFlags::ROW_VALID))
        return false;
    aPos.SetTab(nTab);
    if (!rDoc.HasTable(nTab))
        return false;
    rOut = aPos;
    return true;
}

SCTAB lcl_activeTab(ScDocShell& rDocShell)
{
    if (ScTabViewShell* pView = rDocShell.GetBestViewShell())
        return pView->GetViewData().CurrentTabForData();
    return 0;
}

bool lcl_applyOnePatch(ScDocShell& rDocShell, const Patch& rPatch, SCTAB nTab, OUString& rError)
{
    ScDocument& rDoc = rDocShell.GetDocument();
    ScAddress aPos;
    if (!lcl_parseA1(rDoc, rPatch.maCellRef, nTab, aPos))
    {
        rError = u"表格写回失败 · 无效单元格目标 "_ustr + rPatch.maCellRef
                 + u"（仅支持活动表 cell:A1 形式）"_ustr;
        return false;
    }

    ScDocFunc& rFunc = rDocShell.GetDocFunc();
    if (rPatch.meKind == PatchKind::CellFormula)
    {
        OUString aFormula = rPatch.maAfter.trim();
        if (aFormula.isEmpty())
        {
            rError = u"表格写回失败 · cell-formula 缺少公式内容"_ustr;
            return false;
        }
        if (!aFormula.startsWith(u"="))
            aFormula = u"="_ustr + aFormula;
        const formula::FormulaGrammar::Grammar eGrammar = rDoc.GetGrammar();
        auto* pCell = new ScFormulaCell(rDoc, aPos, aFormula, eGrammar);
        const bool bOk = rFunc.SetFormulaCell(aPos, pCell, /*bInteraction*/ true);
        if (!bOk)
        {
            rError = u"表格写回失败 · 无法写入公式到 "_ustr + rPatch.maCellRef;
            return false;
        }
        return true;
    }

    // cell-replace
    const bool bOk
        = rFunc.SetStringOrEditCell(aPos, rPatch.maAfter, /*bInteraction*/ true);
    if (!bOk)
    {
        rError = u"表格写回失败 · 无法写入单元格 "_ustr + rPatch.maCellRef;
        return false;
    }
    return true;
}

} // namespace

OString PatchKindToken(PatchKind eKind)
{
    switch (eKind)
    {
        case PatchKind::CellReplace:
            return kKindCellReplace;
        case PatchKind::CellFormula:
            return kKindCellFormula;
    }
    return "unknown";
}

std::optional<PatchKind> PatchKindFromToken(std::string_view aToken)
{
    if (aToken == kKindCellReplace)
        return PatchKind::CellReplace;
    if (aToken == kKindCellFormula)
        return PatchKind::CellFormula;
    return std::nullopt;
}

OString ApplyStatusToken(ApplyStatus eStatus)
{
    switch (eStatus)
    {
        case ApplyStatus::Ok:
            return kStatusOk;
        case ApplyStatus::ValidationFailed:
            return kStatusValidationFailed;
        case ApplyStatus::PatchFailed:
            return kStatusPatchFailed;
        case ApplyStatus::Unsupported:
            return kStatusUnsupported;
        case ApplyStatus::UndoException:
            return kStatusUndoException;
    }
    return "unknown";
}

bool ValidateCalcApplyPlanShape(const ApplyPlan& rPlan, OUString& rError)
{
    if (rPlan.maSchemaVersion != OUString::fromUtf8(kCalcSchemaVersion))
    {
        rError = u"表格写回失败 · 不支持的 schema_version（需要 v1-calc-runtime-1）"_ustr;
        return false;
    }
    if (rPlan.maPlanId.isEmpty())
    {
        rError = u"表格写回失败 · 缺少 plan_id"_ustr;
        return false;
    }
    if (rPlan.mbPreviewOnly)
    {
        rError = u"预览计划不可写回 · 请生成可批准的正式计划"_ustr;
        return false;
    }
    if (rPlan.maPatches.empty())
    {
        rError = u"表格写回失败 · 空 patches"_ustr;
        return false;
    }
    if (static_cast<sal_Int32>(rPlan.maPatches.size()) > kMaxRuntimePatches)
    {
        rError = u"表格写回失败 · patches 超过上限（64）"_ustr;
        return false;
    }
    for (const Patch& p : rPlan.maPatches)
    {
        if (p.maPatchId.isEmpty() || p.maCellRef.isEmpty())
        {
            rError = u"表格写回失败 · patch 缺少 patch_id 或 cell"_ustr;
            return false;
        }
        if (p.meKind == PatchKind::CellFormula && p.maAfter.isEmpty())
        {
            rError = u"表格写回失败 · cell-formula 需要 after/formula"_ustr;
            return false;
        }
        // cell-replace may clear a cell (empty after is allowed)
    }
    return true;
}

std::optional<ApplyPlan> TryParseCalcApplyPlanRuntimeJson(const OUString& rJsonBody)
{
    const std::optional<OUString> oSchema = lcl_extractJsonStringField(rJsonBody, u"schema_version");
    if (!oSchema || *oSchema != OUString::fromUtf8(kCalcSchemaVersion))
        return std::nullopt;

    ApplyPlan aPlan;
    aPlan.maSchemaVersion = *oSchema;
    aPlan.maPlanId = lcl_extractJsonStringField(rJsonBody, u"plan_id").value_or(OUString());
    if (const std::optional<bool> oPreview = lcl_parseJsonBoolKey(rJsonBody, u"preview_only"))
        aPlan.mbPreviewOnly = *oPreview;

    const std::optional<std::vector<Patch>> oPatches = lcl_parsePatchesArray(rJsonBody);
    if (!oPatches)
    {
        // Flat single-patch form: kind + cell + after at top level.
        const std::optional<OUString> oKind = lcl_extractJsonStringField(rJsonBody, u"kind");
        if (!oKind)
            return std::nullopt;
        const OString aKindUtf8 = OUStringToOString(*oKind, RTL_TEXTENCODING_UTF8);
        const std::optional<PatchKind> oPatchKind = PatchKindFromToken(aKindUtf8);
        if (!oPatchKind)
            return std::nullopt;
        std::optional<OUString> oCell = lcl_extractJsonStringField(rJsonBody, u"cell");
        if (!oCell)
            oCell = lcl_extractJsonStringField(rJsonBody, u"target");
        if (!oCell)
            return std::nullopt;
        const OUString aCell = lcl_normalizeCellRef(*oCell);
        if (aCell.isEmpty())
            return std::nullopt;
        Patch aPatch;
        aPatch.maPatchId
            = lcl_extractJsonStringField(rJsonBody, u"patch_id").value_or(u"p1"_ustr);
        aPatch.meKind = *oPatchKind;
        aPatch.maCellRef = aCell;
        aPatch.maAfter
            = lcl_extractJsonStringField(rJsonBody, u"after")
                  .value_or(lcl_extractJsonStringField(rJsonBody, u"formula")
                                .value_or(lcl_extractJsonStringField(rJsonBody, u"new_text")
                                              .value_or(OUString())));
        aPlan.maPatches.push_back(std::move(aPatch));
    }
    else
        aPlan.maPatches = *oPatches;

    if (aPlan.maPlanId.isEmpty())
        aPlan.maPlanId = u"ap-calc-apply"_ustr;
    return aPlan;
}

ApplyResult ApplyCalcPlan(ScDocShell& rDocShell, const ApplyPlan& rPlan)
{
    ApplyResult aResult;
    OUString aShapeError;
    if (!ValidateCalcApplyPlanShape(rPlan, aShapeError))
    {
        aResult.meStatus = ApplyStatus::ValidationFailed;
        aResult.maError = aShapeError;
        return aResult;
    }

    SfxUndoManager* pUndoMgr = rDocShell.GetUndoManager();
    if (!pUndoMgr)
    {
        aResult.meStatus = ApplyStatus::UndoException;
        aResult.maError = u"表格写回失败 · 无撤销管理器 · 主文档未改"_ustr;
        return aResult;
    }

    const SCTAB nTab = lcl_activeTab(rDocShell);
    const OUString aListComment = u"Apply AI Plan "_ustr + rPlan.maPlanId;
    ViewShellId nViewShellId(-1);
    if (ScTabViewShell* pViewSh = ScTabViewShell::GetActiveViewShell())
        nViewShellId = pViewSh->GetViewShellId();

    pUndoMgr->EnterListAction(aListComment, aListComment, 0, nViewShellId);

    sal_Int32 nApplied = 0;
    for (const Patch& rPatch : rPlan.maPatches)
    {
        OUString aErr;
        if (!lcl_applyOnePatch(rDocShell, rPatch, nTab, aErr))
        {
            // Roll back any successful patches in this list action.
            pUndoMgr->LeaveListAction();
            if (nApplied > 0)
            {
                try
                {
                    pUndoMgr->Undo();
                }
                catch (...)
                {
                    aResult.meStatus = ApplyStatus::UndoException;
                    aResult.maError
                        = u"表格写回失败 · 回滚异常 · 请用撤销检查文档状态"_ustr;
                    aResult.maFailedPatchId = rPatch.maPatchId;
                    aResult.mnAppliedCount = 0;
                    SAL_WARN("sc.apply", "ApplyCalcPlan undo-exception plan=" << rPlan.maPlanId);
                    return aResult;
                }
            }
            aResult.meStatus = ApplyStatus::PatchFailed;
            aResult.maError = aErr.isEmpty()
                                  ? (u"表格写回失败 · patch="_ustr + rPatch.maPatchId)
                                  : aErr;
            aResult.maFailedPatchId = rPatch.maPatchId;
            aResult.mnAppliedCount = 0;
            SAL_WARN("sc.apply", "ApplyCalcPlan patch-failed plan="
                                     << rPlan.maPlanId << " patch=" << rPatch.maPatchId);
            return aResult;
        }
        ++nApplied;
    }

    pUndoMgr->LeaveListAction();
    aResult.meStatus = ApplyStatus::Ok;
    aResult.mnAppliedCount = nApplied;
    SAL_INFO("sc.apply", "ApplyCalcPlan ok plan=" << rPlan.maPlanId << " applied=" << nApplied);
    return aResult;
}

} // namespace sc::intelligent

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
