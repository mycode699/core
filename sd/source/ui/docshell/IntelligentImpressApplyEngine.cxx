/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * W9-B / I1: minimal IntelligentImpressApplyEngine — shape-text-replace
 * (slide-insert accepted in schema but returns honest unsupported for UNO fallback).
 */

#include <IntelligentImpressApplyEngine.hxx>

#include <DrawDocShell.hxx>
#include <drawdoc.hxx>
#include <pres.hxx>
#include <sdpage.hxx>

#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <sfx2/objsh.hxx>
#include <svl/undo.hxx>
#include <svx/svdobj.hxx>
#include <svx/svdotext.hxx>
#include <svx/svdundo.hxx>

#include <algorithm>
#include <memory>
#include <string_view>

namespace sd::intelligent
{
namespace
{
constexpr sal_Int32 kMaxRuntimePatches = 64;

constexpr const char kKindShapeTextReplace[] = "shape-text-replace";
constexpr const char kKindSlideInsert[] = "slide-insert";

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

std::optional<sal_Int32> lcl_extractJsonIntField(const OUString& rJson, std::u16string_view rKey)
{
    OUStringBuffer aNeedle(24);
    aNeedle.append('"');
    aNeedle.append(rKey.data(), static_cast<sal_Int32>(rKey.size()));
    aNeedle.append("\":");
    const OUString aKey = aNeedle.makeStringAndClear();
    sal_Int32 nPos = rJson.indexOf(aKey);
    if (nPos < 0)
    {
        aNeedle.setLength(0);
        aNeedle.append('"');
        aNeedle.append(rKey.data(), static_cast<sal_Int32>(rKey.size()));
        aNeedle.append("\": ");
        nPos = rJson.indexOf(aNeedle.makeStringAndClear());
        if (nPos < 0)
            return std::nullopt;
        nPos += static_cast<sal_Int32>(rKey.size()) + 4;
    }
    else
        nPos += aKey.getLength();

    while (nPos < rJson.getLength()
           && (rJson[nPos] == u' ' || rJson[nPos] == u'\t' || rJson[nPos] == u'\n'
               || rJson[nPos] == u'\r'))
        ++nPos;

    if (nPos >= rJson.getLength())
        return std::nullopt;

    // Also accept JSON string numbers: "slide":"1"
    if (rJson[nPos] == u'"')
    {
        if (auto oStr = lcl_extractJsonStringField(rJson, rKey))
        {
            const OUString s = oStr->trim();
            if (s.isEmpty())
                return std::nullopt;
            for (sal_Int32 i = 0; i < s.getLength(); ++i)
                if (s[i] < u'0' || s[i] > u'9')
                    return std::nullopt;
            return s.toInt32();
        }
        return std::nullopt;
    }

    bool bNeg = false;
    if (rJson[nPos] == u'-')
    {
        bNeg = true;
        ++nPos;
    }
    if (nPos >= rJson.getLength() || rJson[nPos] < u'0' || rJson[nPos] > u'9')
        return std::nullopt;
    sal_Int32 nVal = 0;
    while (nPos < rJson.getLength() && rJson[nPos] >= u'0' && rJson[nPos] <= u'9')
    {
        nVal = nVal * 10 + (rJson[nPos] - u'0');
        ++nPos;
    }
    return bNeg ? -nVal : nVal;
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

/// Parse "slide:N:shape:M" or "slide:N" → (slide1, shape0 or -1).
bool lcl_parseSlideShapeTarget(const OUString& rRaw, sal_Int32& rSlide1, sal_Int32& rShape0)
{
    rSlide1 = 0;
    rShape0 = -1;
    OUString s = rRaw.trim();
    if (!s.startsWith(u"slide:"_ustr))
        return false;
    const OUString rest = s.copy(5);
    const sal_Int32 shapeKey = rest.indexOf(u":shape:"_ustr);
    OUString slidePart;
    if (shapeKey < 0)
    {
        slidePart = rest;
    }
    else
    {
        slidePart = rest.copy(0, shapeKey);
        const OUString shapePart = rest.copy(shapeKey + 7).trim();
        if (shapePart.isEmpty())
            return false;
        for (sal_Int32 i = 0; i < shapePart.getLength(); ++i)
            if (shapePart[i] < u'0' || shapePart[i] > u'9')
                return false;
        // Chat UNO uses 0-based shape index in "shape:M" when M is the index from
        // XIndexAccess; AgentChatDiffApplier treats shapeIndex as 0-based already.
        // Documented target "slide:1:shape:2" uses 0-based shape ordinal in applier.
        rShape0 = shapePart.toInt32();
    }
    slidePart = slidePart.trim();
    if (slidePart.isEmpty())
        return false;
    for (sal_Int32 i = 0; i < slidePart.getLength(); ++i)
        if (slidePart[i] < u'0' || slidePart[i] > u'9')
            return false;
    rSlide1 = slidePart.toInt32();
    return rSlide1 >= 1;
}

std::optional<Patch> lcl_parsePatchObject(const OUString& rPatchJson)
{
    const std::optional<OUString> oPatchId = lcl_extractJsonStringField(rPatchJson, u"patch_id");
    const std::optional<OUString> oKind = lcl_extractJsonStringField(rPatchJson, u"kind");
    if (!oPatchId || !oKind)
        return std::nullopt;

    const OString aKindUtf8 = OUStringToOString(*oKind, RTL_TEXTENCODING_UTF8);
    const std::optional<PatchKind> oPatchKind = PatchKindFromToken(aKindUtf8);
    if (!oPatchKind)
        return std::nullopt;

    Patch aPatch;
    aPatch.maPatchId = *oPatchId;
    aPatch.meKind = *oPatchKind;

    // Prefer target string (slide:N:shape:M); also accept slide/shape numeric fields.
    if (const std::optional<OUString> oTarget = lcl_extractJsonStringField(rPatchJson, u"target"))
    {
        sal_Int32 nSlide = 0;
        sal_Int32 nShape = -1;
        if (!lcl_parseSlideShapeTarget(*oTarget, nSlide, nShape))
            return std::nullopt;
        aPatch.mnSlide1Based = nSlide;
        aPatch.mnShape0Based = nShape;
    }
    else
    {
        if (const std::optional<sal_Int32> oSlide = lcl_extractJsonIntField(rPatchJson, u"slide"))
            aPatch.mnSlide1Based = *oSlide;
        if (const std::optional<sal_Int32> oShape = lcl_extractJsonIntField(rPatchJson, u"shape"))
            aPatch.mnShape0Based = *oShape;
    }

    if (const std::optional<OUString> oBefore = lcl_extractJsonStringField(rPatchJson, u"before"))
        aPatch.maBefore = *oBefore;
    if (const std::optional<OUString> oAfter = lcl_extractJsonStringField(rPatchJson, u"after"))
        aPatch.maAfter = *oAfter;
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

bool lcl_applyShapeTextReplace(DrawDocShell& rDocShell, const Patch& rPatch, OUString& rError)
{
    SdDrawDocument* pDoc = rDocShell.GetDoc();
    if (!pDoc)
    {
        rError = u"演示写回失败 · 无活动演示文档"_ustr;
        return false;
    }

    if (rPatch.mnSlide1Based < 1)
    {
        rError = u"演示写回失败 · 无效幻灯索引（需要 slide ≥ 1）"_ustr;
        return false;
    }
    if (rPatch.mnShape0Based < 0)
    {
        rError = u"演示写回失败 · shape-text-replace 需要 shape 索引（slide:N:shape:M）"_ustr;
        return false;
    }

    const sal_uInt16 nSlide0 = static_cast<sal_uInt16>(rPatch.mnSlide1Based - 1);
    const sal_uInt16 nPageCount = pDoc->GetSdPageCount(PageKind::Standard);
    if (nSlide0 >= nPageCount)
    {
        rError = u"演示写回失败 · 幻灯 "_ustr + OUString::number(rPatch.mnSlide1Based)
                 + u" 不存在（当前共 "_ustr + OUString::number(static_cast<sal_Int32>(nPageCount))
                 + u" 页）"_ustr;
        return false;
    }

    SdPage* pPage = pDoc->GetSdPage(nSlide0, PageKind::Standard);
    if (!pPage)
    {
        rError = u"演示写回失败 · 无法获取幻灯页"_ustr;
        return false;
    }

    const size_t nObjCount = pPage->GetObjCount();
    if (static_cast<size_t>(rPatch.mnShape0Based) >= nObjCount)
    {
        rError = u"演示写回失败 · 形状索引 "_ustr + OUString::number(rPatch.mnShape0Based)
                 + u" 超出幻灯 "_ustr + OUString::number(rPatch.mnSlide1Based) + u" 范围"_ustr;
        return false;
    }

    SdrObject* pObj = pPage->GetObj(static_cast<size_t>(rPatch.mnShape0Based));
    SdrTextObj* pTextObj = DynCastSdrTextObj(pObj);
    if (!pTextObj)
    {
        rError = u"演示写回失败 · 目标形状不支持文本"_ustr;
        return false;
    }

    // Undo: capture old text → SetText → AfterSetText → AddUndo.
    pDoc->BegUndo(u"Apply AI Plan shape-text-replace"_ustr);
    std::unique_ptr<SdrUndoObjSetText> pTxtUndo;
    if (pDoc->IsUndoEnabled())
        pTxtUndo = std::make_unique<SdrUndoObjSetText>(*pTextObj, /*nText*/ 0);

    pTextObj->SetText(rPatch.maAfter);
    pTextObj->SetEmptyPresObj(false);

    if (pTxtUndo)
    {
        pTxtUndo->AfterSetText();
        if (pTxtUndo->IsDifferent())
            pDoc->AddUndo(std::move(pTxtUndo));
    }
    pDoc->EndUndo();

    rDocShell.SetModified();
    return true;
}

} // namespace

OString PatchKindToken(PatchKind eKind)
{
    switch (eKind)
    {
        case PatchKind::ShapeTextReplace:
            return kKindShapeTextReplace;
        case PatchKind::SlideInsert:
            return kKindSlideInsert;
    }
    return "unknown";
}

std::optional<PatchKind> PatchKindFromToken(std::string_view aToken)
{
    if (aToken == kKindShapeTextReplace)
        return PatchKind::ShapeTextReplace;
    if (aToken == kKindSlideInsert)
        return PatchKind::SlideInsert;
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

bool ValidateImpressApplyPlanShape(const ApplyPlan& rPlan, OUString& rError)
{
    if (rPlan.maSchemaVersion != OUString::fromUtf8(kImpressSchemaVersion))
    {
        rError = u"演示写回失败 · 不支持的 schema_version（需要 v1-impress-runtime-1）"_ustr;
        return false;
    }
    if (rPlan.maPlanId.isEmpty())
    {
        rError = u"演示写回失败 · 缺少 plan_id"_ustr;
        return false;
    }
    if (rPlan.mbPreviewOnly)
    {
        rError = u"预览计划不可写回 · 请生成可批准的正式计划"_ustr;
        return false;
    }
    if (rPlan.maPatches.empty())
    {
        rError = u"演示写回失败 · 空 patches"_ustr;
        return false;
    }
    if (static_cast<sal_Int32>(rPlan.maPatches.size()) > kMaxRuntimePatches)
    {
        rError = u"演示写回失败 · patches 超过上限（64）"_ustr;
        return false;
    }
    for (const Patch& p : rPlan.maPatches)
    {
        if (p.maPatchId.isEmpty())
        {
            rError = u"演示写回失败 · patch 缺少 patch_id"_ustr;
            return false;
        }
        if (p.meKind == PatchKind::ShapeTextReplace)
        {
            if (p.mnSlide1Based < 1 || p.mnShape0Based < 0)
            {
                rError = u"演示写回失败 · shape-text-replace 需要 slide:N:shape:M"_ustr;
                return false;
            }
        }
        else if (p.meKind == PatchKind::SlideInsert)
        {
            if (p.mnSlide1Based < 1)
            {
                rError = u"演示写回失败 · slide-insert 需要 slide ≥ 1"_ustr;
                return false;
            }
        }
    }
    return true;
}

std::optional<ApplyPlan> TryParseImpressApplyPlanRuntimeJson(const OUString& rJsonBody)
{
    const std::optional<OUString> oSchema = lcl_extractJsonStringField(rJsonBody, u"schema_version");
    if (!oSchema || *oSchema != OUString::fromUtf8(kImpressSchemaVersion))
        return std::nullopt;

    ApplyPlan aPlan;
    aPlan.maSchemaVersion = *oSchema;
    aPlan.maPlanId = lcl_extractJsonStringField(rJsonBody, u"plan_id").value_or(OUString());
    if (const std::optional<bool> oPreview = lcl_parseJsonBoolKey(rJsonBody, u"preview_only"))
        aPlan.mbPreviewOnly = *oPreview;

    const std::optional<std::vector<Patch>> oPatches = lcl_parsePatchesArray(rJsonBody);
    if (!oPatches)
    {
        // Flat single-patch form: kind + target/slide/shape + after at top level.
        const std::optional<OUString> oKind = lcl_extractJsonStringField(rJsonBody, u"kind");
        if (!oKind)
            return std::nullopt;
        const OString aKindUtf8 = OUStringToOString(*oKind, RTL_TEXTENCODING_UTF8);
        const std::optional<PatchKind> oPatchKind = PatchKindFromToken(aKindUtf8);
        if (!oPatchKind)
            return std::nullopt;

        Patch aPatch;
        aPatch.maPatchId
            = lcl_extractJsonStringField(rJsonBody, u"patch_id").value_or(u"p1"_ustr);
        aPatch.meKind = *oPatchKind;

        if (const std::optional<OUString> oTarget = lcl_extractJsonStringField(rJsonBody, u"target"))
        {
            sal_Int32 nSlide = 0;
            sal_Int32 nShape = -1;
            if (!lcl_parseSlideShapeTarget(*oTarget, nSlide, nShape))
                return std::nullopt;
            aPatch.mnSlide1Based = nSlide;
            aPatch.mnShape0Based = nShape;
        }
        else
        {
            if (const std::optional<sal_Int32> oSlide = lcl_extractJsonIntField(rJsonBody, u"slide"))
                aPatch.mnSlide1Based = *oSlide;
            if (const std::optional<sal_Int32> oShape = lcl_extractJsonIntField(rJsonBody, u"shape"))
                aPatch.mnShape0Based = *oShape;
        }

        aPatch.maAfter
            = lcl_extractJsonStringField(rJsonBody, u"after")
                  .value_or(lcl_extractJsonStringField(rJsonBody, u"new_text").value_or(OUString()));
        aPlan.maPatches.push_back(std::move(aPatch));
    }
    else
        aPlan.maPatches = *oPatches;

    if (aPlan.maPlanId.isEmpty())
        aPlan.maPlanId = u"ap-impress-apply"_ustr;
    return aPlan;
}

ApplyResult ApplyImpressPlan(DrawDocShell& rDocShell, const ApplyPlan& rPlan)
{
    ApplyResult aResult;
    OUString aShapeError;
    if (!ValidateImpressApplyPlanShape(rPlan, aShapeError))
    {
        aResult.meStatus = ApplyStatus::ValidationFailed;
        aResult.maError = aShapeError;
        return aResult;
    }

    SfxUndoManager* pUndoMgr = rDocShell.GetUndoManager();
    if (!pUndoMgr)
    {
        aResult.meStatus = ApplyStatus::UndoException;
        aResult.maError = u"演示写回失败 · 无撤销管理器 · 主文档未改"_ustr;
        return aResult;
    }

    const OUString aListComment = u"Apply AI Plan "_ustr + rPlan.maPlanId;
    ViewShellId nViewShellId(-1);
    pUndoMgr->EnterListAction(aListComment, aListComment, 0, nViewShellId);

    sal_Int32 nApplied = 0;
    for (const Patch& rPatch : rPlan.maPatches)
    {
        OUString aErr;
        bool bOk = false;

        if (rPatch.meKind == PatchKind::ShapeTextReplace)
        {
            bOk = lcl_applyShapeTextReplace(rDocShell, rPatch, aErr);
        }
        else if (rPatch.meKind == PatchKind::SlideInsert)
        {
            // I1: honest unsupported — DocumentAIApply falls back to UNO DiffApplier
            // (outline multi-slide / impressFillSlide). No silent write.
            aErr = u"演示写回 · I1 原生骨架暂不支持 slide-insert（请用 UNO 大纲成片或 shape-text-replace）"_ustr;
            bOk = false;
            // Mark as unsupported for clearer status token after rollback.
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
                        = u"演示写回失败 · 回滚异常 · 请用撤销检查文档状态"_ustr;
                    aResult.maFailedPatchId = rPatch.maPatchId;
                    aResult.mnAppliedCount = 0;
                    return aResult;
                }
            }
            aResult.meStatus = ApplyStatus::Unsupported;
            aResult.maError = aErr;
            aResult.maFailedPatchId = rPatch.maPatchId;
            aResult.mnAppliedCount = 0;
            SAL_WARN("sd.apply", "ApplyImpressPlan unsupported slide-insert plan="
                                     << rPlan.maPlanId << " patch=" << rPatch.maPatchId);
            return aResult;
        }
        else
        {
            aErr = u"演示写回失败 · 未知 patch kind"_ustr;
            bOk = false;
        }

        if (!bOk)
        {
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
                        = u"演示写回失败 · 回滚异常 · 请用撤销检查文档状态"_ustr;
                    aResult.maFailedPatchId = rPatch.maPatchId;
                    aResult.mnAppliedCount = 0;
                    SAL_WARN("sd.apply",
                             "ApplyImpressPlan undo-exception plan=" << rPlan.maPlanId);
                    return aResult;
                }
            }
            aResult.meStatus = ApplyStatus::PatchFailed;
            aResult.maError = aErr.isEmpty()
                                  ? (u"演示写回失败 · patch="_ustr + rPatch.maPatchId)
                                  : aErr;
            aResult.maFailedPatchId = rPatch.maPatchId;
            aResult.mnAppliedCount = 0;
            SAL_WARN("sd.apply", "ApplyImpressPlan patch-failed plan="
                                     << rPlan.maPlanId << " patch=" << rPatch.maPatchId);
            return aResult;
        }
        ++nApplied;
    }

    pUndoMgr->LeaveListAction();
    aResult.meStatus = ApplyStatus::Ok;
    aResult.mnAppliedCount = nApplied;
    SAL_INFO("sd.apply",
             "ApplyImpressPlan ok plan=" << rPlan.maPlanId << " applied=" << nApplied);
    return aResult;
}

} // namespace sd::intelligent

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
