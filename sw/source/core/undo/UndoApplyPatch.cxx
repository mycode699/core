/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <UndoApplyPatch.hxx>

#include <IntelligentWriterApplyEngine.hxx>
#include <doc.hxx>
#include <format.hxx>
#include <IDocumentContentOperations.hxx>
#include <names.hxx>
#include <IDocumentUndoRedo.hxx>
#include <ndarr.hxx>
#include <ndtxt.hxx>
#include <node.hxx>
#include <pam.hxx>
#include <swcrsr.hxx>

#include <editeng/fontitem.hxx>
#include <editeng/wghtitem.hxx>
#include <hintids.hxx>

#include <sal/log.hxx>

#include <vcl/font.hxx>

namespace sw
{
namespace
{
constexpr OUString kParagraphIdPrefix = u"swpara-"_ustr;

bool lcl_parseParagraphIndex(const OUString& rParagraphId, sal_uInt32& rnParagraph)
{
    if (!rParagraphId.startsWith(kParagraphIdPrefix))
        return false;
    const sal_Int32 nIdx = rParagraphId.copy(kParagraphIdPrefix.getLength()).toInt32();
    if (nIdx < 1)
        return false;
    rnParagraph = static_cast<sal_uInt32>(nIdx);
    return true;
}

SwTextNode* lcl_resolveParagraphNode(SwDoc& rDoc, const OUString& rParagraphId)
{
    sal_uInt32 nParagraph = 0;
    if (!lcl_parseParagraphIndex(rParagraphId, nParagraph))
        return nullptr;
    sal_uInt32 nSeen = 0;
    const SwNodes& rNodes = rDoc.GetNodes();
    for (SwNodeOffset nNode(0); nNode < rNodes.Count(); ++nNode)
    {
        SwTextNode* pTextNode = rNodes[nNode]->GetTextNode();
        if (!pTextNode)
            continue;
        ++nSeen;
        if (nSeen == nParagraph)
            return pTextNode;
    }
    return nullptr;
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

bool lcl_insertDeletedParagraph(SwDoc& rDoc, const OUString& rParagraphId, const OUString& rText)
{
    sal_uInt32 nParagraph = 0;
    if (!lcl_parseParagraphIndex(rParagraphId, nParagraph))
        return false;

    sw::UndoGuard aGuard(rDoc.GetIDocumentUndoRedo());

    SwTextNode* pNew = nullptr;
    if (nParagraph == 1)
    {
        const SwTextNode* pFirst = lcl_getBodyTextParagraph(rDoc, 1);
        if (!pFirst)
            return false;
        SwNodeIndex aIdx(rDoc.GetNodes(), pFirst->GetIndex());
        pNew = rDoc.GetNodes().MakeTextNode(aIdx.GetNode(), rDoc.GetDfltTextFormatColl());
    }
    else
    {
        const SwTextNode* pBefore = lcl_getBodyTextParagraph(rDoc, nParagraph - 1);
        if (!pBefore)
            return false;
        SwPosition aPos(*pBefore);
        aPos.AssignEndIndex(*pBefore);
        if (!rDoc.getIDocumentContentOperations().AppendTextNode(aPos))
            return false;
        pNew = aPos.GetNode().GetTextNode();
    }

    if (!pNew)
        return false;
    if (!rText.isEmpty())
    {
        SwContentIndex aIdx(pNew, 0);
        pNew->ReplaceText(aIdx, 0, rText);
    }
    return true;
}

bool lcl_deleteParagraph(SwDoc& rDoc, const OUString& rParagraphId)
{
    SwTextNode* pNode = lcl_resolveParagraphNode(rDoc, rParagraphId);
    if (!pNode)
        return false;
    SwCursor aCursor(SwPosition(*pNode), nullptr);
    sw::UndoGuard aGuard(rDoc.GetIDocumentUndoRedo());
    return rDoc.getIDocumentContentOperations().DelFullPara(aCursor);
}

bool lcl_insertAfterParagraph(SwDoc& rDoc, const OUString& rAnchorParagraphId,
                              const OUString& rText)
{
    SwTextNode* pAnchor = lcl_resolveParagraphNode(rDoc, rAnchorParagraphId);
    if (!pAnchor)
        return false;

    sw::UndoGuard aGuard(rDoc.GetIDocumentUndoRedo());

    SwPosition aPos(*pAnchor);
    aPos.AssignEndIndex(*pAnchor);
    if (!rDoc.getIDocumentContentOperations().AppendTextNode(aPos))
        return false;

    SwTextNode* pNew = aPos.GetNode().GetTextNode();
    if (!pNew)
        return false;
    if (!rText.isEmpty())
    {
        SwContentIndex aIdx(pNew, 0);
        pNew->ReplaceText(aIdx, 0, rText);
    }
    return true;
}

bool lcl_deleteParagraphAfterAnchor(SwDoc& rDoc, const OUString& rAnchorParagraphId)
{
    sal_uInt32 nAnchor = 0;
    if (!lcl_parseParagraphIndex(rAnchorParagraphId, nAnchor))
        return false;
    const OUString aInsertedId = kParagraphIdPrefix + OUString::number(nAnchor + 1);
    return lcl_deleteParagraph(rDoc, aInsertedId);
}

bool lcl_applyTextFormatWeight(SwDoc& rDoc, SwTextNode& rNode, sal_Int32 nStart, sal_Int32 nLength,
                               FontWeight eWeight)
{
    if (nLength <= 0 || nStart < 0)
        return false;
    SwPosition aMk(rNode, nStart);
    SwPosition aPt(rNode, nStart + nLength);
    SwPaM aPam(aMk, aPt);
    const SvxWeightItem aItem(eWeight, RES_CHRATR_WEIGHT);
    return rDoc.getIDocumentContentOperations().InsertPoolItem(aPam, aItem);
}

} // namespace

UndoApplyPatch::UndoApplyPatch(SwDoc& rDoc, const OUString& rPatchId, const OUString& rKindToken,
                               const OUString& rBeforeHash, const OUString& rAfterHash,
                               const OUString& rParagraphId, const OUString& rUndoText,
                               const OUString& rRedoText, sal_Int32 nRangeStart,
                               sal_Int32 nRangeLength, sal_Int32 nUndoWeight,
                               sal_Int32 nRedoWeight)
    : SwUndo(SwUndoId::EMPTY, rDoc)
    , mrDoc(rDoc)
    , maPatchId(rPatchId)
    , maKindToken(rKindToken)
    , maBeforeHash(rBeforeHash)
    , maAfterHash(rAfterHash)
    , maParagraphId(rParagraphId)
    , maUndoText(rUndoText)
    , maRedoText(rRedoText)
    , mnRangeStart(nRangeStart)
    , mnRangeLength(nRangeLength)
    , mnUndoWeight(nUndoWeight)
    , mnRedoWeight(nRedoWeight)
{
}

UndoApplyPatch::~UndoApplyPatch() {}

bool UndoApplyPatch::lcl_restoreParagraphText(const OUString& rText) const
{
    if (maParagraphId.isEmpty()
        || (maKindToken != u"paragraph-replace"_ustr
            && maKindToken != u"text-range-replace"_ustr))
        return false;
    SwTextNode* pNode = lcl_resolveParagraphNode(mrDoc, maParagraphId);
    if (!pNode)
        return false;
    const OUString& rCurrent = pNode->GetText();
    SwContentIndex aIdx(pNode, 0);
    pNode->ReplaceText(aIdx, rCurrent.getLength(), rText);
    return true;
}

bool UndoApplyPatch::lcl_restoreParagraphStyle(const OUString& rStyleName) const
{
    if (maParagraphId.isEmpty() || rStyleName.isEmpty())
        return false;
    SwTextNode* pNode = lcl_resolveParagraphNode(mrDoc, maParagraphId);
    if (!pNode)
        return false;
    SwTextFormatColl* pColl = mrDoc.FindTextFormatCollByName(UIName(rStyleName));
    if (!pColl)
        return false;
    SwPaM aPam(*pNode);
    return mrDoc.SetTextFormatColl(aPam, pColl, false);
}

bool UndoApplyPatch::lcl_restoreParagraphReformat(const OUString& rFormatChangesJson) const
{
    if (maParagraphId.isEmpty() || rFormatChangesJson.isEmpty())
        return false;
    const std::optional<sw::intelligent::ParagraphReformatAttrs> oAttrs
        = sw::intelligent::ParseFormatChangesReformat(rFormatChangesJson);
    if (!oAttrs)
        return false;
    SwTextNode* pNode = lcl_resolveParagraphNode(mrDoc, maParagraphId);
    if (!pNode)
        return false;
    return sw::intelligent::ApplyParagraphReformatAttrs(*pNode, *oAttrs);
}

bool UndoApplyPatch::lcl_restoreTextFormatWeight(sal_Int32 nWeight) const
{
    if (maParagraphId.isEmpty() || mnRangeStart < 0 || mnRangeLength <= 0 || nWeight < 0)
        return false;
    SwTextNode* pNode = lcl_resolveParagraphNode(mrDoc, maParagraphId);
    if (!pNode)
        return false;
    return lcl_applyTextFormatWeight(mrDoc, *pNode, mnRangeStart, mnRangeLength,
                                     static_cast<FontWeight>(nWeight));
}

void UndoApplyPatch::UndoImpl(::sw::UndoRedoContext& /*rContext*/)
{
    if (maKindToken == u"paragraph-delete"_ustr)
    {
        if (!lcl_insertDeletedParagraph(mrDoc, maParagraphId, maUndoText))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: paragraph-delete undo failed for "
                                          << maPatchId);
        return;
    }
    if (maKindToken == u"paragraph-insert-after"_ustr)
    {
        if (!lcl_deleteParagraphAfterAnchor(mrDoc, maParagraphId))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: paragraph-insert-after undo failed for "
                                          << maPatchId);
        return;
    }
    if (maKindToken == u"paragraph-format"_ustr)
    {
        if (!lcl_restoreParagraphStyle(maUndoText))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: paragraph-format undo failed for "
                                          << maPatchId);
        return;
    }
    if (maKindToken == u"paragraph-reformat"_ustr)
    {
        if (!lcl_restoreParagraphReformat(maUndoText))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: paragraph-reformat undo failed for "
                                          << maPatchId);
        return;
    }
    if (maKindToken == u"text-format"_ustr)
    {
        if (!lcl_restoreTextFormatWeight(mnUndoWeight))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: text-format undo failed for "
                                          << maPatchId);
        return;
    }
    if (!maUndoText.isEmpty())
    {
        if (!lcl_restoreParagraphText(maUndoText))
            SAL_WARN("sw.intelligent",
                     "UndoApplyPatch: paragraph/text-range replace undo failed for "
                         << maPatchId);
        return;
    }
    // Skeleton kinds: no document mutation to reverse yet.
}

void UndoApplyPatch::RedoImpl(::sw::UndoRedoContext& /*rContext*/)
{
    if (maKindToken == u"paragraph-delete"_ustr)
    {
        if (!lcl_deleteParagraph(mrDoc, maParagraphId))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: paragraph-delete redo failed for "
                                          << maPatchId);
        return;
    }
    if (maKindToken == u"paragraph-insert-after"_ustr)
    {
        if (!lcl_insertAfterParagraph(mrDoc, maParagraphId, maRedoText))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: paragraph-insert-after redo failed for "
                                          << maPatchId);
        return;
    }
    if (maKindToken == u"paragraph-format"_ustr)
    {
        if (!lcl_restoreParagraphStyle(maRedoText))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: paragraph-format redo failed for "
                                          << maPatchId);
        return;
    }
    if (maKindToken == u"paragraph-reformat"_ustr)
    {
        if (!lcl_restoreParagraphReformat(maRedoText))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: paragraph-reformat redo failed for "
                                          << maPatchId);
        return;
    }
    if (maKindToken == u"text-format"_ustr)
    {
        if (!lcl_restoreTextFormatWeight(mnRedoWeight))
            SAL_WARN("sw.intelligent", "UndoApplyPatch: text-format redo failed for "
                                          << maPatchId);
        return;
    }
    if (!maRedoText.isEmpty())
    {
        if (!lcl_restoreParagraphText(maRedoText))
            SAL_WARN("sw.intelligent",
                     "UndoApplyPatch: paragraph/text-range replace redo failed for "
                         << maPatchId);
        return;
    }
    SAL_WARN("sw.intelligent",
             "UndoApplyPatch: redo is no-op for skeleton patch kind " << maKindToken);
}

} // namespace sw

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */