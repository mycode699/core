/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include <DocumentAILocalRag.hxx>
#include <AgentChatSelectionCapture.hxx>

#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <comphelper/processfactory.hxx>

#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XDrawPages.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextDocument.hpp>

#include <algorithm>
#include <vector>

namespace kqoffice::ai::chat
{
namespace
{
OUString clip(const OUString& s, sal_Int32 nMax)
{
    if (nMax <= 0 || s.getLength() <= nMax)
        return s;
    return s.copy(0, nMax) + u"…"_ustr;
}

css::uno::Reference<css::frame::XModel> currentModel()
{
    try
    {
        auto xDesktop
            = css::frame::Desktop::create(comphelper::getProcessComponentContext());
        return css::uno::Reference<css::frame::XModel>(xDesktop->getCurrentComponent(),
                                                       css::uno::UNO_QUERY);
    }
    catch (const css::uno::Exception&)
    {
    }
    return {};
}

OUString captureWriter(sal_Int32 nMaxChars)
{
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::text::XTextDocument> xDoc(xModel, css::uno::UNO_QUERY);
        if (!xDoc.is() || !xDoc->getText().is())
            return OUString();
        return clip(xDoc->getText()->getString(), nMaxChars);
    }
    catch (const css::uno::Exception&)
    {
    }
    return OUString();
}

OUString captureCalc(sal_Int32 nMaxChars)
{
    try
    {
        auto xModel = currentModel();
        auto xCtrl = xModel.is() ? xModel->getCurrentController()
                                 : css::uno::Reference<css::frame::XController>();
        css::uno::Reference<css::sheet::XSpreadsheetView> xView(xCtrl, css::uno::UNO_QUERY);
        if (!xView.is())
            return OUString();
        auto xSheet = xView->getActiveSheet();
        if (!xSheet.is())
            return OUString();

        OUStringBuffer b;
        // Sample top-left used region (bounded) — local-first, no full sheet dump.
        constexpr sal_Int32 kMaxRows = 40;
        constexpr sal_Int32 kMaxCols = 12;
        for (sal_Int32 r = 0; r < kMaxRows; ++r)
        {
            OUStringBuffer row;
            bool any = false;
            for (sal_Int32 c = 0; c < kMaxCols; ++c)
            {
                auto xCell = xSheet->getCellByPosition(c, r);
                OUString v = xCell.is() ? xCell->getFormula() : OUString();
                if (v.isEmpty() && xCell.is())
                {
                    css::uno::Reference<css::text::XText> xText(xCell, css::uno::UNO_QUERY);
                    if (xText.is())
                        v = xText->getString();
                }
                if (!v.isEmpty())
                    any = true;
                if (c)
                    row.append(u'\t');
                row.append(v);
            }
            if (any)
            {
                if (!b.isEmpty())
                    b.append(u'\n');
                b.append(u"R"_ustr + OUString::number(r + 1) + u": "_ustr);
                b.append(row.makeStringAndClear());
            }
            if (b.getLength() >= nMaxChars)
                break;
        }
        return clip(b.makeStringAndClear(), nMaxChars);
    }
    catch (const css::uno::Exception&)
    {
    }
    return OUString();
}

OUString captureImpress(sal_Int32 nMaxChars)
{
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::drawing::XDrawPagesSupplier> xSupp(xModel, css::uno::UNO_QUERY);
        if (!xSupp.is())
            return OUString();
        auto xPages = xSupp->getDrawPages();
        if (!xPages.is())
            return OUString();
        css::uno::Reference<css::container::XIndexAccess> xIndex(xPages, css::uno::UNO_QUERY);
        if (!xIndex.is())
            return OUString();

        OUStringBuffer b;
        const sal_Int32 n = std::min<sal_Int32>(xIndex->getCount(), 30);
        for (sal_Int32 i = 0; i < n; ++i)
        {
            css::uno::Reference<css::drawing::XDrawPage> xPage(xIndex->getByIndex(i),
                                                               css::uno::UNO_QUERY);
            if (!xPage.is())
                continue;
            css::uno::Reference<css::drawing::XShapes> xShapes(xPage, css::uno::UNO_QUERY);
            if (!xShapes.is())
                continue;
            if (!b.isEmpty())
                b.append(u"\n"_ustr);
            b.append(u"## 幻灯 "_ustr + OUString::number(i + 1) + u"\n"_ustr);
            const sal_Int32 nShapes = std::min<sal_Int32>(xShapes->getCount(), 40);
            for (sal_Int32 s = 0; s < nShapes; ++s)
            {
                css::uno::Reference<css::drawing::XShape> xShape(xShapes->getByIndex(s),
                                                                 css::uno::UNO_QUERY);
                css::uno::Reference<css::text::XText> xText(xShape, css::uno::UNO_QUERY);
                if (!xText.is())
                    continue;
                const OUString t = xText->getString().trim();
                if (t.isEmpty())
                    continue;
                b.append(t);
                b.append(u'\n');
                if (b.getLength() >= nMaxChars)
                    break;
            }
            if (b.getLength() >= nMaxChars)
                break;
        }
        return clip(b.makeStringAndClear(), nMaxChars);
    }
    catch (const css::uno::Exception&)
    {
    }
    return OUString();
}

std::vector<OUString> tokenize(const OUString& r)
{
    std::vector<OUString> out;
    OUStringBuffer cur;
    auto flush = [&]() {
        if (cur.isEmpty())
            return;
        OUString t = cur.makeStringAndClear().toAsciiLowerCase();
        if (t.getLength() >= 2)
            out.push_back(t);
    };
    for (sal_Int32 i = 0; i < r.getLength(); ++i)
    {
        const sal_Unicode c = r[i];
        const bool word = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'z')
                          || (c >= u'A' && c <= u'Z') || c > 127;
        if (word)
            cur.append(c);
        else
            flush();
    }
    flush();
    // Also add CJK bigrams for better Chinese recall
    for (sal_Int32 i = 0; i + 1 < r.getLength(); ++i)
    {
        if (r[i] > 127 && r[i + 1] > 127)
            out.push_back(r.copy(i, 2));
    }
    return out;
}

sal_Int32 scoreChunk(const OUString& chunk, const std::vector<OUString>& queryTokens)
{
    if (chunk.isEmpty() || queryTokens.empty())
        return 0;
    const OUString low = chunk.toAsciiLowerCase();
    sal_Int32 score = 0;
    for (const auto& t : queryTokens)
    {
        if (t.isEmpty())
            continue;
        sal_Int32 pos = 0;
        while ((pos = low.indexOf(t, pos)) >= 0)
        {
            score += (t.getLength() >= 2) ? 3 : 1;
            pos += t.getLength();
        }
    }
    return score;
}
} // namespace

bool DocumentAILocalRag::wantsDocumentRag(const OUString& rUserInput)
{
    const OUString t = rUserInput.trim();
    if (t.isEmpty())
        return false;
    if (t.startsWith(u"/问本文档"_ustr) || t.startsWith(u"/askdoc"_ustr)
        || t.startsWith(u"/本文档"_ustr))
        return true;
    const OUString low = t.toAsciiLowerCase();
    return low.indexOf(u"本文档"_ustr) >= 0 || low.indexOf(u"问文档"_ustr) >= 0
           || low.indexOf(u"全文"_ustr) >= 0 || low.indexOf(u"文档里"_ustr) >= 0
           || low.indexOf(u"文档中"_ustr) >= 0 || low.indexOf(u"根据文档"_ustr) >= 0
           || low.indexOf(u"this document"_ustr) >= 0 || low.indexOf(u"ask document"_ustr) >= 0
           || low.indexOf(u"in this doc"_ustr) >= 0 || low.indexOf(u"whole document"_ustr) >= 0;
}

