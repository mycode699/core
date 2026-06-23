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
#include <undobj.hxx>

#include <rtl/ustring.hxx>

class SwDoc;

namespace sw
{
// SwUndo subclass for a single AI-plan patch application.
//
// Holds patch_id + kind token + before/after content hashes. Per-kind
// concrete behavior (text replace / paragraph insert / format set ...)
// lands in W3 Day-1c/d/e/f as additional subclasses; the skeleton here
// records the tag so the H7 harness can grep all 7 kind tokens out of
// this header.
//
// 7 patch kinds — order locked to W3 spec §"Patch Kinds（v1）" table.
// H7 harness (tests/v2-apply-plan-runtime-schema-test.sh) extracts these
// 7 string literals and asserts the schema kind enum matches.
//
//   "paragraph-replace"
//   "paragraph-insert-after"
//   "paragraph-delete"
//   "paragraph-format"
//   "paragraph-reformat"
//   "text-range-replace"
//   "text-format"
class SW_DLLPUBLIC UndoApplyPatch final : public SwUndo
{
public:
    UndoApplyPatch(SwDoc& rDoc, const OUString& rPatchId, const OUString& rKindToken,
                   const OUString& rBeforeHash, const OUString& rAfterHash,
                   const OUString& rParagraphId = OUString(),
                   const OUString& rUndoText = OUString(),
                   const OUString& rRedoText = OUString(),
                   sal_Int32 nRangeStart = -1, sal_Int32 nRangeLength = -1,
                   sal_Int32 nUndoWeight = -1, sal_Int32 nRedoWeight = -1);
    virtual ~UndoApplyPatch() override;

    void UndoImpl(::sw::UndoRedoContext& rContext) override;
    void RedoImpl(::sw::UndoRedoContext& rContext) override;

    const OUString& GetPatchId() const { return maPatchId; }
    const OUString& GetKindToken() const { return maKindToken; }
    const OUString& GetBeforeHash() const { return maBeforeHash; }
    const OUString& GetAfterHash() const { return maAfterHash; }

private:
    bool lcl_restoreParagraphText(const OUString& rText) const;
    bool lcl_restoreParagraphStyle(const OUString& rStyleName) const;
    bool lcl_restoreParagraphReformat(const OUString& rFormatChangesJson) const;
    bool lcl_restoreTextFormatWeight(sal_Int32 nWeight) const;

    SwDoc& mrDoc;
    OUString maPatchId;
    OUString maKindToken;
    OUString maBeforeHash;
    OUString maAfterHash;
    OUString maParagraphId;
    OUString maUndoText;
    OUString maRedoText;
    sal_Int32 mnRangeStart;
    sal_Int32 mnRangeLength;
    sal_Int32 mnUndoWeight;
    sal_Int32 mnRedoWeight;
};

} // namespace sw

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */