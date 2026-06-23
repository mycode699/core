/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <IntelligentWriterApplyEngine.hxx>
#include <UndoApplyPatch.hxx>

#include <comphelper/hash.hxx>
#include <doc.hxx>
#include <docsh.hxx>
#include <IDocumentContentOperations.hxx>
#include <IDocumentUndoRedo.hxx>
#include <ndarr.hxx>
#include <ndtxt.hxx>
#include <node.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/objsh.hxx>
#include <format.hxx>
#include <frmatr.hxx>
#include <hintids.hxx>
#include <names.hxx>
#include <pam.hxx>
#include <paratr.hxx>
#include <swcrsr.hxx>

#include <editeng/fontitem.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/lspcitem.hxx>
#include <editeng/svxenum.hxx>
#include <editeng/wghtitem.hxx>

#include <vcl/font.hxx>

#include <rtl/math.h>
#include <rtl/math.hxx>
#include <rtl/string.hxx>
#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cmath>
#include <memory>

namespace sw::intelligent
{

bool ParseParagraphId(const OUString& rParagraphId, sal_uInt32& rnParagraph)
{
    static constexpr OUString kPrefix = u"swpara-"_ustr;
    if (!rParagraphId.startsWith(kPrefix))
        return false;
    const sal_Int32 nIdx = rParagraphId.copy(kPrefix.getLength()).toInt32();
    if (nIdx < 1)
        return false;
    rnParagraph = static_cast<sal_uInt32>(nIdx);
    return true;
}

namespace
{
// 7 patch-kind string literals — H7 harness greps for these.
// Order locked to W3 spec §"Patch Kinds（v1）" table top-to-bottom.
constexpr const char kKindParagraphReplace[]     = "paragraph-replace";
constexpr const char kKindParagraphInsertAfter[] = "paragraph-insert-after";
constexpr const char kKindParagraphDelete[]      = "paragraph-delete";
constexpr const char kKindParagraphFormat[]      = "paragraph-format";
constexpr const char kKindParagraphReformat[]    = "paragraph-reformat";
constexpr const char kKindTextRangeReplace[]     = "text-range-replace";
constexpr const char kKindTextFormat[]           = "text-format";

constexpr const char kStatusOk[]                = "ok";
constexpr const char kStatusValidationFailed[]  = "validation-failed";
constexpr const char kStatusStaleSnapshot[]     = "stale-snapshot";
constexpr const char kStatusPatchFailed[]       = "patch-failed";
constexpr const char kStatusUndoException[]     = "undo-exception";

constexpr const char kPatchStatusOk[]               = "ok";
constexpr const char kPatchStatusFailed[]           = "failed";
constexpr const char kPatchStatusSkippedIdempotent[] = "skipped-idempotent";

constexpr const char kSchemaVersionConst[] = "v2-w3-runtime-1";

OUString lcl_sha256Prefixed(const OString& rPayload)
{
    const std::vector<unsigned char> aDigest = comphelper::Hash::calculateHash(
        rPayload.getStr(), rPayload.getLength(), comphelper::HashType::SHA256);
    return u"sha256:"_ustr
           + OUString::createFromAscii(comphelper::hashToString(aDigest).c_str());
}

sal_uInt32 lcl_countBodyTextParagraphs(const SwDoc& rDoc)
{
    sal_uInt32 nParagraph = 0;
    const SwNodes& rNodes = rDoc.GetNodes();
    for (SwNodeOffset nNode(0); nNode < rNodes.Count(); ++nNode)
    {
        if (rNodes[nNode]->GetTextNode())
            ++nParagraph;
    }
    return nParagraph;
}

const SwTextNode* lcl_getBodyTextParagraph(const SwDoc& rDoc, sal_uInt32 nParagraph)
{
    if (nParagraph < 1)
        return nullptr;
    sal_uInt32 nSeen = 0;
    const SwNodes& rNodes = rDoc.GetNodes();
    for (SwNodeOffset nNode(0); nNode < rNodes.Count(); ++nNode)
    {
        const SwTextNode* pTextNode = rNodes[nNode]->GetTextNode();
        if (!pTextNode)
            continue;
        ++nSeen;
        if (nSeen == nParagraph)
            return pTextNode;
    }
    return nullptr;
}

SwTextNode* lcl_resolveParagraphNode(SwDoc& rDoc, const OUString& rParagraphId)
{
    sal_uInt32 nParagraph = 0;
    if (!ParseParagraphId(rParagraphId, nParagraph))
        return nullptr;
    const SwTextNode* pConst = lcl_getBodyTextParagraph(rDoc, nParagraph);
    return const_cast<SwTextNode*>(pConst);
}

bool lcl_validatePlanShape(const ApplyPlan& rPlan, OUString& rError)
{
    if (rPlan.maSchemaVersion.toUtf8() != kSchemaVersionConst)
    {
        rError = u"schema_version const mismatch"_ustr;
        return false;
    }
    if (rPlan.maPlanId.isEmpty())
    {
        rError = u"plan_id empty"_ustr;
        return false;
    }
    if (rPlan.maSourceDiagnosticId.isEmpty())
    {
        rError = u"source_diagnostic_id empty"_ustr;
        return false;
    }
    if (rPlan.maDocSnapshotHash.isEmpty())
    {
        rError = u"doc_snapshot_hash empty"_ustr;
        return false;
    }
    if (rPlan.maPatches.empty())
    {
        rError = u"patches empty"_ustr;
        return false;
    }
    for (const Patch& rPatch : rPlan.maPatches)
    {
        if (rPatch.maPatchId.isEmpty())
        {
            rError = u"patch_id empty"_ustr;
            return false;
        }
        if (rPatch.maTarget.maParagraphId.isEmpty())
        {
            rError = u"target.paragraph_id empty"_ustr;
            return false;
        }
    }
    return true;
}

OString lcl_applyParagraphReplace(SwDoc& rDoc, const Patch& rPatch, OUString& rBeforeHash,
                                  OUString& rAfterHash, OUString& rUndoText, OUString& rRedoText)
{
    SwTextNode* pNode = lcl_resolveParagraphNode(rDoc, rPatch.maTarget.maParagraphId);
    if (!pNode)
        return kPatchStatusFailed;

    const OUString& rCurrent = pNode->GetText();
    rBeforeHash = ComputeParagraphTextHash(rCurrent);

    if (rPatch.maTarget.maTextHash && *rPatch.maTarget.maTextHash != rBeforeHash)
        return kPatchStatusFailed;

    if (!rPatch.maAfter)
        return kPatchStatusFailed;

    const OUString& rAfter = *rPatch.maAfter;
    rAfterHash = ComputeParagraphTextHash(rAfter);

    if (rCurrent == rAfter)
        return kPatchStatusSkippedIdempotent;

    if (rPatch.maBefore && *rPatch.maBefore != rCurrent)
        return kPatchStatusFailed;

    rUndoText = rCurrent;
    rRedoText = rAfter;

    SwContentIndex aIdx(pNode, 0);
    pNode->ReplaceText(aIdx, rCurrent.getLength(), rAfter);
    return kPatchStatusOk;
}

OString lcl_applyParagraphDelete(SwDoc& rDoc, const Patch& rPatch, OUString& rBeforeHash,
                                 OUString& rAfterHash, OUString& rUndoText, OUString& rRedoText)
{
    SwTextNode* pNode = lcl_resolveParagraphNode(rDoc, rPatch.maTarget.maParagraphId);
    if (!pNode)
        return kPatchStatusFailed;

    const OUString& rCurrent = pNode->GetText();
    rBeforeHash = ComputeParagraphTextHash(rCurrent);
    rAfterHash = ComputeParagraphTextHash(u""_ustr);

    // Non-empty paragraphs require mbForce (see Patch::mbForce).
    if (!rCurrent.isEmpty() && !rPatch.mbForce)
        return kPatchStatusFailed;

    if (rPatch.maTarget.maTextHash && *rPatch.maTarget.maTextHash != rBeforeHash)
        return kPatchStatusFailed;

    if (rPatch.maBefore && *rPatch.maBefore != rCurrent)
        return kPatchStatusFailed;

    // Idempotent: target already matches deleted (empty) state.
    if (rPatch.maTarget.maTextHash && *rPatch.maTarget.maTextHash == rAfterHash)
        return kPatchStatusSkippedIdempotent;

    rUndoText = rCurrent;
    rRedoText.clear();

    SwCursor aCursor(SwPosition(*pNode), nullptr);
    sw::UndoGuard aGuard(rDoc.GetIDocumentUndoRedo());
    if (!rDoc.getIDocumentContentOperations().DelFullPara(aCursor))
        return kPatchStatusFailed;

    return kPatchStatusOk;
}

OString lcl_applyTextRangeReplace(SwDoc& rDoc, const Patch& rPatch, OUString& rBeforeHash,
                                  OUString& rAfterHash, OUString& rUndoText, OUString& rRedoText)
{
    if (!rPatch.maRange || !rPatch.maBefore || !rPatch.maAfter)
        return kPatchStatusFailed;

    SwTextNode* pNode = lcl_resolveParagraphNode(rDoc, rPatch.maTarget.maParagraphId);
    if (!pNode)
        return kPatchStatusFailed;

    const OUString& rCurrent = pNode->GetText();
    rBeforeHash = ComputeParagraphTextHash(rCurrent);

    const OString aPrep = ValidateTextRangeReplacePatch(
        rCurrent, *rPatch.maRange, *rPatch.maBefore, *rPatch.maAfter, rPatch.maTarget.maTextHash);
    if (aPrep == kPatchStatusFailed)
        return kPatchStatusFailed;
    if (aPrep == kPatchStatusSkippedIdempotent)
    {
        const OUString aNewParagraph
            = rCurrent.replaceAt(rPatch.maRange->mnStart, rPatch.maRange->mnLength, *rPatch.maAfter);
        rAfterHash = ComputeParagraphTextHash(aNewParagraph);
        return kPatchStatusSkippedIdempotent;
    }

    const sal_Int32 nStart = rPatch.maRange->mnStart;
    const sal_Int32 nLength = rPatch.maRange->mnLength;
    const OUString& rAfter = *rPatch.maAfter;
    const OUString aNewParagraph = rCurrent.replaceAt(nStart, nLength, rAfter);
    rAfterHash = ComputeParagraphTextHash(aNewParagraph);

    rUndoText = rCurrent;
    rRedoText = aNewParagraph;

    SwContentIndex aIdx(pNode, nStart);
    pNode->ReplaceText(aIdx, nLength, rAfter);
    return kPatchStatusOk;
}

OString lcl_applyParagraphInsertAfter(SwDoc& rDoc, const Patch& rPatch, OUString& rBeforeHash,
                                      OUString& rAfterHash, OUString& rUndoText,
                                      OUString& rRedoText)
{
    SwTextNode* pAnchor = lcl_resolveParagraphNode(rDoc, rPatch.maTarget.maParagraphId);
    if (!pAnchor)
        return kPatchStatusFailed;

    const OUString& rAnchorText = pAnchor->GetText();
    rBeforeHash = ComputeParagraphTextHash(rAnchorText);

    if (rPatch.maTarget.maTextHash && *rPatch.maTarget.maTextHash != rBeforeHash)
        return kPatchStatusFailed;

    OUString aInsertText;
    if (rPatch.maAfter)
        aInsertText = *rPatch.maAfter;
    rAfterHash = ComputeParagraphTextHash(aInsertText);

    sal_uInt32 nAnchor = 0;
    if (!ParseParagraphId(rPatch.maTarget.maParagraphId, nAnchor))
        return kPatchStatusFailed;

    const SwTextNode* pNext = lcl_getBodyTextParagraph(rDoc, nAnchor + 1);
    if (pNext)
    {
        const OUString aNextHash = ComputeParagraphTextHash(pNext->GetText());
        if (aNextHash == rAfterHash)
            return kPatchStatusSkippedIdempotent;
        return kPatchStatusFailed;
    }

    if (rPatch.maBefore && *rPatch.maBefore != rAnchorText)
        return kPatchStatusFailed;

    rUndoText.clear();
    rRedoText = aInsertText;

    sw::UndoGuard aGuard(rDoc.GetIDocumentUndoRedo());
    SwPosition aPos(*pAnchor);
    aPos.AssignEndIndex(*pAnchor);
    if (!rDoc.getIDocumentContentOperations().AppendTextNode(aPos))
        return kPatchStatusFailed;

    SwTextNode* pNew = aPos.GetNode().GetTextNode();
    if (!pNew)
        return kPatchStatusFailed;

    if (!aInsertText.isEmpty())
    {
        SwContentIndex aIdx(pNew, 0);
        pNew->ReplaceText(aIdx, 0, aInsertText);
    }
    return kPatchStatusOk;
}

OUString lcl_paragraphStyleName(const SwTextNode* pNode)
{
    if (!pNode)
        return OUString();
    SwTextFormatColl* pColl = pNode->GetTextColl();
    return pColl ? pColl->GetName().toString() : OUString();
}

std::optional<ParagraphReformatAttrs> lcl_captureParagraphReformatAttrs(const SwTextNode& rNode)
{
    ParagraphReformatAttrs aAttrs;
    const SvxFirstLineIndentItem& rFirst = rNode.GetAttr(RES_MARGIN_FIRSTLINE);
    aAttrs.mnFirstLineIndentTwips = rFirst.ResolveTextFirstLineOffset({});

    const SvxLineSpacingItem& rSpace = rNode.GetAttr(RES_PARATR_LINESPACING);
    if (rSpace.GetLineSpaceRule() == SvxLineSpaceRule::Auto
        && rSpace.GetInterLineSpaceRule() == SvxInterLineSpaceRule::Prop)
    {
        aAttrs.mfLineSpacing = rSpace.GetPropLineSpace() / 100.0;
    }
    else
    {
        aAttrs.mfLineSpacing = 1.0;
    }
    return aAttrs;
}

OString lcl_applyParagraphReformat(SwDoc& rDoc, const Patch& rPatch, OUString& rBeforeHash,
                                   OUString& rAfterHash, OUString& rUndoText, OUString& rRedoText)
{
    if (!rPatch.maFormatChangesJson)
        return kPatchStatusFailed;

    const std::optional<ParagraphReformatAttrs> oTarget
        = ParseFormatChangesReformat(*rPatch.maFormatChangesJson);
    if (!oTarget)
        return kPatchStatusFailed;

    SwTextNode* pNode = lcl_resolveParagraphNode(rDoc, rPatch.maTarget.maParagraphId);
    if (!pNode)
        return kPatchStatusFailed;

    const std::optional<ParagraphReformatAttrs> oCurrent = lcl_captureParagraphReformatAttrs(*pNode);
    if (!oCurrent)
        return kPatchStatusFailed;

    const OUString aBeforeJson = SerializeFormatChangesReformat(*oCurrent);
    const OUString aAfterJson = SerializeFormatChangesReformat(*oTarget);

    rBeforeHash = ComputeParagraphTextHash(aBeforeJson);
    rAfterHash = ComputeParagraphTextHash(aAfterJson);

    if (aBeforeJson == aAfterJson)
        return kPatchStatusSkippedIdempotent;

    if (rPatch.maTarget.maTextHash)
    {
        const OUString aTextHash = ComputeParagraphTextHash(pNode->GetText());
        if (*rPatch.maTarget.maTextHash != aTextHash)
            return kPatchStatusFailed;
    }

    if (rPatch.maBefore && *rPatch.maBefore != aBeforeJson)
        return kPatchStatusFailed;

    if (!ApplyParagraphReformatAttrs(*pNode, *oTarget))
        return kPatchStatusFailed;

    rUndoText = aBeforeJson;
    rRedoText = aAfterJson;
    return kPatchStatusOk;
}

OString lcl_applyParagraphFormat(SwDoc& rDoc, const Patch& rPatch, OUString& rBeforeHash,
                                 OUString& rAfterHash, OUString& rUndoText, OUString& rRedoText)
{
    if (!rPatch.maFormatChangesJson)
        return kPatchStatusFailed;

    std::optional<OUString> oNewStyle = ParseFormatChangesStyle(*rPatch.maFormatChangesJson);
    if (!oNewStyle || oNewStyle->isEmpty())
        return kPatchStatusFailed;

    SwTextNode* pNode = lcl_resolveParagraphNode(rDoc, rPatch.maTarget.maParagraphId);
    if (!pNode)
        return kPatchStatusFailed;

    const OUString aCurrentStyle = lcl_paragraphStyleName(pNode);
    rBeforeHash = ComputeParagraphTextHash(aCurrentStyle);
    rAfterHash = ComputeParagraphTextHash(*oNewStyle);

    if (aCurrentStyle == *oNewStyle)
        return kPatchStatusSkippedIdempotent;

    if (rPatch.maTarget.maTextHash)
    {
        const OUString aTextHash = ComputeParagraphTextHash(pNode->GetText());
        if (*rPatch.maTarget.maTextHash != aTextHash)
            return kPatchStatusFailed;
    }

    if (rPatch.maBefore && *rPatch.maBefore != aCurrentStyle)
        return kPatchStatusFailed;

    SwTextFormatColl* pNewColl = rDoc.FindTextFormatCollByName(UIName(*oNewStyle));
    if (!pNewColl)
        return kPatchStatusFailed;

    rUndoText = aCurrentStyle;
    rRedoText = *oNewStyle;

    SwPaM aPam(*pNode);
    if (!rDoc.SetTextFormatColl(aPam, pNewColl, false))
        return kPatchStatusFailed;

    return kPatchStatusOk;
}

struct PatchUndoExtras
{
    sal_Int32 mnRangeStart = -1;
    sal_Int32 mnRangeLength = -1;
    sal_Int32 mnUndoWeight = -1;
    sal_Int32 mnRedoWeight = -1;
};

FontWeight lcl_getWeightAt(const SwTextNode& rNode, sal_Int32 nPos, const SwDoc& rDoc)
{
    SfxItemSetFixed<RES_CHRATR_WEIGHT, RES_CHRATR_WEIGHT> aSet(
        const_cast<SwAttrPool&>(rDoc.GetAttrPool()));
    rNode.GetParaAttr(aSet, nPos, nPos + 1, true, true);
    if (const SvxWeightItem* pItem = aSet.GetItemIfSet(RES_CHRATR_WEIGHT))
        return pItem->GetWeight();
    return WEIGHT_NORMAL;
}

bool lcl_applyTextFormatWeight(SwDoc& rDoc, SwTextNode& rNode, sal_Int32 nStart, sal_Int32 nLength,
                               FontWeight eWeight)
{
    if (nLength <= 0)
        return false;
    const sal_Int32 nEnd = nStart + nLength;
    SwPosition aMk(rNode, nStart);
    SwPosition aPt(rNode, nEnd);
    SwPaM aPam(aMk, aPt);
    const SvxWeightItem aItem(eWeight, RES_CHRATR_WEIGHT);
    return rDoc.getIDocumentContentOperations().InsertPoolItem(aPam, aItem);
}

OString lcl_applyTextFormat(SwDoc& rDoc, const Patch& rPatch, OUString& rBeforeHash,
                            OUString& rAfterHash, PatchUndoExtras& rUndo)
{
    if (!rPatch.maRange || !rPatch.maFormatChangesJson)
        return kPatchStatusFailed;

    const std::optional<bool> oBold = ParseFormatChangesBold(*rPatch.maFormatChangesJson);
    if (!oBold || !*oBold)
        return kPatchStatusFailed;

    SwTextNode* pNode = lcl_resolveParagraphNode(rDoc, rPatch.maTarget.maParagraphId);
    if (!pNode)
        return kPatchStatusFailed;

    const OUString& rCurrent = pNode->GetText();
    rBeforeHash = ComputeParagraphTextHash(rCurrent);
    rAfterHash = rBeforeHash;

    if (ValidateTextFormatPatch(rCurrent, *rPatch.maRange, *rPatch.maFormatChangesJson,
                                rPatch.maTarget.maTextHash)
        == kPatchStatusFailed)
        return kPatchStatusFailed;

    const sal_Int32 nStart = rPatch.maRange->mnStart;
    const sal_Int32 nLength = rPatch.maRange->mnLength;
    const FontWeight ePrior = lcl_getWeightAt(*pNode, nStart, rDoc);
    if (ePrior == WEIGHT_BOLD)
        return kPatchStatusSkippedIdempotent;

    if (!lcl_applyTextFormatWeight(rDoc, *pNode, nStart, nLength, WEIGHT_BOLD))
        return kPatchStatusFailed;

    rUndo.mnRangeStart = nStart;
    rUndo.mnRangeLength = nLength;
    rUndo.mnUndoWeight = static_cast<sal_Int32>(ePrior);
    rUndo.mnRedoWeight = static_cast<sal_Int32>(WEIGHT_BOLD);
    return kPatchStatusOk;
}

OString lcl_applyPatchSkeleton(SwDoc& /*rDoc*/, const Patch& rPatch, OUString& rBeforeHash,
                               OUString& rAfterHash)
{
    // Non-paragraph-replace kinds: undo stub only until W3 Day-1c/d/e/f.
    SAL_INFO("sw.intelligent", "apply patch skeleton for kind "
                                  << PatchKindToken(rPatch.meKind));
    rBeforeHash = u"sha256:skeleton-before"_ustr;
    rAfterHash = u"sha256:skeleton-after"_ustr;
    return kPatchStatusOk;
}

OString lcl_applyPatch(SwDoc& rDoc, const Patch& rPatch, OUString& rBeforeHash,
                       OUString& rAfterHash, OUString& rUndoText, OUString& rRedoText,
                       PatchUndoExtras& rUndoExtras)
{
    switch (rPatch.meKind)
    {
        case PatchKind::ParagraphReplace:
            return lcl_applyParagraphReplace(rDoc, rPatch, rBeforeHash, rAfterHash, rUndoText,
                                             rRedoText);
        case PatchKind::ParagraphDelete:
            return lcl_applyParagraphDelete(rDoc, rPatch, rBeforeHash, rAfterHash, rUndoText,
                                            rRedoText);
        case PatchKind::ParagraphInsertAfter:
            return lcl_applyParagraphInsertAfter(rDoc, rPatch, rBeforeHash, rAfterHash, rUndoText,
                                                 rRedoText);
        case PatchKind::TextRangeReplace:
            return lcl_applyTextRangeReplace(rDoc, rPatch, rBeforeHash, rAfterHash, rUndoText,
                                             rRedoText);
        case PatchKind::ParagraphFormat:
            return lcl_applyParagraphFormat(rDoc, rPatch, rBeforeHash, rAfterHash, rUndoText,
                                            rRedoText);
        case PatchKind::ParagraphReformat:
            return lcl_applyParagraphReformat(rDoc, rPatch, rBeforeHash, rAfterHash, rUndoText,
                                              rRedoText);
        case PatchKind::TextFormat:
            return lcl_applyTextFormat(rDoc, rPatch, rBeforeHash, rAfterHash, rUndoExtras);
        default:
            rUndoText.clear();
            rRedoText.clear();
            return lcl_applyPatchSkeleton(rDoc, rPatch, rBeforeHash, rAfterHash);
    }
}

} // namespace

namespace
{
std::optional<sal_Int32> lcl_parseJsonIntKey(std::u16string_view rJson, std::u16string_view rKey)
{
    const OUString aJson(rJson);
    const OUString aQuotedKey = OUString::Concat(u"\"") + OUString(rKey) + u"\"";
    const sal_Int32 nKeyPos = aJson.indexOf(aQuotedKey);
    if (nKeyPos < 0)
        return std::nullopt;
    sal_Int32 nPos = aJson.indexOf(':', nKeyPos + aQuotedKey.getLength());
    if (nPos < 0)
        return std::nullopt;
    ++nPos;
    while (nPos < aJson.getLength() && (aJson[nPos] == u' ' || aJson[nPos] == u'\t'))
        ++nPos;
    if (nPos >= aJson.getLength())
        return std::nullopt;

    bool bNegative = false;
    if (aJson[nPos] == u'-')
    {
        bNegative = true;
        ++nPos;
    }

    sal_Int64 nValue = 0;
    bool bAnyDigit = false;
    while (nPos < aJson.getLength() && aJson[nPos] >= u'0' && aJson[nPos] <= u'9')
    {
        bAnyDigit = true;
        nValue = nValue * 10 + (aJson[nPos] - u'0');
        ++nPos;
    }
    if (!bAnyDigit)
        return std::nullopt;
    if (bNegative)
        nValue = -nValue;
    return static_cast<sal_Int32>(nValue);
}

std::optional<bool> lcl_parseJsonBoolKey(std::u16string_view rJson, std::u16string_view rKey)
{
    const OUString aJson(rJson);
    const OUString aQuotedKey = OUString::Concat(u"\"") + OUString(rKey) + u"\"";
    const sal_Int32 nKeyPos = aJson.indexOf(aQuotedKey);
    if (nKeyPos < 0)
        return std::nullopt;
    sal_Int32 nPos = aJson.indexOf(':', nKeyPos + aQuotedKey.getLength());
    if (nPos < 0)
        return std::nullopt;
    ++nPos;
    while (nPos < aJson.getLength() && (aJson[nPos] == u' ' || aJson[nPos] == u'\t'))
        ++nPos;
    if (nPos >= aJson.getLength())
        return std::nullopt;

    const OUString aRemainder = aJson.copy(nPos);
    if (aRemainder.startsWithIgnoreAsciiCase("true"))
        return true;
    if (aRemainder.startsWithIgnoreAsciiCase("false"))
        return false;
    return std::nullopt;
}

std::optional<double> lcl_parseJsonDoubleKey(std::u16string_view rJson, std::u16string_view rKey)
{
    const OUString aJson(rJson);
    const OUString aQuotedKey = OUString::Concat(u"\"") + OUString(rKey) + u"\"";
    const sal_Int32 nKeyPos = aJson.indexOf(aQuotedKey);
    if (nKeyPos < 0)
        return std::nullopt;
    sal_Int32 nPos = aJson.indexOf(':', nKeyPos + aQuotedKey.getLength());
    if (nPos < 0)
        return std::nullopt;
    ++nPos;
    while (nPos < aJson.getLength() && (aJson[nPos] == u' ' || aJson[nPos] == u'\t'))
        ++nPos;
    if (nPos >= aJson.getLength())
        return std::nullopt;

    const OString aNumber = OUStringToOString(aJson.subView(nPos), RTL_TEXTENCODING_UTF8);
    rtl_math_ConversionStatus eStatus = rtl_math_ConversionStatus_Ok;
    char const* pParsedEnd = nullptr;
    const double fValue = rtl_math_stringToDouble(
        aNumber.getStr(), aNumber.getStr() + aNumber.getLength(), '.', ',', &eStatus, &pParsedEnd);
    if (eStatus != rtl_math_ConversionStatus_Ok || !pParsedEnd || pParsedEnd == aNumber.getStr())
        return std::nullopt;
    return fValue;
}
} // namespace

std::optional<ParagraphReformatAttrs> ParseFormatChangesReformat(std::u16string_view rJson)
{
    const std::optional<sal_Int32> oIndent = lcl_parseJsonIntKey(rJson, u"first_line_indent");
    if (!oIndent)
        return std::nullopt;

    ParagraphReformatAttrs aAttrs;
    aAttrs.mnFirstLineIndentTwips = *oIndent;
    if (const std::optional<double> oSpacing = lcl_parseJsonDoubleKey(rJson, u"line_spacing"))
        aAttrs.mfLineSpacing = *oSpacing;
    return aAttrs;
}

OUString SerializeFormatChangesReformat(const ParagraphReformatAttrs& rAttrs)
{
    OUStringBuffer aJson(u"{"_ustr);
    aJson.append(u"\"first_line_indent\": "_ustr);
    aJson.append(OUString::number(rAttrs.mnFirstLineIndentTwips));
    if (rAttrs.mfLineSpacing)
    {
        aJson.append(u", \"line_spacing\": "_ustr);
        aJson.append(OUString::number(*rAttrs.mfLineSpacing));
    }
    aJson.append(u"}"_ustr);
    return aJson.makeStringAndClear();
}

bool ApplyParagraphReformatAttrs(SwTextNode& rNode, const ParagraphReformatAttrs& rAttrs)
{
    SvxFirstLineIndentItem aFirstLine(SvxIndentValue::twips(rAttrs.mnFirstLineIndentTwips),
                                      RES_MARGIN_FIRSTLINE);
    aFirstLine.SetAutoFirst(false);
    if (!rNode.SetAttr(aFirstLine))
        return false;

    if (rAttrs.mfLineSpacing)
    {
        const double fSpacing = *rAttrs.mfLineSpacing;
        SvxLineSpacingItem aLineSpace(LINE_SPACE_DEFAULT_HEIGHT, RES_PARATR_LINESPACING);
        aLineSpace.SetLineSpaceRule(SvxLineSpaceRule::Auto);
        if (fSpacing <= 1.0)
        {
            aLineSpace.SetInterLineSpaceRule(SvxInterLineSpaceRule::Off);
            aLineSpace.SetPropLineSpace(100);
        }
        else
        {
            aLineSpace.SetPropLineSpace(
                static_cast<sal_uInt16>(std::lround(fSpacing * 100.0)));
        }
        if (!rNode.SetAttr(aLineSpace))
            return false;
    }
    return true;
}

std::optional<bool> ParseFormatChangesBold(std::u16string_view rJson)
{
    return lcl_parseJsonBoolKey(rJson, u"bold");
}

OString ValidateTextFormatPatch(const std::u16string_view rParagraphText,
                              const PatchRange& rRange, const std::u16string_view rFormatChangesJson,
                              const std::optional<OUString>& rTargetTextHash)
{
    const std::optional<bool> oBold = ParseFormatChangesBold(rFormatChangesJson);
    if (!oBold || !*oBold)
        return OString("failed");

    const sal_Int32 nLen = static_cast<sal_Int32>(rParagraphText.length());
    if (rRange.mnStart < 0 || rRange.mnLength < 0 || rRange.mnStart > nLen
        || rRange.mnStart + rRange.mnLength > nLen)
        return OString("failed");

    if (rTargetTextHash)
    {
        const OUString aHash = ComputeParagraphTextHash(rParagraphText);
        if (*rTargetTextHash != aHash)
            return OString("failed");
    }

    return OString("ok");
}

std::optional<OUString> ParseFormatChangesStyle(std::u16string_view rJson)
{
    const OUString aJson(rJson);
    const sal_Int32 nStyleKey = aJson.indexOf(u"\"style\""_ustr);
    if (nStyleKey < 0)
        return std::nullopt;
    const sal_Int32 nColon = aJson.indexOf(':', nStyleKey + 6);
    if (nColon < 0)
        return std::nullopt;
    const sal_Int32 nOpen = aJson.indexOf('\"', nColon + 1);
    if (nOpen < 0)
        return std::nullopt;
    const sal_Int32 nClose = aJson.indexOf('\"', nOpen + 1);
    if (nClose < 0 || nClose <= nOpen)
        return std::nullopt;
    return aJson.copy(nOpen + 1, nClose - nOpen - 1);
}

OString ValidateTextRangeReplacePatch(const std::u16string_view rParagraphText,
                                      const PatchRange& rRange, const std::u16string_view rBefore,
                                      const std::u16string_view rAfter,
                                      const std::optional<OUString>& rTargetTextHash)
{
    const sal_Int32 nLen = static_cast<sal_Int32>(rParagraphText.length());
    if (rRange.mnStart < 0 || rRange.mnLength < 0 || rRange.mnStart > nLen
        || rRange.mnStart + rRange.mnLength > nLen)
        return OString("failed");

    if (rBefore.length() != static_cast<size_t>(rRange.mnLength))
        return OString("failed");

    const std::u16string_view aSlice
        = rParagraphText.substr(static_cast<size_t>(rRange.mnStart),
                                static_cast<size_t>(rRange.mnLength));
    if (aSlice != rBefore)
        return OString("failed");

    const OUString aCurrent(rParagraphText.data(), rParagraphText.length());
    const OUString aAfter(rAfter.data(), rAfter.length());
    const OUString aNewParagraph = aCurrent.replaceAt(rRange.mnStart, rRange.mnLength, aAfter);
    const OUString aBeforeHash = ComputeParagraphTextHash(rParagraphText);
    const OUString aAfterHash = ComputeParagraphTextHash(aNewParagraph);

    if (rTargetTextHash)
    {
        if (*rTargetTextHash != aBeforeHash && *rTargetTextHash != aAfterHash)
            return OString("failed");
        if (*rTargetTextHash == aAfterHash)
            return OString("skipped-idempotent");
    }

    if (aCurrent == aNewParagraph)
        return OString("skipped-idempotent");

    return OString("ok");
}

OUString ComputeParagraphTextHash(std::u16string_view rText)
{
    if (rText.empty())
        return u"sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"_ustr;
    const std::vector<unsigned char> aDigest = comphelper::Hash::calculateHash(
        rText.data(), rText.size() * sizeof(char16_t), comphelper::HashType::SHA256);
    return u"sha256:"_ustr
           + OUString::createFromAscii(comphelper::hashToString(aDigest).c_str());
}

OUString ComputeDocSnapshotHash(const SwDocShell& rDocShell)
{
    const SwDoc* pDoc = rDocShell.GetDoc();
    if (!pDoc)
        return lcl_sha256Prefixed("empty-doc"_ostr);

    OStringBuffer aPayload;
    const sal_uInt32 nParagraphCount = lcl_countBodyTextParagraphs(*pDoc);
    aPayload.append("pc=" + OString::number(nParagraphCount));

    if (const SwTextNode* pFirst = lcl_getBodyTextParagraph(*pDoc, 1))
    {
        const OUString aFpHash = ComputeParagraphTextHash(pFirst->GetText());
        aPayload.append(";fp=" + aFpHash.toUtf8());
    }

    if (SfxMedium* pMedium = rDocShell.GetMedium())
        aPayload.append(";url=" + pMedium->GetName().toUtf8());
    else
        aPayload.append(";url="_ostr);

    aPayload.append(rDocShell.IsModified() ? ";mod=1"_ostr : ";mod=0"_ostr);
    return lcl_sha256Prefixed(aPayload.makeStringAndClear());
}

OString PatchKindToken(PatchKind eKind)
{
    switch (eKind)
    {
        case PatchKind::ParagraphReplace:     return kKindParagraphReplace;
        case PatchKind::ParagraphInsertAfter: return kKindParagraphInsertAfter;
        case PatchKind::ParagraphDelete:      return kKindParagraphDelete;
        case PatchKind::ParagraphFormat:      return kKindParagraphFormat;
        case PatchKind::ParagraphReformat:    return kKindParagraphReformat;
        case PatchKind::TextRangeReplace:     return kKindTextRangeReplace;
        case PatchKind::TextFormat:           return kKindTextFormat;
    }
    return OString();
}

std::optional<PatchKind> PatchKindFromToken(std::string_view aToken)
{
    if (aToken == kKindParagraphReplace)     return PatchKind::ParagraphReplace;
    if (aToken == kKindParagraphInsertAfter) return PatchKind::ParagraphInsertAfter;
    if (aToken == kKindParagraphDelete)      return PatchKind::ParagraphDelete;
    if (aToken == kKindParagraphFormat)      return PatchKind::ParagraphFormat;
    if (aToken == kKindParagraphReformat)    return PatchKind::ParagraphReformat;
    if (aToken == kKindTextRangeReplace)     return PatchKind::TextRangeReplace;
    if (aToken == kKindTextFormat)           return PatchKind::TextFormat;
    return std::nullopt;
}

OString ApplyStatusToken(ApplyStatus eStatus)
{
    switch (eStatus)
    {
        case ApplyStatus::Ok:               return kStatusOk;
        case ApplyStatus::ValidationFailed: return kStatusValidationFailed;
        case ApplyStatus::StaleSnapshot:    return kStatusStaleSnapshot;
        case ApplyStatus::PatchFailed:      return kStatusPatchFailed;
        case ApplyStatus::UndoException:    return kStatusUndoException;
    }
    return OString();
}

std::optional<ApplyStatus> ApplyStatusFromToken(std::string_view aToken)
{
    if (aToken == kStatusOk)                return ApplyStatus::Ok;
    if (aToken == kStatusValidationFailed)  return ApplyStatus::ValidationFailed;
    if (aToken == kStatusStaleSnapshot)     return ApplyStatus::StaleSnapshot;
    if (aToken == kStatusPatchFailed)       return ApplyStatus::PatchFailed;
    if (aToken == kStatusUndoException)     return ApplyStatus::UndoException;
    return std::nullopt;
}

bool ValidateApplyPlanShape(const ApplyPlan& rPlan, OUString& rError)
{
    return lcl_validatePlanShape(rPlan, rError);
}

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

std::optional<OUString> lcl_extractJsonStringField(const OUString& rJson,
                                                   std::u16string_view rKey)
{
    OUStringBuffer aNeedle(20);
    aNeedle.append('"');
    aNeedle.append(rKey.data(), rKey.size());
    aNeedle.append("\":\"");
    if (auto oVal = lcl_extractJsonStringFieldFromNeedle(rJson, aNeedle.makeStringAndClear()))
        return oVal;

    aNeedle.setLength(0);
    aNeedle.append('"');
    aNeedle.append(rKey.data(), rKey.size());
    aNeedle.append("\": \"");
    return lcl_extractJsonStringFieldFromNeedle(rJson, aNeedle.makeStringAndClear());
}

std::optional<OUString> GetBodyParagraphText(const SwDocShell& rDocShell,
                                             const OUString& rParagraphId)
{
    sal_uInt32 nParagraph = 0;
    if (!ParseParagraphId(rParagraphId, nParagraph))
        return std::nullopt;
    const SwDoc* pDoc = rDocShell.GetDoc();
    if (!pDoc)
        return std::nullopt;
    const SwTextNode* pNode = lcl_getBodyTextParagraph(*pDoc, nParagraph);
    if (!pNode)
        return std::nullopt;
    return pNode->GetText();
}

std::optional<ApplyPlan> lcl_buildSingleParagraphReplacePlan(SwDocShell& rDocShell,
                                                             const OUString& rPlanId,
                                                             const OUString& rParagraphId,
                                                             const OUString& rAfterText,
                                                             const OUString& rSourceDiagnosticId,
                                                             const std::optional<OUString>& rBefore)
{
    std::optional<OUString> oBefore = rBefore;
    if (!oBefore)
        oBefore = GetBodyParagraphText(rDocShell, rParagraphId);
    if (!oBefore)
        return std::nullopt;

    ApplyPlan aPlan;
    aPlan.maSchemaVersion = OUString::fromUtf8(kSchemaVersionConst);
    aPlan.maPlanId = rPlanId;
    aPlan.maSourceDiagnosticId = rSourceDiagnosticId;
    aPlan.maDocSnapshotHash = ComputeDocSnapshotHash(rDocShell);
    aPlan.mbPreviewOnly = false;

    Patch aPatch;
    aPatch.maPatchId = u"p1"_ustr;
    aPatch.meKind = PatchKind::ParagraphReplace;
    aPatch.maTarget.maParagraphId = rParagraphId;
    aPatch.maTarget.maTextHash = ComputeParagraphTextHash(*oBefore);
    aPatch.maSeverity = u"minor"_ustr;
    aPatch.maRationale = u"inline provider paragraph-replace"_ustr;
    aPatch.maBefore = *oBefore;
    aPatch.maAfter = rAfterText;

    aPlan.maPatches.push_back(aPatch);

    OUString aError;
    if (!lcl_validatePlanShape(aPlan, aError))
        return std::nullopt;
    return aPlan;
}

std::optional<ApplyPlan>
BuildSingleParagraphReplacePlan(SwDocShell& rDocShell, const OUString& rPlanId,
                                const OUString& rParagraphId, const OUString& rAfterText,
                                const OUString& rSourceDiagnosticId)
{
    return lcl_buildSingleParagraphReplacePlan(rDocShell, rPlanId, rParagraphId, rAfterText,
                                               rSourceDiagnosticId, std::nullopt);
}

namespace
{
constexpr sal_uInt32 kMaxRuntimePatches = 32;

sal_Int32 lcl_findMatchingJsonBrace(const OUString& rJson, sal_Int32 nOpenBrace)
{
    if (nOpenBrace < 0 || nOpenBrace >= rJson.getLength() || rJson[nOpenBrace] != u'{')
        return -1;
    sal_Int32 nDepth = 0;
    bool bInString = false;
    for (sal_Int32 i = nOpenBrace; i < rJson.getLength(); ++i)
    {
        const sal_Unicode c = rJson[i];
        if (bInString)
        {
            if (c == u'\\' && i + 1 < rJson.getLength())
            {
                ++i;
                continue;
            }
            if (c == u'"')
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

std::optional<OUString> lcl_extractJsonObjectField(const OUString& rJson,
                                                   std::u16string_view rKey)
{
    OUStringBuffer aNeedle(24);
    aNeedle.append('"');
    aNeedle.append(rKey.data(), rKey.size());
    aNeedle.append("\":");
    sal_Int32 nKeyPos = rJson.indexOf(aNeedle.makeStringAndClear());
    if (nKeyPos < 0)
    {
        aNeedle.append('"');
        aNeedle.append(rKey.data(), rKey.size());
        aNeedle.append("\": \"");
        nKeyPos = rJson.indexOf(aNeedle.makeStringAndClear());
    }
    if (nKeyPos < 0)
        return std::nullopt;

    sal_Int32 nPos = rJson.indexOf(':', nKeyPos);
    if (nPos < 0)
        return std::nullopt;
    ++nPos;
    while (nPos < rJson.getLength() && (rJson[nPos] == u' ' || rJson[nPos] == u'\t'))
        ++nPos;
    if (nPos >= rJson.getLength() || rJson[nPos] != u'{')
        return std::nullopt;

    const sal_Int32 nClose = lcl_findMatchingJsonBrace(rJson, nPos);
    if (nClose < 0)
        return std::nullopt;
    return rJson.copy(nPos, nClose - nPos + 1);
}

std::optional<Patch> lcl_parsePatchObject(const OUString& rPatchJson)
{
    const std::optional<OUString> oPatchId = lcl_extractJsonStringField(rPatchJson, u"patch_id");
    const std::optional<OUString> oKind = lcl_extractJsonStringField(rPatchJson, u"kind");
    const std::optional<OUString> oParagraphId
        = lcl_extractJsonStringField(rPatchJson, u"paragraph_id");
    if (!oPatchId || !oKind || !oParagraphId)
        return std::nullopt;

    const OString aKindUtf8 = OUStringToOString(*oKind, RTL_TEXTENCODING_UTF8);
    const std::optional<PatchKind> oPatchKind = PatchKindFromToken(aKindUtf8);
    if (!oPatchKind)
        return std::nullopt;

    Patch aPatch;
    aPatch.maPatchId = *oPatchId;
    aPatch.meKind = *oPatchKind;
    aPatch.maTarget.maParagraphId = *oParagraphId;

    if (const std::optional<OUString> oTextHash
        = lcl_extractJsonStringField(rPatchJson, u"text_hash"))
        aPatch.maTarget.maTextHash = *oTextHash;

    if (const std::optional<OUString> oSeverity
        = lcl_extractJsonStringField(rPatchJson, u"severity"))
        aPatch.maSeverity = *oSeverity;
    if (const std::optional<OUString> oRationale
        = lcl_extractJsonStringField(rPatchJson, u"rationale"))
        aPatch.maRationale = *oRationale;
    if (const std::optional<OUString> oBefore = lcl_extractJsonStringField(rPatchJson, u"before"))
        aPatch.maBefore = *oBefore;
    if (const std::optional<OUString> oAfter = lcl_extractJsonStringField(rPatchJson, u"after"))
        aPatch.maAfter = *oAfter;

    if (rPatchJson.indexOf(u"\"range\""_ustr) >= 0)
    {
        PatchRange aRange;
        if (const std::optional<sal_Int32> oStart = lcl_parseJsonIntKey(rPatchJson, u"start"))
            aRange.mnStart = *oStart;
        if (const std::optional<sal_Int32> oLength = lcl_parseJsonIntKey(rPatchJson, u"length"))
            aRange.mnLength = *oLength;
        aPatch.maRange = aRange;
    }

    if (const std::optional<OUString> oFormatChanges
        = lcl_extractJsonObjectField(rPatchJson, u"format_changes"))
        aPatch.maFormatChangesJson = *oFormatChanges;

    if (const std::optional<bool> oForce = lcl_parseJsonBoolKey(rPatchJson, u"force"))
        aPatch.mbForce = *oForce;

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

    if (rJsonBody.indexOf(kPatchIdKey, nPos) >= 0)
        return std::nullopt;

    return aPatches;
}

std::optional<ApplyPlan> lcl_parseApplyPlanEnvelope(const OUString& rJsonBody)
{
    const std::optional<OUString> oSchema = lcl_extractJsonStringField(rJsonBody, u"schema_version");
    if (!oSchema || *oSchema != OUString::fromUtf8(kSchemaVersionConst))
        return std::nullopt;

    const std::optional<std::vector<Patch>> oPatches = lcl_parsePatchesArray(rJsonBody);
    if (!oPatches)
        return std::nullopt;

    ApplyPlan aPlan;
    aPlan.maSchemaVersion = *oSchema;

    const std::optional<OUString> oPlanId = lcl_extractJsonStringField(rJsonBody, u"plan_id");
    const std::optional<OUString> oSourceDiagId
        = lcl_extractJsonStringField(rJsonBody, u"source_diagnostic_id");
    if (!oPlanId || !oSourceDiagId)
        return std::nullopt;
    aPlan.maPlanId = *oPlanId;
    aPlan.maSourceDiagnosticId = *oSourceDiagId;

    if (const std::optional<OUString> oSnapshotHash
        = lcl_extractJsonStringField(rJsonBody, u"doc_snapshot_hash"))
        aPlan.maDocSnapshotHash = *oSnapshotHash;

    if (const std::optional<bool> oPreview = lcl_parseJsonBoolKey(rJsonBody, u"preview_only"))
        aPlan.mbPreviewOnly = *oPreview;

    aPlan.maPatches = *oPatches;

    OUString aError;
    if (!lcl_validatePlanShape(aPlan, aError))
        return std::nullopt;
    return aPlan;
}

std::optional<ApplyPlan> lcl_tryParseInlineFlatParagraphReplace(const OUString& rJsonBody,
                                                                SwDocShell& rDocShell)
{
    const std::optional<OUString> oSchema = lcl_extractJsonStringField(rJsonBody, u"schema_version");
    if (!oSchema || *oSchema != OUString::fromUtf8(kSchemaVersionConst))
        return std::nullopt;

    const std::optional<OUString> oKind = lcl_extractJsonStringField(rJsonBody, u"kind");
    if (!oKind || *oKind != u"paragraph-replace"_ustr)
        return std::nullopt;

    const std::optional<OUString> oParagraphId
        = lcl_extractJsonStringField(rJsonBody, u"paragraph_id");
    const std::optional<OUString> oAfter = lcl_extractJsonStringField(rJsonBody, u"after");
    if (!oParagraphId || !oAfter)
        return std::nullopt;

    OUString aPlanId = lcl_extractJsonStringField(rJsonBody, u"plan_id").value_or(OUString());
    if (aPlanId.isEmpty())
        aPlanId = u"ap-inline-runtime"_ustr;

    OUString aSourceDiagId
        = lcl_extractJsonStringField(rJsonBody, u"source_diagnostic_id").value_or(OUString());
    if (aSourceDiagId.isEmpty())
        aSourceDiagId = u"diag-inline-runtime"_ustr;

    const std::optional<OUString> oBefore = lcl_extractJsonStringField(rJsonBody, u"before");
    return lcl_buildSingleParagraphReplacePlan(rDocShell, aPlanId, *oParagraphId, *oAfter,
                                               aSourceDiagId, oBefore);
}
} // namespace

std::optional<ApplyPlan> ParseApplyPlanRuntimeJson(const OUString& rJsonBody)
{
    return lcl_parseApplyPlanEnvelope(rJsonBody);
}

std::optional<ApplyPlan> TryParseApplyPlanRuntimeJson(const OUString& rJsonBody,
                                                      SwDocShell& rDocShell)
{
    std::optional<ApplyPlan> oPlan = lcl_parseApplyPlanEnvelope(rJsonBody);
    if (!oPlan)
        oPlan = lcl_tryParseInlineFlatParagraphReplace(rJsonBody, rDocShell);
    if (!oPlan)
        return std::nullopt;
    oPlan->maDocSnapshotHash = ComputeDocSnapshotHash(rDocShell);
    return oPlan;
}

ApplyEngine::ApplyEngine(SwDocShell& rDocShell)
    : mrDocShell(rDocShell)
{
}

ApplyEngine::~ApplyEngine() {}

ApplyResult ApplyEngine::run(const ApplyPlan& rPlan)
{
    ApplyResult aResult;

    // Step 1 — validate
    OUString aValidationError;
    if (!lcl_validatePlanShape(rPlan, aValidationError))
    {
        aResult.meStatus = ApplyStatus::ValidationFailed;
        aResult.maStatusToken = OUString::fromUtf8(ApplyStatusToken(aResult.meStatus));
        return aResult;
    }

    const OUString aCurrentHash = ComputeDocSnapshotHash(mrDocShell);
    if (rPlan.maDocSnapshotHash != aCurrentHash)
    {
        aResult.meStatus = ApplyStatus::StaleSnapshot;
        aResult.maStatusToken = OUString::fromUtf8(ApplyStatusToken(aResult.meStatus));
        return aResult;
    }

    SwDoc* pDoc = mrDocShell.GetDoc();
    if (!pDoc)
    {
        aResult.meStatus = ApplyStatus::UndoException;
        aResult.maStatusToken = OUString::fromUtf8(ApplyStatusToken(aResult.meStatus));
        return aResult;
    }

    // Step 2 — per-patch undo groups (W3/W4 Day-4): each successful patch is its own
    // Sfx list action so DiffReview Accept can SfxUndoManager::Undo() once per patch.
    IDocumentUndoRedo& rUndo = pDoc->GetIDocumentUndoRedo();

    // Step 3 — for each patch
    sal_Int32 nApplied = 0;
    bool bRolledBack = false;
    OUString aFailedPatchId;
    for (const Patch& rPatch : rPlan.maPatches)
    {
        OUString aBeforeHash;
        OUString aAfterHash;
        OUString aUndoText;
        OUString aRedoText;
        PatchUndoExtras aUndoExtras;
        OString aPatchStatus = lcl_applyPatch(*pDoc, rPatch, aBeforeHash, aAfterHash, aUndoText, aRedoText,
                                            aUndoExtras);

        PatchResult aPatchResult;
        aPatchResult.maPatchId = rPatch.maPatchId;
        aPatchResult.maStatus = OUString::fromUtf8(aPatchStatus);
        aPatchResult.maBeforeHash = aBeforeHash;
        aPatchResult.maAfterHash = aAfterHash;
        aResult.maPatchResults.push_back(aPatchResult);

        if (aPatchStatus == kPatchStatusOk)
        {
            rUndo.StartUndo(SwUndoId::EMPTY, nullptr);
            std::unique_ptr<SwUndo> pUndo(new sw::UndoApplyPatch(
                *pDoc, rPatch.maPatchId, OUString::fromUtf8(PatchKindToken(rPatch.meKind)),
                aBeforeHash, aAfterHash, rPatch.maTarget.maParagraphId, aUndoText, aRedoText,
                aUndoExtras.mnRangeStart, aUndoExtras.mnRangeLength, aUndoExtras.mnUndoWeight,
                aUndoExtras.mnRedoWeight));
            rUndo.AppendUndo(std::move(pUndo));
            rUndo.EndUndo(SwUndoId::EMPTY, nullptr);
            SAL_INFO("sw.intelligent",
                     "ApplyEngine: per-patch undo group closed patch_id=" << rPatch.maPatchId
                                                                           << " kind="
                                                                           << PatchKindToken(rPatch.meKind));
            ++nApplied;
        }
        else if (aPatchStatus == kPatchStatusSkippedIdempotent)
        {
            // No undo entry — document unchanged.
        }
        else
        {
            aFailedPatchId = rPatch.maPatchId;
            bRolledBack = true;
            break;
        }
    }

    if (bRolledBack)
    {
        aResult.meStatus = ApplyStatus::PatchFailed;
        aResult.maStatusToken = OUString::fromUtf8(ApplyStatusToken(aResult.meStatus));
        aResult.maFailedPatchId = aFailedPatchId;
        return aResult;
    }

    aResult.mnAppliedCount = nApplied;

    // Step 5 — EvidenceRecorder.write (skeleton: synthesize an evidence id)
    aResult.maEvidenceId = u"ev-skeleton-"_ustr + rPlan.maPlanId;
    aResult.maAfterSnapshotHash = ComputeDocSnapshotHash(mrDocShell);

    // Step 6 — Ok (Diff Review UI is opened from SwDocShell::applyDiagnosticsPlan
    // so sw/core does not depend on uibase/ai).
    aResult.meStatus = ApplyStatus::Ok;
    aResult.maStatusToken = OUString::fromUtf8(ApplyStatusToken(aResult.meStatus));
    return aResult;
}

} // namespace sw::intelligent

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