OUString DocumentAILocalRag::captureDocumentText(sal_Int32 nMaxChars)
{
    const SelectionContext sel = AgentChatSelectionCapture::captureCurrent();
    if (sel.surface == u"writer"_ustr)
        return captureWriter(nMaxChars);
    if (sel.surface == u"calc"_ustr)
        return captureCalc(nMaxChars);
    if (sel.surface == u"impress"_ustr)
        return captureImpress(nMaxChars);
    // Fallback try all
    OUString t = captureWriter(nMaxChars);
    if (!t.isEmpty())
        return t;
    t = captureCalc(nMaxChars);
    if (!t.isEmpty())
        return t;
    return captureImpress(nMaxChars);
}

std::vector<LocalRagChunk> DocumentAILocalRag::chunkDocument(sal_Int32 nMaxChunks,
                                                             sal_Int32 nMaxChunkChars)
{
    std::vector<LocalRagChunk> out;
    const OUString full = captureDocumentText(nMaxChunks * nMaxChunkChars);
    if (full.isEmpty())
        return out;

    sal_Int32 pos = 0;
    sal_Int32 ordinal = 1;
    while (pos < full.getLength() && static_cast<sal_Int32>(out.size()) < nMaxChunks)
    {
        sal_Int32 nl = full.indexOf(u'\n', pos);
        if (nl < 0)
            nl = full.getLength();
        // Merge short lines into ~paragraph chunks
        OUStringBuffer chunk;
        while (pos < full.getLength() && chunk.getLength() < nMaxChunkChars)
        {
            nl = full.indexOf(u'\n', pos);
            if (nl < 0)
                nl = full.getLength();
            OUString line = full.copy(pos, nl - pos).trim();
            pos = (nl < full.getLength()) ? nl + 1 : nl;
            if (line.isEmpty())
            {
                if (!chunk.isEmpty())
                    break;
                continue;
            }
            if (!chunk.isEmpty())
                chunk.append(u'\n');
            chunk.append(line);
            if (chunk.getLength() >= nMaxChunkChars / 2 && line.getLength() < 40)
                break;
        }
        OUString body = chunk.makeStringAndClear().trim();
        if (body.isEmpty())
            continue;
        LocalRagChunk c;
        c.position = u"chunk:"_ustr + OUString::number(ordinal++);
        c.text = clip(body, nMaxChunkChars);
        out.push_back(c);
    }
    return out;
}

std::vector<LocalRagChunk> DocumentAILocalRag::retrieve(const OUString& rQuery, sal_Int32 nTopK,
                                                        sal_Int32 nMaxChunkChars)
{
    auto chunks = chunkDocument(/*nMaxChunks*/ 64, nMaxChunkChars);
    if (chunks.empty())
        return chunks;

    const auto tokens = tokenize(rQuery);
    for (auto& c : chunks)
        c.score = scoreChunk(c.text, tokens);

    std::stable_sort(chunks.begin(), chunks.end(),
                     [](const LocalRagChunk& a, const LocalRagChunk& b) {
                         return a.score > b.score;
                     });

    // If all scores 0 (no keyword hit), keep leading structural chunks.
    const sal_Int32 k = std::max<sal_Int32>(1, nTopK);
    if (static_cast<sal_Int32>(chunks.size()) > k)
        chunks.resize(static_cast<size_t>(k));
    return chunks;
}

OUString DocumentAILocalRag::buildContextBlock(const OUString& rQuery, sal_Int32 nTopK,
                                               sal_Int32 nMaxChars)
{
    auto hits = retrieve(rQuery, nTopK);
    if (hits.empty())
        return OUString();

    OUStringBuffer b;
    b.append(u"【本地文档检索 — 仅当前打开文档，无外传】\n"_ustr);
    sal_Int32 rank = 1;
    for (const auto& h : hits)
    {
        if (b.getLength() >= nMaxChars)
            break;
        b.append(u"["_ustr);
        b.append(rank++);
        b.append(u"] "_ustr);
        b.append(h.position);
        if (h.score > 0)
        {
            b.append(u" score="_ustr);
            b.append(h.score);
        }
        b.append(u"\n"_ustr);
        b.append(clip(h.text, 800));
        b.append(u"\n\n"_ustr);
    }
    OUString out = b.makeStringAndClear();
    if (out.getLength() > nMaxChars)
        out = out.copy(0, nMaxChars) + u"…"_ustr;
    return out;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
