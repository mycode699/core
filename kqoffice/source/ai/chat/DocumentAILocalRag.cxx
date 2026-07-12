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
#include <com/sun/star/drawing/XDrawView.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/text/XParagraphCursor.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>

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

    // Prefer real para:N chunks on Writer for locate-able provenance.
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::text::XTextDocument> xDoc(xModel, css::uno::UNO_QUERY);
        if (xDoc.is() && xDoc->getText().is())
        {
            auto xText = xDoc->getText();
            auto xCursor = xText->createTextCursor();
            css::uno::Reference<css::text::XParagraphCursor> xPara(
                xCursor, css::uno::UNO_QUERY);
            if (xPara.is())
            {
                xPara->gotoStart(false);
                sal_Int32 paraIdx = 0; // 0-based; UI tokens use 1-based para:N
                do
                {
                    xPara->gotoEndOfParagraph(true);
                    OUString body = xCursor->getString().trim();
                    xPara->collapseToEnd();
                    if (!body.isEmpty())
                    {
                        LocalRagChunk c;
                        c.position = u"para:"_ustr + OUString::number(paraIdx + 1);
                        c.text = clip(body, nMaxChunkChars);
                        out.push_back(c);
                        if (static_cast<sal_Int32>(out.size()) >= nMaxChunks)
                            return out;
                    }
                    ++paraIdx;
                    if (paraIdx > 5000)
                        break;
                } while (xPara->gotoNextParagraph(false));
                if (!out.empty())
                    return out;
            }
        }
    }
    catch (const css::uno::Exception&)
    {
    }

    // Fallback: newline-merged chunks (calc/impress/plain capture).
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

OUString DocumentAILocalRag::formatAnswerCard(const OUString& rQuery, const OUString& rAnswer,
                                              sal_Int32 nTopK)
{
    OUStringBuffer card;
    card.append(u"【问本文档 · 本地检索 · 无外传】\n"_ustr);
    card.append(u"问题："_ustr);
    card.append(clip(rQuery, 200));
    card.append(u"\n\n—— 回答 ——\n"_ustr);
    card.append(rAnswer.isEmpty() ? u"（模型未返回正文）"_ustr : rAnswer);

    auto hits = retrieve(rQuery, nTopK);
    if (!hits.empty())
    {
        card.append(u"\n\n—— 可定位出处（在文档中查找下列位置）——\n"_ustr);
        sal_Int32 rank = 1;
        for (const auto& h : hits)
        {
            card.append(u"· ["_ustr);
            card.append(rank++);
            card.append(u"] "_ustr);
            card.append(h.position.isEmpty() ? u"（未知位置）"_ustr : h.position);
            OUString snippet = h.text;
            snippet = snippet.replaceAll(u"\n"_ustr, u" "_ustr);
            if (snippet.getLength() > 72)
                snippet = snippet.copy(0, 72) + u"…"_ustr;
            if (!snippet.isEmpty())
            {
                card.append(u" — "_ustr);
                card.append(snippet);
            }
            card.append(u"\n"_ustr);
        }
        card.append(u"\n提示：复制位置标记，在 Writer/Calc/Impress 中对照查找；"
                    u"主文档不会被自动改写。"_ustr);
    }
    else
    {
        card.append(u"\n\n（未命中可定位片段 — 可换关键词再问）"_ustr);
    }
    card.append(u"\n\n操作：点侧栏「定位出处」跳到首条命中位置（只选中，不改文档）。"_ustr);
    return card.makeStringAndClear();
}

namespace
{
bool parseParaToken(const OUString& rPos, sal_Int32& rOut1Based)
{
    if (!rPos.startsWith(u"para:"_ustr))
        return false;
    rOut1Based = rPos.copy(5).toInt32();
    return rOut1Based > 0;
}

bool parseSlideToken(const OUString& rPos, sal_Int32& rOut1Based)
{
    if (!rPos.startsWith(u"slide:"_ustr))
        return false;
    rOut1Based = rPos.copy(6).toInt32();
    return rOut1Based > 0;
}

bool parseCellToken(const OUString& rPos, sal_Int32& rCol, sal_Int32& rRow)
{
    // cell:A1 or cell:AB12
    if (!rPos.startsWith(u"cell:"_ustr))
        return false;
    OUString rest = rPos.copy(5).trim().toAsciiUpperCase();
    if (rest.isEmpty())
        return false;
    sal_Int32 i = 0;
    sal_Int32 col = 0;
    while (i < rest.getLength() && rest[i] >= u'A' && rest[i] <= u'Z')
    {
        col = col * 26 + (rest[i] - u'A' + 1);
        ++i;
    }
    if (col <= 0 || i >= rest.getLength())
        return false;
    sal_Int32 row = rest.copy(i).toInt32();
    if (row <= 0)
        return false;
    rCol = col - 1; // 0-based
    rRow = row - 1;
    return true;
}

LocalRagLocateResult locateWriterPara(sal_Int32 para1Based)
{
    LocalRagLocateResult r;
    r.position = u"para:"_ustr + OUString::number(para1Based);
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::text::XTextDocument> xDoc(xModel, css::uno::UNO_QUERY);
        if (!xDoc.is() || !xDoc->getText().is())
        {
            r.message = u"定位失败：当前不是文字文档"_ustr;
            return r;
        }
        auto xText = xDoc->getText();
        auto xCursor = xText->createTextCursor();
        css::uno::Reference<css::text::XParagraphCursor> xPara(xCursor, css::uno::UNO_QUERY);
        if (!xPara.is())
        {
            r.message = u"定位失败：无法创建段落游标"_ustr;
            return r;
        }
        xPara->gotoStart(false);
        for (sal_Int32 i = 1; i < para1Based; ++i)
        {
            if (!xPara->gotoNextParagraph(false))
            {
                r.message = u"定位失败：段落超出范围 "_ustr + r.position;
                return r;
            }
        }
        xPara->gotoEndOfParagraph(true);

        // Sync view selection when possible.
        if (xModel.is())
        {
            auto xCtrl = xModel->getCurrentController();
            css::uno::Reference<css::text::XTextViewCursorSupplier> xSupp(xCtrl,
                                                                          css::uno::UNO_QUERY);
            if (xSupp.is())
            {
                auto xView = xSupp->getViewCursor();
                if (xView.is())
                {
                    css::uno::Reference<css::text::XTextCursor> xViewCur(xView, css::uno::UNO_QUERY);
                    if (xViewCur.is())
                    {
                        xViewCur->gotoRange(xCursor->getStart(), false);
                        xViewCur->gotoRange(xCursor->getEnd(), true);
                    }
                }
            }
            css::uno::Reference<css::view::XSelectionSupplier> xSel(xCtrl, css::uno::UNO_QUERY);
            if (xSel.is())
                xSel->select(css::uno::Any(xCursor));
        }
        r.success = true;
        r.message = u"已定位到 "_ustr + r.position + u" · 仅选中，未改文档"_ustr;
        return r;
    }
    catch (const css::uno::Exception& e)
    {
        r.message = u"定位失败："_ustr + e.Message;
        return r;
    }
}

LocalRagLocateResult locateCalcCell(sal_Int32 col0, sal_Int32 row0)
{
    LocalRagLocateResult r;
    {
        OUString col;
        sal_Int32 c = col0 + 1;
        while (c > 0)
        {
            const sal_Unicode ch = static_cast<sal_Unicode>(u'A' + ((c - 1) % 26));
            col = OUString(&ch, 1) + col;
            c = (c - 1) / 26;
        }
        r.position = u"cell:"_ustr + col + OUString::number(row0 + 1);
    }
    try
    {
        auto xModel = currentModel();
        auto xCtrl = xModel.is() ? xModel->getCurrentController()
                                 : css::uno::Reference<css::frame::XController>();
        css::uno::Reference<css::sheet::XSpreadsheetView> xView(xCtrl, css::uno::UNO_QUERY);
        if (!xView.is())
        {
            r.message = u"定位失败：当前不是表格文档"_ustr;
            return r;
        }
        auto xSheet = xView->getActiveSheet();
        if (!xSheet.is())
        {
            r.message = u"定位失败：无活动表"_ustr;
            return r;
        }
        auto xCell = xSheet->getCellByPosition(col0, row0);
        css::uno::Reference<css::view::XSelectionSupplier> xSel(xCtrl, css::uno::UNO_QUERY);
        if (xSel.is() && xCell.is())
            xSel->select(css::uno::Any(xCell));
        r.success = true;
        r.message = u"已定位到 "_ustr + r.position + u" · 仅选中，未改文档"_ustr;
        return r;
    }
    catch (const css::uno::Exception& e)
    {
        r.message = u"定位失败："_ustr + e.Message;
        return r;
    }
}

