/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include <DocumentAIDocumentTools.hxx>
#include <AgentChatSelectionCapture.hxx>

#include <comphelper/hash.hxx>
#include <comphelper/processfactory.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

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
#include <com/sun/star/text/XParagraphCursor.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/text/XTextDocument.hpp>

#include <algorithm>
#include <mutex>
#include <vector>

namespace kqoffice::ai::chat
{
namespace
{
std::mutex& seenMutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}

OUString& seenHashStorage()
{
    static OUString s_hash;
    return s_hash;
}

OUString clip(const OUString& s, sal_Int32 nMax)
{
    if (nMax <= 0 || s.getLength() <= nMax)
        return s;
    return s.copy(0, nMax) + u"…"_ustr;
}

OUString collapseWs(const OUString& s)
{
    OUStringBuffer b;
    bool prevSpace = false;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u' ' || c == u'\t' || c == u'\n' || c == u'\r')
        {
            if (!prevSpace && !b.isEmpty())
            {
                b.append(u' ');
                prevSpace = true;
            }
            continue;
        }
        prevSpace = false;
        b.append(c);
    }
    return b.makeStringAndClear().trim();
}

css::uno::Reference<css::frame::XModel> currentModel()
{
    try
    {
        auto xDesktop = css::frame::Desktop::create(comphelper::getProcessComponentContext());
        return css::uno::Reference<css::frame::XModel>(xDesktop->getCurrentComponent(),
                                                       css::uno::UNO_QUERY);
    }
    catch (const css::uno::Exception&)
    {
    }
    return {};
}

OUString shortSha256(const OUString& rInput)
{
    if (rInput.isEmpty())
        return OUString();
    const OString utf8 = OUStringToOString(rInput, RTL_TEXTENCODING_UTF8);
    const auto hash
        = comphelper::Hash::calculateHash(utf8.getStr(), utf8.getLength(),
                                          comphelper::HashType::SHA256);
    const OUString full = OUString::createFromAscii(comphelper::hashToString(hash));
    return full.getLength() > 16 ? full.copy(0, 16) : full;
}

OUString colName(sal_Int32 col0)
{
    // 0 -> A, 25 -> Z, 26 -> AA
    OUStringBuffer b;
    sal_Int32 n = col0;
    do
    {
        b.insert(0, sal_Unicode(u'A' + (n % 26)));
        n = n / 26 - 1;
    } while (n >= 0);
    return b.makeStringAndClear();
}

void appendWriterBlocks(std::vector<DocumentToolBlock>& out, sal_Int32 nMaxBlocks)
{
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::text::XTextDocument> xDoc(xModel, css::uno::UNO_QUERY);
        if (!xDoc.is() || !xDoc->getText().is())
            return;
        auto xText = xDoc->getText();
        auto xCursor = xText->createTextCursor();
        css::uno::Reference<css::text::XParagraphCursor> xPara(xCursor, css::uno::UNO_QUERY);
        if (!xPara.is())
            return;
        xPara->gotoStart(false);
        sal_Int32 paraIdx = 0;
        do
        {
            xPara->gotoEndOfParagraph(true);
            const OUString body = collapseWs(xCursor->getString());
            xPara->collapseToEnd();
            DocumentToolBlock blk;
            blk.index = static_cast<sal_Int32>(out.size());
            blk.type = u"p"_ustr;
            blk.position = u"para:"_ustr + OUString::number(paraIdx + 1);
            blk.preview = body;
            if (!body.isEmpty() || paraIdx == 0)
                out.push_back(blk);
            ++paraIdx;
            if (static_cast<sal_Int32>(out.size()) >= nMaxBlocks || paraIdx > 8000)
                break;
        } while (xPara->gotoNextParagraph(false));
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat", "DocumentAIDocumentTools writer skeleton: " << e.Message);
    }
}

void appendCalcBlocks(std::vector<DocumentToolBlock>& out, sal_Int32 nMaxBlocks)
{
    try
    {
        auto xModel = currentModel();
        auto xCtrl = xModel.is() ? xModel->getCurrentController()
                                 : css::uno::Reference<css::frame::XController>();
        css::uno::Reference<css::sheet::XSpreadsheetView> xView(xCtrl, css::uno::UNO_QUERY);
        if (!xView.is())
            return;
        auto xSheet = xView->getActiveSheet();
        if (!xSheet.is())
            return;

        constexpr sal_Int32 kMaxRows = 80;
        constexpr sal_Int32 kMaxCols = 16;
        for (sal_Int32 r = 0; r < kMaxRows; ++r)
        {
            OUStringBuffer rowPreview;
            bool any = false;
            OUString firstCell;
            for (sal_Int32 c = 0; c < kMaxCols; ++c)
            {
                auto xCell = xSheet->getCellByPosition(c, r);
                OUString v;
                if (xCell.is())
                {
                    v = xCell->getFormula();
                    if (v.isEmpty())
                    {
                        css::uno::Reference<css::text::XText> xText(xCell, css::uno::UNO_QUERY);
                        if (xText.is())
                            v = xText->getString();
                    }
                }
                if (!v.isEmpty())
                {
                    any = true;
                    if (firstCell.isEmpty())
                        firstCell = colName(c) + OUString::number(r + 1);
                }
                if (c)
                    rowPreview.append(u'\t');
                rowPreview.append(collapseWs(v));
            }
            if (!any)
                continue;
            DocumentToolBlock blk;
            blk.index = static_cast<sal_Int32>(out.size());
            blk.type = u"cell-row"_ustr;
            blk.position = u"cell:"_ustr + (firstCell.isEmpty() ? u"A1"_ustr : firstCell);
            blk.preview = collapseWs(rowPreview.makeStringAndClear());
            out.push_back(blk);
            if (static_cast<sal_Int32>(out.size()) >= nMaxBlocks)
                break;
        }
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat", "DocumentAIDocumentTools calc skeleton: " << e.Message);
    }
}

void appendImpressBlocks(std::vector<DocumentToolBlock>& out, sal_Int32 nMaxBlocks)
{
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::drawing::XDrawPagesSupplier> xSupp(xModel, css::uno::UNO_QUERY);
        if (!xSupp.is())
            return;
        auto xPages = xSupp->getDrawPages();
        css::uno::Reference<css::container::XIndexAccess> xIndex(xPages, css::uno::UNO_QUERY);
        if (!xIndex.is())
            return;
        const sal_Int32 nPages = std::min<sal_Int32>(xIndex->getCount(), 40);
        for (sal_Int32 i = 0; i < nPages; ++i)
        {
            css::uno::Reference<css::drawing::XDrawPage> xPage(xIndex->getByIndex(i),
                                                               css::uno::UNO_QUERY);
            if (!xPage.is())
                continue;
            css::uno::Reference<css::drawing::XShapes> xShapes(xPage, css::uno::UNO_QUERY);
            OUStringBuffer slidePreview;
            if (xShapes.is())
            {
                const sal_Int32 nShapes = std::min<sal_Int32>(xShapes->getCount(), 30);
                for (sal_Int32 s = 0; s < nShapes; ++s)
                {
                    css::uno::Reference<css::drawing::XShape> xShape(xShapes->getByIndex(s),
                                                                     css::uno::UNO_QUERY);
                    css::uno::Reference<css::text::XText> xText(xShape, css::uno::UNO_QUERY);
                    if (!xText.is())
                        continue;
                    const OUString t = collapseWs(xText->getString());
                    if (t.isEmpty())
                        continue;
                    if (!slidePreview.isEmpty())
                        slidePreview.append(u" · "_ustr);
                    slidePreview.append(t);
                }
            }
            DocumentToolBlock blk;
            blk.index = static_cast<sal_Int32>(out.size());
            blk.type = u"slide"_ustr;
            blk.position = u"slide:"_ustr + OUString::number(i + 1);
            blk.preview = collapseWs(slidePreview.makeStringAndClear());
            out.push_back(blk);
            if (static_cast<sal_Int32>(out.size()) >= nMaxBlocks)
                break;
        }
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat", "DocumentAIDocumentTools impress skeleton: " << e.Message);
    }
}

OUString formatSkeletonLines(const DocumentToolSkeleton& sk, sal_Int32 nMaxChars,
                             sal_Int32 nPreviewChars, sal_Int32 nTightPreview)
{
    auto render = [&](sal_Int32 bodyMax) {
        std::vector<OUString> lines;
        lines.reserve(sk.blocks.size() + 4);
        lines.push_back(u"【文档块列表 · 仅本地 · 无外传】"_ustr);
        lines.push_back(u"surface="_ustr + sk.surface + u" blocks="_ustr
                        + OUString::number(sk.blockCount) + u" snapshot="_ustr
                        + sk.snapshotHash);
        lines.push_back(sk.statsLine);
        if (!sk.selectionLine.isEmpty())
            lines.push_back(sk.selectionLine);
        lines.push_back(u"index|type|position|preview"_ustr);
        for (const auto& b : sk.blocks)
        {
            OUString line = OUString::number(b.index) + u"|"_ustr + b.type + u"|"_ustr
                            + b.position + u"|"_ustr + clip(b.preview, bodyMax);
            lines.push_back(line);
        }
        lines.push_back(
            u"纪律：改写前须用块索引定位；预览截断时不要凭截断内容重写全文；"
            u"写回须 ApplyPlan 并经用户批准；主文档默认不改。"_ustr);
        OUStringBuffer buf;
        for (const auto& line : lines)
        {
            if (!buf.isEmpty())
                buf.append(u'\n');
            buf.append(line);
        }
        return buf.makeStringAndClear();
    };

    OUString text = render(nPreviewChars);
    if (text.getLength() > nMaxChars)
        text = render(nTightPreview);
    if (text.getLength() > nMaxChars)
    {
        // Keep ends so indexes remain verifiable.
        const sal_Int32 keepHead = nMaxChars * 2 / 3;
        const sal_Int32 keepTail = nMaxChars / 5;
        if (keepHead + keepTail + 40 < text.getLength())
        {
            text = text.copy(0, keepHead) + u"\n…(中间块已省略；编号连续)…\n"_ustr
                   + text.copy(text.getLength() - keepTail);
        }
        else
            text = clip(text, nMaxChars);
    }
    return text;
}

OUString readWriterParagraph(sal_Int32 nPara1Based)
{
    try
    {
        auto xModel = currentModel();
        css::uno::Reference<css::text::XTextDocument> xDoc(xModel, css::uno::UNO_QUERY);
        if (!xDoc.is() || !xDoc->getText().is() || nPara1Based <= 0)
            return OUString();
        auto xText = xDoc->getText();
        auto xCursor = xText->createTextCursor();
        css::uno::Reference<css::text::XParagraphCursor> xPara(xCursor, css::uno::UNO_QUERY);
        if (!xPara.is())
            return OUString();
        xPara->gotoStart(false);
        for (sal_Int32 i = 1; i < nPara1Based; ++i)
        {
            if (!xPara->gotoNextParagraph(false))
                return OUString();
        }
        xPara->gotoEndOfParagraph(true);
        return xCursor->getString();
    }
    catch (const css::uno::Exception&)
    {
    }
    return OUString();
}
} // namespace

DocumentToolSkeleton DocumentAIDocumentTools::buildSkeleton(sal_Int32 nMaxBlocks,
                                                            sal_Int32 nMaxChars,
                                                            sal_Int32 nPreviewChars)
{
    DocumentToolSkeleton sk;
    const SelectionContext sel = AgentChatSelectionCapture::captureCurrent();
    sk.surface = sel.surface.isEmpty() ? u"none"_ustr : sel.surface;
    sk.hasDocument = sk.surface != u"none"_ustr && sk.surface != u"unknown"_ustr;

    if (!sk.hasDocument)
    {
        sk.formatted.clear();
        return sk;
    }

    const sal_Int32 maxBlocks = std::max<sal_Int32>(1, nMaxBlocks);
    if (sk.surface == u"writer"_ustr)
        appendWriterBlocks(sk.blocks, maxBlocks);
    else if (sk.surface == u"calc"_ustr)
        appendCalcBlocks(sk.blocks, maxBlocks);
    else if (sk.surface == u"impress"_ustr)
        appendImpressBlocks(sk.blocks, maxBlocks);
    else
    {
        appendWriterBlocks(sk.blocks, maxBlocks);
        if (sk.blocks.empty())
            appendCalcBlocks(sk.blocks, maxBlocks);
        if (sk.blocks.empty())
            appendImpressBlocks(sk.blocks, maxBlocks);
    }

    sk.blockCount = static_cast<sal_Int32>(sk.blocks.size());

    sal_Int32 chars = 0;
    for (const auto& b : sk.blocks)
        chars += b.preview.getLength();
    sk.statsLine = u"Full-text stats (preview basis): blocks="_ustr
                   + OUString::number(sk.blockCount) + u" preview-chars≈"_ustr
                   + OUString::number(chars);

    if (sel.text.isEmpty())
    {
        sk.selectionLine = u"No selection; position="_ustr
                           + (sel.position.isEmpty() ? u"(unknown)"_ustr : sel.position);
    }
    else
    {
        sk.selectionLine = u"Current selection: "_ustr
                           + (sel.position.isEmpty() ? u"(range)"_ustr : sel.position) + u" · "_ustr
                           + OUString::number(sel.length > 0 ? sel.length : sel.text.getLength())
                           + u" chars"_ustr;
    }

    OUStringBuffer hashSrc;
    hashSrc.append(sk.surface);
    hashSrc.append(u'|');
    hashSrc.append(sk.blockCount);
    for (const auto& b : sk.blocks)
    {
        hashSrc.append(u'|');
        hashSrc.append(b.index);
        hashSrc.append(u':');
        hashSrc.append(b.type);
        hashSrc.append(u':');
        hashSrc.append(b.position);
        hashSrc.append(u':');
        hashSrc.append(clip(b.preview, 40));
    }
    sk.snapshotHash = shortSha256(hashSrc.makeStringAndClear());

    const sal_Int32 preview = std::max<sal_Int32>(12, nPreviewChars);
    sk.formatted = formatSkeletonLines(sk, std::max<sal_Int32>(512, nMaxChars), preview,
                                       std::max<sal_Int32>(12, preview / 3));
    return sk;
}

DocumentToolReadResult DocumentAIDocumentTools::readBlocks(sal_Int32 nStartIndex,
                                                           sal_Int32 nEndIndex,
                                                           sal_Int32 nOffset,
                                                           sal_Int32 nMaxChars)
{
    DocumentToolReadResult r;
    const DocumentToolSkeleton sk = buildSkeleton(/*nMaxBlocks*/ 400, /*nMaxChars*/ 12000,
                                                  /*nPreviewChars*/ 80);
    if (!sk.hasDocument)
    {
        r.error = u"no open document"_ustr;
        r.summary = u"read_blocks · 无文档"_ustr;
        return r;
    }
    if (nStartIndex < 0 || nEndIndex < nStartIndex || nStartIndex >= sk.blockCount)
    {
        r.error = u"block index invalid or out of range (document has "_ustr
                  + OUString::number(sk.blockCount) + u" blocks)"_ustr;
        r.summary = u"read_blocks · 索引无效"_ustr;
        return r;
    }
    const sal_Int32 end = std::min(nEndIndex, sk.blockCount - 1);
    OUStringBuffer body;
    for (sal_Int32 i = nStartIndex; i <= end; ++i)
    {
        const auto& blk = sk.blocks[static_cast<size_t>(i)];
        OUString full;
        if (sk.surface == u"writer"_ustr && blk.position.startsWith(u"para:"_ustr))
        {
            const sal_Int32 para = blk.position.copy(5).toInt32();
            full = readWriterParagraph(para);
        }
        if (full.isEmpty())
            full = blk.preview;
        if (!body.isEmpty())
            body.append(u"\n\n"_ustr);
        body.append(u"--- block "_ustr + OUString::number(i) + u" "_ustr + blk.position
                    + u" ---\n"_ustr);
        body.append(full);
    }

    const OUString all = body.makeStringAndClear();
    const sal_Int32 off = std::max<sal_Int32>(0, nOffset);
    if (off > 0 && off >= all.getLength())
    {
        r.error = u"offset beyond content"_ustr;
        r.summary = u"read_blocks · offset 越界"_ustr;
        return r;
    }
    const sal_Int32 maxChars = std::max<sal_Int32>(256, nMaxChars);
    const OUString slice = all.copy(off, std::min(maxChars, all.getLength() - off));
    r.success = true;
    r.content = slice;
    r.truncated = (off + slice.getLength()) < all.getLength();
    r.nextOffset = off + slice.getLength();
    if (r.truncated)
        r.content += u"\n…(truncated: call read_blocks again with offset="_ustr
                     + OUString::number(r.nextOffset) + u")"_ustr;
    r.summary = u"read_blocks · "_ustr + OUString::number(nStartIndex) + u"-"_ustr
                + OUString::number(end);
    return r;
}

OUString DocumentAIDocumentTools::computeSnapshotHash()
{
    return buildSkeleton().snapshotHash;
}

void DocumentAIDocumentTools::markSeen(const OUString& rSnapshotHash)
{
    std::scoped_lock lock(seenMutex());
    seenHashStorage() = rSnapshotHash;
}

bool DocumentAIDocumentTools::isStale()
{
    std::scoped_lock lock(seenMutex());
    const OUString baseline = seenHashStorage();
    if (baseline.isEmpty())
        return false;
    const OUString now = buildSkeleton().snapshotHash;
    if (now.isEmpty())
        return false;
    return now != baseline;
}

void DocumentAIDocumentTools::clearSeen()
{
    std::scoped_lock lock(seenMutex());
    seenHashStorage().clear();
}

OUString DocumentAIDocumentTools::lastSeenHash()
{
    std::scoped_lock lock(seenMutex());
    return seenHashStorage();
}

OUString DocumentAIDocumentTools::buildDefaultContextBlock(sal_Int32 nMaxChars)
{
    const DocumentToolSkeleton sk = buildSkeleton(/*nMaxBlocks*/ 200, nMaxChars,
                                                  /*nPreviewChars*/ 60);
    if (!sk.hasDocument || sk.formatted.isEmpty())
        return OUString();
    return sk.formatted;
}

OUString DocumentAIDocumentTools::staleApplyErrorZh()
{
    return u"文档结构已在计划暂存后被用户修改，块索引/锚点可能过期。"
           u"主文档未改。请点「重新生成」按当前文档再跑一轮，或重新选区后再批准写回。"_ustr;
}

bool DocumentAIDocumentTools::isStaleApplyError(const OUString& rError)
{
    if (rError.isEmpty())
        return false;
    // Match Chinese stale message and evidence token (avoid bare "stale" false positives).
    return rError.indexOf(u"暂存后被用户修改"_ustr) >= 0
           || rError.indexOf(u"stale-document"_ustr) >= 0
           || rError.indexOf(u"stale-document-snapshot"_ustr) >= 0
           || rError.indexOf(u"结构已在计划"_ustr) >= 0
           || rError.indexOf(u"计划已过期"_ustr) >= 0;
}

OUString DocumentAIDocumentTools::classifyIntent(const OUString& rUserPrompt,
                                                 const OUString& rCapability,
                                                 bool bHasSelection)
{
    sal_Int32 dummyStart = 0;
    sal_Int32 dummyEnd = 0;
    if (parseExplicitBlockRange(rUserPrompt, dummyStart, dummyEnd))
        return u"read"_ustr;

    const OUString cap = rCapability.toAsciiLowerCase();
    if (cap == u"rewrite"_ustr || cap == u"quick-edit"_ustr || cap == u"rewrite-light"_ustr
        || cap == u"translate"_ustr || cap == u"expand"_ustr || cap == u"shorten"_ustr
        || cap == u"plan"_ustr || cap == u"polish"_ustr || cap == u"formal"_ustr
        || cap == u"continue"_ustr)
        return u"edit"_ustr;
    // Document structure / proofread / full summary: consult first (skeleton + optional read).
    if (cap == u"outline"_ustr || cap == u"proofread"_ustr || cap == u"review"_ustr
        || cap == u"doc-summary"_ustr)
        return u"consult"_ustr;

    const OUString low = rUserPrompt.toAsciiLowerCase();
    if (low.indexOf(u"read_blocks"_ustr) >= 0 || low.indexOf(u"读块"_ustr) >= 0
        || low.indexOf(u"全文"_ustr) >= 0 || low.indexOf(u"完整内容"_ustr) >= 0
        || low.indexOf(u"full text"_ustr) >= 0 || low.indexOf(u"read blocks"_ustr) >= 0)
        return u"read"_ustr;

    // Edit verbs (with or without selection — capability-less freeform).
    if (low.indexOf(u"改写"_ustr) >= 0 || low.indexOf(u"润色"_ustr) >= 0
        || low.indexOf(u"扩写"_ustr) >= 0 || low.indexOf(u"简写"_ustr) >= 0
        || low.indexOf(u"翻译"_ustr) >= 0 || low.indexOf(u"rewrite"_ustr) >= 0
        || low.indexOf(u"translate"_ustr) >= 0 || low.indexOf(u"polish"_ustr) >= 0
        || low.indexOf(u"缩短"_ustr) >= 0 || low.indexOf(u"加长"_ustr) >= 0
        || low.indexOf(u"替换"_ustr) >= 0 || low.indexOf(u"删掉"_ustr) >= 0
        || low.indexOf(u"删除"_ustr) >= 0 || low.indexOf(u"改成"_ustr) >= 0
        || low.indexOf(u"换成"_ustr) >= 0 || low.indexOf(u"replace"_ustr) >= 0
        || (bHasSelection
            && (low.indexOf(u"正式"_ustr) >= 0 || low.indexOf(u"语气"_ustr) >= 0)))
        return u"edit"_ustr;

    // Document assist verbs (Writer P0).
    if (low.indexOf(u"大纲"_ustr) >= 0 || low.indexOf(u"结构"_ustr) >= 0
        || low.indexOf(u"outline"_ustr) >= 0 || low.indexOf(u"审阅"_ustr) >= 0
        || low.indexOf(u"校对"_ustr) >= 0 || low.indexOf(u"proofread"_ustr) >= 0
        || low.indexOf(u"全文总结"_ustr) >= 0 || low.indexOf(u"doc-summary"_ustr) >= 0
        || low.startsWith(u"/outline"_ustr) || low.startsWith(u"/proofread"_ustr)
        || low.startsWith(u"/doc-summary"_ustr))
        return u"consult"_ustr;
    if (low.indexOf(u"续写"_ustr) >= 0 || low.startsWith(u"/continue"_ustr)
        || low.indexOf(u"continue writing"_ustr) >= 0)
        return u"edit"_ustr;

    // Calc assist (P1).
    if (low.indexOf(u"清洗"_ustr) >= 0 || low.indexOf(u"clean"_ustr) >= 0
        || low.startsWith(u"/clean"_ustr) || low.indexOf(u"解读"_ustr) >= 0
        || low.startsWith(u"/interpret"_ustr) || low.indexOf(u"interpret"_ustr) >= 0)
        return u"consult"_ustr;
    if (low.indexOf(u"公式"_ustr) >= 0 || low.startsWith(u"/formula"_ustr)
        || low.indexOf(u"formula"_ustr) >= 0 || low.startsWith(u"/aggregate"_ustr)
        || low.indexOf(u"汇总"_ustr) >= 0 || low.indexOf(u"sumif"_ustr) >= 0
        || low.indexOf(u"sum("_ustr) >= 0)
        return u"edit"_ustr;

    // Consult / Q&A style — do not invoke write tools.
    if (low.indexOf(u"什么"_ustr) >= 0 || low.indexOf(u"为什么"_ustr) >= 0
        || low.indexOf(u"怎么"_ustr) >= 0 || low.indexOf(u"如何"_ustr) >= 0
        || low.indexOf(u"解释"_ustr) >= 0 || low.indexOf(u"说明"_ustr) >= 0
        || low.indexOf(u"总结"_ustr) >= 0 || low.indexOf(u"概括"_ustr) >= 0
        || low.indexOf(u"有哪些"_ustr) >= 0 || low.indexOf(u"是否"_ustr) >= 0
        || low.indexOf(u"问"_ustr) >= 0 || low.indexOf(u"?"_ustr) >= 0
        || low.indexOf(u"？"_ustr) >= 0 || low.indexOf(u"what"_ustr) >= 0
        || low.indexOf(u"why"_ustr) >= 0 || low.indexOf(u"how"_ustr) >= 0
        || low.indexOf(u"explain"_ustr) >= 0 || low.indexOf(u"summar"_ustr) >= 0
        || low.indexOf(u"summarize"_ustr) >= 0 || low.indexOf(u"tell me"_ustr) >= 0
        || cap == u"chat"_ustr || cap == u"knowledge-query"_ustr)
        return u"consult"_ustr;

    if (bHasSelection)
        return u"unknown"_ustr;
    return u"consult"_ustr;
}

