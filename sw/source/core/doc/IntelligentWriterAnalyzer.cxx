/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <IntelligentWriterAnalyzer.hxx>
#include <IntelligentWriterApplyEngine.hxx>

#include <doc.hxx>
#include <ndarr.hxx>
#include <ndtxt.hxx>
#include <node.hxx>

namespace sw::intelligent
{
namespace
{
DiagnosticAction MakePreviewAction()
{
    return { u"preview-suggestion"_ustr, u"预览建议"_ustr, u"preview"_ustr };
}

Diagnostic MakeLongParagraphDiagnostic(sal_uInt32 nParagraph)
{
    Diagnostic aDiagnostic;
    aDiagnostic.maId = u"writer.paragraph.long-preview"_ustr;
    aDiagnostic.maModule = u"writer"_ustr;
    aDiagnostic.maSeverity = u"suggestion"_ustr;
    aDiagnostic.maTitleZh = u"段落较长"_ustr;
    aDiagnostic.maMessageZh = u"该段落较长，建议预览拆分或提炼小标题以提升可读性。"_ustr;
    aDiagnostic.maLocation.maKind = u"paragraph"_ustr;
    aDiagnostic.maLocation.maLabelZh = u"第 "_ustr + OUString::number(nParagraph) + u" 段"_ustr;
    aDiagnostic.maLocation.maPath = u"paragraph/"_ustr + OUString::number(nParagraph);
    aDiagnostic.maActions.push_back(MakePreviewAction());
    aDiagnostic.maEvidence.maSource = u"analyzer"_ustr;
    aDiagnostic.maEvidence.maPath = aDiagnostic.maLocation.maPath;
    aDiagnostic.maEvidence.maSummaryZh = u"仅基于文档模型读取段落长度，不修改文档。"_ustr;
    return aDiagnostic;
}
}

std::vector<Diagnostic> AnalyzeWriterDocumentPreview(const SwDoc& rDoc)
{
    std::vector<Diagnostic> aDiagnostics;
    const SwNodes& rNodes = rDoc.GetNodes();
    sal_uInt32 nParagraph = 0;

    for (SwNodeOffset nNode(0); nNode < rNodes.Count(); ++nNode)
    {
        const SwTextNode* pTextNode = rNodes[nNode]->GetTextNode();
        if (!pTextNode)
            continue;

        ++nParagraph;
        if (pTextNode->GetText().getLength() > 280)
            aDiagnostics.push_back(MakeLongParagraphDiagnostic(nParagraph));
    }

    return aDiagnostics;
}

ApplyResult runApply(SwDocShell& rDocShell, const ApplyPlan& rPlan)
{
    // W3 Day-1b D1 bridge — exposes the apply engine to callers that already
    // include IntelligentWriterAnalyzer.hxx, mirroring the preview/apply pair
    // documented in W3 spec §"File Map" L48-49.
    return ApplyEngine(rDocShell).run(rPlan);
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