LocalRagLocateResult locateImpressSlide(sal_Int32 slide1Based)
{
    LocalRagLocateResult r;
    r.position = u"slide:"_ustr + OUString::number(slide1Based);
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::drawing::XDrawPagesSupplier> xSupp(xModel, css::uno::UNO_QUERY);
        if (!xSupp.is())
        {
            r.message = u"定位失败：当前不是演示文档"_ustr;
            return r;
        }
        auto xPages = xSupp->getDrawPages();
        css::uno::Reference<css::container::XIndexAccess> xIndex(xPages, css::uno::UNO_QUERY);
        if (!xIndex.is() || slide1Based < 1 || slide1Based > xIndex->getCount())
        {
            r.message = u"定位失败：幻灯页超出范围 "_ustr + r.position;
            return r;
        }
        css::uno::Reference<css::drawing::XDrawPage> xPage(
            xIndex->getByIndex(slide1Based - 1), css::uno::UNO_QUERY);
        auto xCtrl = xModel.is() ? xModel->getCurrentController()
                                 : css::uno::Reference<css::frame::XController>();
        css::uno::Reference<css::drawing::XDrawView> xDrawView(xCtrl, css::uno::UNO_QUERY);
        if (xDrawView.is() && xPage.is())
            xDrawView->setCurrentPage(xPage);
        r.success = true;
        r.message = u"已定位到 "_ustr + r.position + u" · 仅切换页，未改文档"_ustr;
        return r;
    }
    catch (const css::uno::Exception& e)
    {
        r.message = u"定位失败："_ustr + e.Message;
        return r;
    }
}

LocalRagLocateResult locateBySnippet(const OUString& rSnippet)
{
    LocalRagLocateResult r;
    r.position = u"snippet"_ustr;
    if (rSnippet.trim().isEmpty())
    {
        r.message = u"定位失败：无可用文本片段"_ustr;
        return r;
    }
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::text::XTextDocument> xDoc(xModel, css::uno::UNO_QUERY);
        if (!xDoc.is() || !xDoc->getText().is())
        {
            r.message = u"定位失败：片段定位仅支持文字文档"_ustr;
            return r;
        }
        const OUString full = xDoc->getText()->getString();
        const OUString needle = rSnippet.trim();
        sal_Int32 found = full.indexOf(needle.copy(0, std::min<sal_Int32>(needle.getLength(), 24)));
        if (found < 0)
        {
            r.message = u"定位失败：文档中未找到该片段"_ustr;
            return r;
        }
        auto xCursor = xDoc->getText()->createTextCursor();
        xCursor->gotoStart(false);
        if (found > 0)
            xCursor->goRight(found, false);
        const sal_Int32 selLen = std::min<sal_Int32>(needle.getLength(), 80);
        xCursor->goRight(selLen, true);
        auto xCtrl = xModel->getCurrentController();
        css::uno::Reference<css::view::XSelectionSupplier> xSel(xCtrl, css::uno::UNO_QUERY);
        if (xSel.is())
            xSel->select(css::uno::Any(xCursor));
        r.success = true;
        r.message = u"已按片段选中正文 · 未改文档"_ustr;
        return r;
    }
    catch (const css::uno::Exception& e)
    {
        r.message = u"定位失败："_ustr + e.Message;
        return r;
    }
}
} // namespace

LocalRagLocateResult DocumentAILocalRag::locatePosition(const OUString& rPosition)
{
    LocalRagLocateResult r;
    const OUString pos = rPosition.trim();
    if (pos.isEmpty())
    {
        r.message = u"定位失败：位置为空"_ustr;
        return r;
    }
    sal_Int32 para = 0;
    if (parseParaToken(pos, para))
        return locateWriterPara(para);
    sal_Int32 slide = 0;
    if (parseSlideToken(pos, slide))
        return locateImpressSlide(slide);
    sal_Int32 col = 0, row = 0;
    if (parseCellToken(pos, col, row))
        return locateCalcCell(col, row);
    if (pos.startsWith(u"chunk:"_ustr))
    {
        // Resolve chunk ordinal via current chunkDocument order.
        const sal_Int32 n = pos.copy(6).toInt32();
        auto chunks = chunkDocument(64, 600);
        if (n >= 1 && n <= static_cast<sal_Int32>(chunks.size()))
            return locateBySnippet(chunks[static_cast<size_t>(n - 1)].text);
        r.message = u"定位失败：chunk 超出范围 "_ustr + pos;
        return r;
    }
    r.message = u"定位失败：无法识别位置标记 "_ustr + pos;
    return r;
}

LocalRagLocateResult DocumentAILocalRag::locateFirstHit(const OUString& rQuery)
{
    auto hits = retrieve(rQuery, 1);
    if (hits.empty())
    {
        LocalRagLocateResult r;
        r.message = u"定位失败：当前文档无命中片段"_ustr;
        return r;
    }
    LocalRagLocateResult r = locatePosition(hits.front().position);
    if (!r.success && !hits.front().text.isEmpty())
        r = locateBySnippet(hits.front().text);
    if (r.success && r.position.isEmpty())
        r.position = hits.front().position;
    return r;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