bool DocumentAIDocumentTools::wantsConsultIntent(const OUString& rUserPrompt,
                                                 const OUString& rCapability,
                                                 bool bHasSelection)
{
    return classifyIntent(rUserPrompt, rCapability, bHasSelection) == u"consult"_ustr;
}

bool DocumentAIDocumentTools::wantsEditIntent(const OUString& rUserPrompt,
                                              const OUString& rCapability,
                                              bool bHasSelection)
{
    return classifyIntent(rUserPrompt, rCapability, bHasSelection) == u"edit"_ustr;
}

bool DocumentAIDocumentTools::wantsLazyBlockRead(const OUString& rUserPrompt,
                                                 const OUString& rCapability,
                                                 bool bHasSelection)
{
    if (rUserPrompt.isEmpty() && rCapability.isEmpty())
        return false;

    sal_Int32 dummyStart = 0;
    sal_Int32 dummyEnd = 0;
    if (parseExplicitBlockRange(rUserPrompt, dummyStart, dummyEnd))
        return true;

    const OUString intent = classifyIntent(rUserPrompt, rCapability, bHasSelection);
    // M13: pure consult never lazy-reads (skeleton is enough); explicit range already returned.
    if (intent == u"consult"_ustr)
        return false;

    if (intent == u"edit"_ustr || intent == u"read"_ustr)
        return true;

    // Fallback: capability-driven rewrite slots.
    const OUString cap = rCapability.toAsciiLowerCase();
    if (cap == u"rewrite"_ustr || cap == u"quick-edit"_ustr || cap == u"rewrite-light"_ustr
        || cap == u"translate"_ustr || cap == u"expand"_ustr || cap == u"shorten"_ustr
        || cap == u"plan"_ustr || cap == u"polish"_ustr || cap == u"formal"_ustr)
        return true;

    const OUString low = rUserPrompt.toAsciiLowerCase();
    if (low.indexOf(u"read_blocks"_ustr) >= 0 || low.indexOf(u"读块"_ustr) >= 0
        || low.indexOf(u"全文"_ustr) >= 0 || low.indexOf(u"完整内容"_ustr) >= 0
        || low.indexOf(u"full text"_ustr) >= 0 || low.indexOf(u"read blocks"_ustr) >= 0)
        return true;

    if (bHasSelection)
    {
        if (low.indexOf(u"改写"_ustr) >= 0 || low.indexOf(u"润色"_ustr) >= 0
            || low.indexOf(u"扩写"_ustr) >= 0 || low.indexOf(u"简写"_ustr) >= 0
            || low.indexOf(u"翻译"_ustr) >= 0 || low.indexOf(u"rewrite"_ustr) >= 0
            || low.indexOf(u"translate"_ustr) >= 0 || low.indexOf(u"polish"_ustr) >= 0
            || low.indexOf(u"缩短"_ustr) >= 0 || low.indexOf(u"加长"_ustr) >= 0
            || low.indexOf(u"正式"_ustr) >= 0 || low.indexOf(u"语气"_ustr) >= 0)
            return true;
    }
    return false;
}

DocumentToolProposeResult DocumentAIDocumentTools::proposeReplaceBlocks(
    sal_Int32 nStartIndex, sal_Int32 nEndIndex, const OUString& rNewText,
    const OUString& rRationale)
{
    DocumentToolProposeResult out;
    out.mainDocumentMutation = false;
    out.opType = u"replace"_ustr;

    if (rNewText.trim().isEmpty())
    {
        out.message = u"propose-replace-failed reason=empty-new-text "
                      "main-document-mutation=false public-egress=false"_ustr;
        out.summary = u"document-tools · propose_replace_blocks · failed · empty-new-text · 主文档未改"_ustr;
        return out;
    }

    DocumentToolSkeleton sk = buildSkeleton();
    if (!sk.hasDocument || sk.blocks.empty())
    {
        out.message = u"propose-replace-failed reason=no-open-document "
                      "main-document-mutation=false public-egress=false"_ustr;
        out.summary = u"document-tools · propose_replace_blocks · failed · no-document · 主文档未改"_ustr;
        return out;
    }

    sal_Int32 start = nStartIndex;
    sal_Int32 end = nEndIndex;
    if (start < 0)
        start = 0;
    if (end < start)
        end = start;
    start = std::min(start, sk.blockCount - 1);
    end = std::min(end, sk.blockCount - 1);

    // Read current body for oldText / evidence (not applied).
    DocumentToolReadResult read = readBlocks(start, end, 0, 16000);
    OUString oldBody = read.success ? read.content : OUString();
    if (oldBody.isEmpty() && start < static_cast<sal_Int32>(sk.blocks.size()))
        oldBody = sk.blocks[static_cast<size_t>(start)].preview;

    const OUString target = sk.blocks[static_cast<size_t>(start)].position;
    if (target.isEmpty())
    {
        out.message = u"propose-replace-failed reason=no-position-anchor "
                      "main-document-mutation=false public-egress=false"_ustr;
        out.summary = u"document-tools · propose_replace_blocks · failed · no-anchor · 主文档未改"_ustr;
        return out;
    }

    // Bound proposal text (DoS / prompt dump).
    OUString newText = rNewText;
    constexpr sal_Int32 kMaxNew = 48000;
    if (newText.getLength() > kMaxNew)
        newText = newText.copy(0, kMaxNew) + u"…"_ustr;

    out.success = true;
    out.planId = u"ap-tool-propose-replace"_ustr;
    out.target = target;
    out.oldText = oldBody;
    out.newText = newText;
    out.snapshotHash = sk.snapshotHash;
    out.surface = sk.surface;
    out.previewSummaryZh
        = u"提议替换 "_ustr + target + u" · 新文本 "_ustr
          + OUString::number(newText.getLength()) + u" 字 · 须批准后写回"_ustr;
    out.summary = u"document-tools · propose_replace_blocks · "_ustr + target
                  + u" · blocks="_ustr + OUString::number(start) + u"-"_ustr
                  + OUString::number(end)
                  + u" · mainDocumentMutation=false · 主文档未改 · 无外传"_ustr;
    out.message = u"propose-replace-complete target="_ustr + target
                  + u" plan-id="_ustr + out.planId + u" snapshot="_ustr
                  + (out.snapshotHash.isEmpty() ? u"(none)"_ustr : out.snapshotHash)
                  + u" main-document-mutation=false staged-only=true "
                    "public-egress=false rationale="_ustr
                  + (rRationale.isEmpty() ? u"none"_ustr : rRationale);
    return out;
}

DocumentToolProposeResult
DocumentAIDocumentTools::proposeReplaceSelection(const OUString& rNewText,
                                                 const OUString& rRationale)
{
    DocumentToolProposeResult out;
    out.mainDocumentMutation = false;
    out.opType = u"replace"_ustr;

    if (rNewText.trim().isEmpty())
    {
        out.message = u"propose-replace-failed reason=empty-new-text "
                      "main-document-mutation=false public-egress=false"_ustr;
        out.summary = u"document-tools · propose_replace_selection · failed · empty-new-text · 主文档未改"_ustr;
        return out;
    }

    const auto sel = AgentChatSelectionCapture::captureCurrent();
    DocumentToolSkeleton sk = buildSkeleton();
    if (!sk.hasDocument)
    {
        out.message = u"propose-replace-failed reason=no-open-document "
                      "main-document-mutation=false public-egress=false"_ustr;
        out.summary = u"document-tools · propose_replace_selection · failed · no-document · 主文档未改"_ustr;
        return out;
    }

    sal_Int32 start = 0;
    sal_Int32 end = 0;
    bool have = false;
    if (!sel.position.isEmpty())
    {
        const sal_Int32 idx = findBlockIndexByPosition(sk, sel.position);
        if (idx >= 0)
        {
            start = idx;
            end = idx;
            have = true;
        }
    }
    if (!have && sk.blockCount > 0)
    {
        // Fallback: first block only when no selection map (still propose-only).
        start = 0;
        end = 0;
        have = true;
    }
    if (!have)
    {
        out.message = u"propose-replace-failed reason=no-target-block "
                      "main-document-mutation=false public-egress=false"_ustr;
        out.summary = u"document-tools · propose_replace_selection · failed · no-target · 主文档未改"_ustr;
        return out;
    }

    out = proposeReplaceBlocks(start, end, rNewText, rRationale);
    if (out.success && !sel.text.isEmpty())
    {
        // Prefer live selection text as oldText when available.
        out.oldText = sel.text;
        if (!sel.position.isEmpty())
            out.target = sel.position;
        out.summary = u"document-tools · propose_replace_selection · "_ustr + out.target
                      + u" · mainDocumentMutation=false · 主文档未改 · 无外传"_ustr;
    }
    return out;
}

sal_Int32 DocumentAIDocumentTools::findBlockIndexByPosition(const DocumentToolSkeleton& rSkeleton,
                                                            const OUString& rPosition)
{
    if (rPosition.isEmpty() || rSkeleton.blocks.empty())
        return -1;
    for (const auto& b : rSkeleton.blocks)
    {
        if (b.position == rPosition)
            return b.index;
    }
    // Soft match: para:3 vs para:03 not expected; cell:A1 prefix match for row tools.
    if (rPosition.startsWith(u"cell:"_ustr))
    {
        for (const auto& b : rSkeleton.blocks)
        {
            if (b.position.startsWith(u"cell:"_ustr)
                && b.position.indexOf(rPosition.copy(5)) >= 0)
                return b.index;
        }
    }
    return -1;
}

bool DocumentAIDocumentTools::parseExplicitBlockRange(const OUString& rUserPrompt,
                                                      sal_Int32& rStartOut, sal_Int32& rEndOut)
{
    rStartOut = 0;
    rEndOut = 0;
    const OUString p = rUserPrompt;
    if (p.isEmpty())
        return false;

    // read_blocks:1-3 / read_blocks 1-3 / blocks 2-4
    auto tryAsciiRange = [&](const OUString& token) -> bool {
        sal_Int32 idx = p.toAsciiLowerCase().indexOf(token);
        if (idx < 0)
            return false;
        sal_Int32 i = idx + token.getLength();
        while (i < p.getLength()
               && (p[i] == u' ' || p[i] == u':' || p[i] == u'=' || p[i] == u'['))
            ++i;
        if (i >= p.getLength() || p[i] < u'0' || p[i] > u'9')
            return false;
        sal_Int32 start = 0;
        while (i < p.getLength() && p[i] >= u'0' && p[i] <= u'9')
        {
            start = start * 10 + (p[i] - u'0');
            ++i;
        }
        sal_Int32 end = start;
        if (i < p.getLength() && (p[i] == u'-' || p[i] == u'~' || p[i] == u'—'))
        {
            ++i;
            end = 0;
            bool any = false;
            while (i < p.getLength() && p[i] >= u'0' && p[i] <= u'9')
            {
                any = true;
                end = end * 10 + (p[i] - u'0');
                ++i;
            }
            if (!any)
                end = start;
        }
        rStartOut = start;
        rEndOut = end;
        return true;
    };

    if (tryAsciiRange(u"read_blocks"_ustr) || tryAsciiRange(u"blocks"_ustr))
        return true;

    // 读第3-5段 / 读第3段 / 第2～4块
    sal_Int32 idx = p.indexOf(u"第"_ustr);
    while (idx >= 0 && idx < p.getLength())
    {
        sal_Int32 i = idx + 1;
        if (i >= p.getLength() || p[i] < u'0' || p[i] > u'9')
        {
            idx = p.indexOf(u"第"_ustr, idx + 1);
            continue;
        }
        sal_Int32 start = 0;
        while (i < p.getLength() && p[i] >= u'0' && p[i] <= u'9')
        {
            start = start * 10 + (p[i] - u'0');
            ++i;
        }
        sal_Int32 end = start;
        if (i < p.getLength() && (p[i] == u'-' || p[i] == u'~' || p[i] == u'—'))
        {
            ++i;
            end = 0;
            bool any = false;
            while (i < p.getLength() && p[i] >= u'0' && p[i] <= u'9')
            {
                any = true;
                end = end * 10 + (p[i] - u'0');
                ++i;
            }
            if (!any)
                end = start;
        }
        // Require 段/块/节 or end of nearby token so we don't grab random numbers.
        if (i < p.getLength()
            && (p[i] == u'段' || p[i] == u'块' || p[i] == u'节' || p[i] == u'页'))
        {
            // User-facing 第N段 is 1-based; convert to 0-based tool index.
            rStartOut = std::max<sal_Int32>(0, start - 1);
            rEndOut = std::max<sal_Int32>(0, end - 1);
            if (rEndOut < rStartOut)
                std::swap(rStartOut, rEndOut);
            return true;
        }
        idx = p.indexOf(u"第"_ustr, idx + 1);
    }
    return false;
}

DocumentToolPrepResult DocumentAIDocumentTools::prepareReadOnlyToolPass(
    const OUString& rUserPrompt, const OUString& rCapability,
    const DocumentToolSkeleton& rSkeleton, const OUString& rSelectionPosition,
    bool bHasSelection)
{
    DocumentToolPrepResult out;
    out.mainDocumentMutation = false;
    out.intent = classifyIntent(rUserPrompt, rCapability, bHasSelection);

    DocumentToolSkeleton sk = rSkeleton;
    // Bind may only carry formatted text; rebuild when the block table is missing.
    if (!sk.hasDocument || sk.blocks.empty())
        sk = buildSkeleton();
    if (!sk.hasDocument)
        return out;

    out.ranTools = true;
    out.snapshotHash = sk.snapshotHash;

    // Phase 1: get_document_context (always when document is open).
    {
        DocumentToolActivity a;
        a.phase = u"tool-running"_ustr;
        a.toolName = u"get_document_context"_ustr;
        a.summary = u"document-tools · get_document_context · blocks="_ustr
                    + OUString::number(sk.blockCount) + u" · snapshot="_ustr
                    + (sk.snapshotHash.isEmpty() ? u"(none)"_ustr : sk.snapshotHash)
                    + u" · intent="_ustr + out.intent
                    + u" · 无全文倾倒 · 主文档未改"_ustr;
        a.mutated = false;
        out.activities.push_back(a);
    }

    // M13: consult intent skips lazy read unless user asked for an explicit range.
    sal_Int32 explicitStart = 0;
    sal_Int32 explicitEnd = 0;
    const bool bExplicit = parseExplicitBlockRange(rUserPrompt, explicitStart, explicitEnd);
    if (out.intent == u"consult"_ustr && !bExplicit)
    {
        DocumentToolActivity a;
        a.phase = u"tool-running"_ustr;
        a.toolName = u"intent_classify"_ustr;
        a.summary = u"document-tools · intent=consult · 跳过写工具与懒读 · "
                    u"问询不调写回 · 主文档未改"_ustr;
        a.mutated = false;
        out.activities.push_back(a);
        out.statusLabel = u"文档工具：咨询模式 · 仅骨架 · 不写回 · 主文档未改"_ustr;
        return out;
    }

    const bool bNeedRead
        = wantsLazyBlockRead(rUserPrompt, rCapability, bHasSelection);
    if (!bNeedRead)
    {
        out.statusLabel = u"文档工具：骨架已就绪 · 无需懒读 · intent="_ustr + out.intent;
        return out;
    }

    sal_Int32 start = 0;
    sal_Int32 end = 0;
    bool haveRange = parseExplicitBlockRange(rUserPrompt, start, end);

    if (!haveRange && bHasSelection && !rSelectionPosition.isEmpty())
    {
        const sal_Int32 idx = findBlockIndexByPosition(sk, rSelectionPosition);
        if (idx >= 0)
        {
            start = idx;
            end = idx;
            haveRange = true;
        }
    }

    // Fallback: if rewrite intent but no selection map, read first few non-empty blocks.
    if (!haveRange && sk.blockCount > 0)
    {
        start = 0;
        end = std::min<sal_Int32>(sk.blockCount - 1, 2);
        haveRange = true;
    }

    if (!haveRange)
    {
        out.statusLabel = u"文档工具：需要懒读但无可用块索引"_ustr;
        DocumentToolActivity a;
        a.phase = u"tool-running"_ustr;
        a.toolName = u"read_blocks"_ustr;
        a.summary = u"document-tools · read_blocks · skipped reason=no-range · 主文档未改"_ustr;
        a.mutated = false;
        out.activities.push_back(a);
        return out;
    }

    // Clamp to skeleton size.
    if (start < 0)
        start = 0;
    if (end < start)
        end = start;
    if (sk.blockCount > 0)
    {
        start = std::min(start, sk.blockCount - 1);
        end = std::min(end, sk.blockCount - 1);
    }

    DocumentToolReadResult read = readBlocks(start, end, /*nOffset*/ 0, /*nMaxChars*/ 12000);
    {
        DocumentToolActivity a;
        a.phase = u"tool-running"_ustr;
        a.toolName = u"read_blocks"_ustr;
        if (read.success)
        {
            a.summary = u"document-tools · read_blocks · "_ustr + OUString::number(start)
                        + u"-"_ustr + OUString::number(end)
                        + (read.truncated ? u" · truncated=true"_ustr : u" · truncated=false"_ustr)
                        + u" · 主文档未改 · 无外传"_ustr;
        }
        else
        {
            a.summary = u"document-tools · read_blocks · failed · "_ustr
                        + (read.error.isEmpty() ? u"unknown"_ustr : read.error)
                        + u" · 主文档未改"_ustr;
        }
        a.mutated = false;
        out.activities.push_back(a);
    }

    if (read.success && !read.content.isEmpty())
    {
        OUStringBuffer inj;
        inj.append(u"【document-tools · read_blocks 结果 · 仅本地 · 禁止据此声称已改主文档】\n"_ustr);
        inj.append(u"range="_ustr + OUString::number(start) + u"-"_ustr
                   + OUString::number(end) + u" snapshot="_ustr + sk.snapshotHash + u"\n"_ustr);
        inj.append(read.content);
        inj.append(u"\n【纪律】预览截断时已用 read_blocks 拉全文；写回仍须 ApplyPlan + 用户批准。\n"_ustr);
        out.promptInjection = inj.makeStringAndClear();
        out.statusLabel = u"文档工具：骨架 + 懒读块 "_ustr + OUString::number(start) + u"-"_ustr
                          + OUString::number(end) + u" · 主文档未改"_ustr;
    }
    else
    {
        out.statusLabel = u"文档工具：懒读失败 · 仅骨架可用 · 主文档未改"_ustr;
    }

    return out;
}

OUString DocumentAIDocumentTools::buildMultiRoundToolProtocolNotice()
{
    // Text protocol works for Ollama + OpenAI-compatible without native function-calling.
    return u"【document-tools 多轮协议 · 本地只读 · 主文档不改】\n"
           u"骨架里的 preview 可能被截断。若需要某段全文，请先**只**输出一行（不要夹带改写结果）：\n"
           u"TOOL_REQUEST: read_blocks <start>-<end>\n"
           u"其中 start/end 为骨架中的 0-based index（含端点）。也可：\n"
           u"TOOL_REQUEST: read_blocks para:N   （Writer 1-based 段落）\n"
           u"或：TOOL_REQUEST: read_blocks 读第3-5段\n"
           u"收到 TOOL_RESULT 后，再输出最终答案 / 改写稿 / ApplyPlan。\n"
           u"纪律：禁止声称已改主文档；写回必须用户批准；不要请求写工具。\n"_ustr;
}

bool DocumentAIDocumentTools::shouldSkipMultiRoundForSelection(bool bHasSelection,
                                                               sal_Int32 nSelectionLen,
                                                               const OUString& rCapability,
                                                               const OUString& rUserPrompt)
{
    if (!bHasSelection || nSelectionLen < kSelectionCompleteMinChars)
        return false;

    // User explicitly asked for more blocks / full body → keep multi-round.
    sal_Int32 dummyStart = 0;
    sal_Int32 dummyEnd = 0;
    if (parseExplicitBlockRange(rUserPrompt, dummyStart, dummyEnd))
        return false;

    const OUString low = rUserPrompt.toAsciiLowerCase();
    if (low.indexOf(u"read_blocks"_ustr) >= 0 || low.indexOf(u"读块"_ustr) >= 0
        || low.indexOf(u"全文"_ustr) >= 0 || low.indexOf(u"完整内容"_ustr) >= 0
        || low.indexOf(u"full text"_ustr) >= 0 || low.indexOf(u"read blocks"_ustr) >= 0)
        return false;

    // Selection-complete path is for product edit verbs (rewrite/formal/shorten/expand).
    const OUString cap = rCapability.toAsciiLowerCase();
    if (cap == u"rewrite"_ustr || cap == u"quick-edit"_ustr || cap == u"polish"_ustr
        || cap == u"edit"_ustr || cap == u"formal"_ustr || cap == u"shorten"_ustr
        || cap == u"condense"_ustr || cap == u"expand"_ustr || cap == u"expand-write"_ustr
        || cap == u"translate"_ustr || cap == u"translation"_ustr || cap == u"paraphrase"_ustr)
        return true;

    // Freeform edit with selection (no explicit consult markers).
    if (wantsEditIntent(rUserPrompt, rCapability, bHasSelection))
        return true;

    return false;
}

bool DocumentAIDocumentTools::shouldUseLightSlotForSelectionEdit(bool bHasSelection,
                                                                 sal_Int32 nSelectionLen,
                                                                 const OUString& rCapability)
{
    if (!bHasSelection || nSelectionLen < kSelectionCompleteMinChars)
        return false;
    if (nSelectionLen > kSelectionQuickEditMaxChars)
        return false;

    const OUString cap = rCapability.toAsciiLowerCase();
    // Short selection rewrites / formal / polish prefer light (faster when light ≠ primary).
    return cap == u"rewrite"_ustr || cap == u"quick-edit"_ustr || cap == u"polish"_ustr
           || cap == u"formal"_ustr || cap == u"edit"_ustr || cap == u"shorten"_ustr
           || cap == u"condense"_ustr || cap == u"paraphrase"_ustr || cap == u"translate"_ustr
           || cap == u"translation"_ustr;
}

OUString DocumentAIDocumentTools::buildSelectionCompleteEditNotice(
    const OUString& rSelectionText, const OUString& rPosition, sal_Int32 nMaxChars)
{
    OUStringBuffer b;
    b.append(u"【选区全文快路径 · 禁止 TOOL_REQUEST · 主文档不改】\n"_ustr);
    b.append(u"用户已提供完整选区文本，无需再请求 read_blocks。\n"_ustr);
    if (!rPosition.isEmpty())
    {
        b.append(u"选区位置："_ustr);
        b.append(rPosition);
        b.append(u"\n"_ustr);
    }
    b.append(u"字数："_ustr);
    b.append(rSelectionText.getLength());
    b.append(u"\n--- selection (complete) ---\n"_ustr);
    if (nMaxChars > 0 && rSelectionText.getLength() > nMaxChars)
    {
        b.append(rSelectionText.copy(0, nMaxChars));
        b.append(u"…\n"_ustr);
    }
    else
        b.append(rSelectionText);
    b.append(u"\n--- end selection ---\n"_ustr);
    b.append(u"要求：直接输出最终改写稿 / ApplyPlan JSON；"
             u"**不要**输出 TOOL_REQUEST；禁止声称已改主文档；写回须用户批准。\n"_ustr);
    return b.makeStringAndClear();
}

namespace
{
OUString trimLine(const OUString& r)
{
    return r.trim();
}

bool startsWithIgnoreCase(const OUString& rText, const OUString& rPrefix)
{
    if (rText.getLength() < rPrefix.getLength())
        return false;
    return rText.copy(0, rPrefix.getLength()).equalsIgnoreAsciiCase(rPrefix);
}

bool parseIndexRangeToken(const OUString& rToken, sal_Int32& rStart, sal_Int32& rEnd)
{
    // Accept "0-2", "3", "0~2"
    const OUString t = rToken.trim();
    if (t.isEmpty())
        return false;
    sal_Int32 dash = t.indexOf(u'-');
    if (dash < 0)
        dash = t.indexOf(u'~');
    if (dash < 0)
        dash = t.indexOf(u'—');
    auto parseNum = [](const OUString& s, sal_Int32& out) -> bool {
        if (s.isEmpty())
            return false;
        sal_Int32 v = 0;
        for (sal_Int32 i = 0; i < s.getLength(); ++i)
        {
            const sal_Unicode c = s[i];
            if (c < u'0' || c > u'9')
                return false;
            v = v * 10 + (c - u'0');
        }
        out = v;
        return true;
    };
    if (dash < 0)
    {
        if (!parseNum(t, rStart))
            return false;
        rEnd = rStart;
        return true;
    }
    if (!parseNum(t.copy(0, dash).trim(), rStart))
        return false;
    if (!parseNum(t.copy(dash + 1).trim(), rEnd))
        return false;
    if (rEnd < rStart)
        std::swap(rStart, rEnd);
    return true;
}
} // namespace

DocumentToolRequest DocumentAIDocumentTools::parseModelToolRequest(const OUString& rModelText)
{
    DocumentToolRequest out;
    const OUString text = rModelText.trim();
    if (text.isEmpty())
        return out;

    // Scan line by line for a tool request marker.
    sal_Int32 pos = 0;
    while (pos <= text.getLength())
    {
        sal_Int32 nl = text.indexOf(u'\n', pos);
        const OUString line
            = trimLine(nl < 0 ? text.copy(pos) : text.copy(pos, nl - pos));
        pos = (nl < 0) ? text.getLength() + 1 : nl + 1;
        if (line.isEmpty())
            continue;

        OUString payload;
        if (startsWithIgnoreCase(line, u"TOOL_REQUEST:"_ustr))
            payload = line.copy(OUString(u"TOOL_REQUEST:"_ustr).getLength()).trim();
        else if (line.startsWith(u"工具请求:"_ustr))
            payload = line.copy(OUString(u"工具请求:"_ustr).getLength()).trim();
        else if (startsWithIgnoreCase(line, u"read_blocks:"_ustr))
            payload = line; // keep full line for range parse
        else
            continue;

        out.rawLine = line;
        // Normalize payload to start after optional tool name.
        OUString rest = payload;
        if (startsWithIgnoreCase(rest, u"read_blocks"_ustr))
        {
            rest = rest.copy(OUString(u"read_blocks"_ustr).getLength()).trim();
            if (rest.startsWith(u":"_ustr))
                rest = rest.copy(1).trim();
        }
        else if (rest.startsWith(u"读块"_ustr))
        {
            rest = rest.copy(OUString(u"读块"_ustr).getLength()).trim();
        }

        sal_Int32 start = 0;
        sal_Int32 end = 0;
        bool have = false;

        // Position tokens: para:N / slide:N / cell:A1
        if (rest.startsWith(u"para:"_ustr) || rest.startsWith(u"slide:"_ustr)
            || rest.startsWith(u"cell:"_ustr))
        {
            DocumentToolSkeleton sk = buildSkeleton();
            const sal_Int32 idx = findBlockIndexByPosition(sk, rest);
            if (idx >= 0)
            {
                start = idx;
                end = idx;
                have = true;
            }
            else
            {
                out.isToolRequest = true;
                out.toolName = u"read_blocks"_ustr;
                out.error = u"position-not-found:"_ustr + rest;
                return out;
            }
        }
        // Pure 0-based indices: "0-2", "3" (TOOL_REQUEST protocol default)
        else if (rest.indexOf(u'段') < 0 && rest.indexOf(u'块') < 0 && rest.indexOf(u'节') < 0
                 && rest.indexOf(u'页') < 0 && parseIndexRangeToken(rest, start, end))
        {
            have = true;
        }
        // Chinese / explicit forms: 读第3-5段, blocks 2-4, read_blocks:1-3 (1-based → 0-based)
        else if (parseExplicitBlockRange(rest, start, end)
                 || parseExplicitBlockRange(line, start, end)
                 || parseExplicitBlockRange(payload, start, end))
        {
            have = true;
        }

        if (!have)
        {
            out.isToolRequest = true;
            out.toolName = u"read_blocks"_ustr;
            out.error = u"unparsed-range"_ustr;
            return out;
        }

        out.isToolRequest = true;
        out.toolName = u"read_blocks"_ustr;
        out.startIndex = start;
        out.endIndex = end;
        if (out.endIndex < out.startIndex)
            std::swap(out.startIndex, out.endIndex);
        return out;
    }
    return out;
}

bool DocumentAIDocumentTools::isPrimarilyToolRequest(const OUString& rModelText)
{
    const DocumentToolRequest req = parseModelToolRequest(rModelText);
    if (!req.isToolRequest)
        return false;
    // If the reply is short and dominated by the tool line, treat as intermediate.
    const OUString t = rModelText.trim();
    if (t.getLength() <= 180)
        return true;
    // Long reply that only mentions TOOL_REQUEST near the top still counts.
    sal_Int32 firstNl = t.indexOf(u'\n');
    const OUString firstLine = (firstNl < 0 ? t : t.copy(0, firstNl)).trim();
    if (startsWithIgnoreCase(firstLine, u"TOOL_REQUEST:"_ustr)
        || firstLine.startsWith(u"工具请求:"_ustr)
        || startsWithIgnoreCase(firstLine, u"read_blocks:"_ustr))
    {
        // Allow a small amount of trailing prose; still intermediate if body is mostly request.
        return t.getLength() < 600;
    }
    return false;
}

DocumentToolRoundResult DocumentAIDocumentTools::executeReadOnlyToolRequest(
    const DocumentToolRequest& rRequest, const DocumentToolSkeleton& rSkeleton)
{
    DocumentToolRoundResult out;
    out.mainDocumentMutation = false;
    out.activity.phase = u"tool-running"_ustr;
    out.activity.toolName = rRequest.toolName.isEmpty() ? u"read_blocks"_ustr : rRequest.toolName;
    out.activity.mutated = false;

    if (!rRequest.isToolRequest)
    {
        out.activity.summary = u"document-tools · multi-round · skipped · not-a-tool-request · 主文档未改"_ustr;
        out.statusLabel = u"多轮工具：非工具请求 · 主文档未改"_ustr;
        return out;
    }
    if (!rRequest.error.isEmpty())
    {
        out.executed = true;
        out.activity.summary = u"document-tools · multi-round · read_blocks · failed · "_ustr
                               + rRequest.error + u" · 主文档未改"_ustr;
        out.toolResultBlock
            = u"【TOOL_RESULT · error · 主文档未改】\n"_ustr + rRequest.error + u"\n"_ustr;
        out.statusLabel = u"多轮工具：读块失败 · 主文档未改"_ustr;
        return out;
    }
    if (rRequest.toolName != u"read_blocks"_ustr)
    {
        out.executed = true;
        out.activity.summary = u"document-tools · multi-round · rejected tool="_ustr
                               + rRequest.toolName + u" · 仅允许 read_blocks · 主文档未改"_ustr;
        out.toolResultBlock = u"【TOOL_RESULT · error · 仅允许 read_blocks · 主文档未改】\n"_ustr;
        out.statusLabel = u"多轮工具：拒绝非只读工具 · 主文档未改"_ustr;
        return out;
    }

    DocumentToolSkeleton sk = rSkeleton;
    if (!sk.hasDocument || sk.blocks.empty())
        sk = buildSkeleton();
    if (!sk.hasDocument)
    {
        out.executed = true;
        out.activity.summary
            = u"document-tools · multi-round · read_blocks · no-document · 主文档未改"_ustr;
        out.toolResultBlock = u"【TOOL_RESULT · error · 无打开文档 · 主文档未改】\n"_ustr;
        out.statusLabel = u"多轮工具：无文档 · 主文档未改"_ustr;
        return out;
    }

    sal_Int32 start = rRequest.startIndex;
    sal_Int32 end = rRequest.endIndex;
    if (start < 0)
        start = 0;
    if (end < start)
        end = start;
    if (sk.blockCount > 0)
    {
        start = std::min(start, sk.blockCount - 1);
        end = std::min(end, sk.blockCount - 1);
    }

    DocumentToolReadResult read = readBlocks(start, end, /*nOffset*/ 0, /*nMaxChars*/ 12000);
    out.executed = true;
    if (read.success)
    {
        out.activity.summary = u"document-tools · multi-round · read_blocks · "_ustr
                               + OUString::number(start) + u"-"_ustr + OUString::number(end)
                               + (read.truncated ? u" · truncated=true"_ustr : u" · truncated=false"_ustr)
                               + u" · 主文档未改 · 无外传"_ustr;
        OUStringBuffer inj;
        inj.append(u"【TOOL_RESULT · read_blocks · 仅本地 · 禁止声称已改主文档】\n"_ustr);
        inj.append(u"range="_ustr + OUString::number(start) + u"-"_ustr + OUString::number(end)
                   + u" snapshot="_ustr + sk.snapshotHash + u"\n"_ustr);
        inj.append(read.content);
        inj.append(u"\n【纪律】以上为 TOOL_RESULT；请据此完成用户请求；写回仍须 ApplyPlan + 用户批准。\n"_ustr);
        out.toolResultBlock = inj.makeStringAndClear();
        out.statusLabel = u"多轮工具：已读块 "_ustr + OUString::number(start) + u"-"_ustr
                          + OUString::number(end) + u" · 主文档未改"_ustr;
    }
    else
    {
        out.activity.summary = u"document-tools · multi-round · read_blocks · failed · "_ustr
                               + (read.error.isEmpty() ? u"unknown"_ustr : read.error)
                               + u" · 主文档未改"_ustr;
        out.toolResultBlock = u"【TOOL_RESULT · error · 主文档未改】\n"_ustr
                              + (read.error.isEmpty() ? u"read failed"_ustr : read.error) + u"\n"_ustr;
        out.statusLabel = u"多轮工具：读块失败 · 主文档未改"_ustr;
    }
    return out;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
