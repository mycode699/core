/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AgentChatDiffApplier.
 *
 * V4 M2: Applies DiffOperations to the current document via UNO APIs.
 * Supports Writer (XTextDocument), Calc (XSpreadsheetDocument), and
 * Impress (XDrawPagesSupplier) with insert/delete/replace/format ops.
 * Target strings are parsed to locate the correct document position:
 *   Writer:     "para:3"        (1-based paragraph index)
 *   Calc:       "cell:B2"       (standard cell reference)
 *   Impress:    "slide:1:shape:2"  (1-based slide and shape index)
 */

#include <AgentChatDiffApplier.hxx>

#include <sal/log.hxx>
#include <rtl/ustrbuf.hxx>
#include <comphelper/processfactory.hxx>

#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/text/XParagraphCursor.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XDrawPages.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/presentation/XPresentationPage.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/awt/FontWeight.hpp>
#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>

#include <vector>

using namespace kqoffice::ai::chat;

namespace {

// ---------------------------------------------------------------------------
// Document model helpers
// ---------------------------------------------------------------------------

/// Get the current document model from the desktop.
css::uno::Reference<css::frame::XModel> getCurrentModel()
{
    try
    {
        auto desktop = css::frame::Desktop::create(
            comphelper::getProcessComponentContext());
        auto component = desktop->getCurrentComponent();
        if (!component.is())
        {
            SAL_WARN("kqoffice.ai.chat", "getCurrentComponent returned null");
            return nullptr;
        }
        css::uno::Reference<css::frame::XModel> model(component,
            css::uno::UNO_QUERY);
        return model;
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat",
                 "Failed to get current document: " << e.Message);
        return nullptr;
    }
}

/// Determine document type via UNO interfaces (works for unsaved docs).
OUString detectDocumentType(const css::uno::Reference<css::frame::XModel>& model)
{
    if (!model.is())
        return u""_ustr;
    try
    {
        if (css::uno::Reference<css::text::XTextDocument>(model, css::uno::UNO_QUERY).is())
            return u"writer"_ustr;
        if (css::uno::Reference<css::sheet::XSpreadsheetDocument>(model, css::uno::UNO_QUERY).is())
            return u"calc"_ustr;
        if (css::uno::Reference<css::drawing::XDrawPagesSupplier>(model, css::uno::UNO_QUERY).is())
            return u"impress"_ustr;
        const OUString url = model->getURL();
        if (url.endsWith(u".odt"_ustr) || url.endsWith(u".docx"_ustr) || url.endsWith(u".doc"_ustr))
            return u"writer"_ustr;
        if (url.endsWith(u".ods"_ustr) || url.endsWith(u".xlsx"_ustr) || url.endsWith(u".xls"_ustr))
            return u"calc"_ustr;
        if (url.endsWith(u".odp"_ustr) || url.endsWith(u".pptx"_ustr) || url.endsWith(u".ppt"_ustr))
            return u"impress"_ustr;
    }
    catch (const css::uno::Exception&)
    {
    }
    return u""_ustr;
}

// ---------------------------------------------------------------------------
// Target string parsers
// ---------------------------------------------------------------------------

bool isPositiveIntegerString(const OUString& s)
{
    if (s.isEmpty())
        return false;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        if (s[i] < '0' || s[i] > '9')
            return false;
    }
    return true;
}

bool isHexString(const OUString& s)
{
    if (s.isEmpty())
        return false;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

/// Parse "para:N" target into 0-based paragraph index. Returns -1 on failure.
sal_Int32 parseParaTarget(const OUString& target)
{
    if (!target.startsWith(u"para:"_ustr))
        return -1;
    const OUString numStr = target.copy(5);
    if (!isPositiveIntegerString(numStr))
        return -1;
    sal_Int32 idx = numStr.toInt32();
    if (idx < 1)
        return -1;
    return idx - 1; // convert 1-based to 0-based
}

/// Parse "cell:XX" target into {col, row} (0-based). Returns {-1,-1} on failure.
std::pair<sal_Int32, sal_Int32> parseCellTarget(const OUString& target)
{
    if (!target.startsWith(u"cell:"_ustr))
        return {-1, -1};
    const OUString cellRef = target.copy(5);
    if (cellRef.isEmpty())
        return {-1, -1};

    // Parse column letters (A=1, B=2, ..., Z=26, AA=27, ...)
    sal_Int32 col = 0;
    sal_Int32 i = 0;
    while (i < cellRef.getLength())
    {
        const sal_Unicode c = cellRef[i];
        if (c >= 'A' && c <= 'Z')
            col = col * 26 + (c - 'A' + 1);
        else if (c >= 'a' && c <= 'z')
            col = col * 26 + (c - 'a' + 1);
        else
            break;
        ++i;
    }
    if (col == 0)
        return {-1, -1};
    col -= 1; // convert 1-based to 0-based

    // Parse row number
    const OUString rowStr = cellRef.copy(i);
    if (rowStr.isEmpty())
        return {-1, -1};
    if (!isPositiveIntegerString(rowStr))
        return {-1, -1};
    sal_Int32 row = rowStr.toInt32();
    if (row < 1)
        return {-1, -1};
    row -= 1; // convert 1-based to 0-based

    return {col, row};
}

/// Parsed result for "slide:N:shape:M" targets.
struct SlideShapeTarget
{
    sal_Int32 slideIndex = -1;
    sal_Int32 shapeIndex = -1;
    bool valid = false;
};

/// Parse "slide:N:shape:M" or bare "slide:N" (whole-slide content) targets.
SlideShapeTarget parseSlideShapeTarget(const OUString& target)
{
    SlideShapeTarget result;
    const OUString prefix = u"slide:"_ustr;
    if (!target.startsWith(prefix))
        return result;
    const OUString rest = target.copy(prefix.getLength());
    const sal_Int32 colonPos = rest.indexOf(u":shape:"_ustr);
    if (colonPos < 0)
    {
        // Bare slide:N — create/ensure page and fill title+body shapes.
        if (!isPositiveIntegerString(rest))
            return result;
        const sal_Int32 si = rest.toInt32();
        if (si < 1)
            return result;
        result.slideIndex = si - 1;
        result.shapeIndex = -1; // sentinel: whole-slide content
        result.valid = true;
        return result;
    }
    const OUString slideStr = rest.copy(0, colonPos);
    const OUString shapeStr = rest.copy(colonPos + 7);
    if (slideStr.isEmpty() || shapeStr.isEmpty())
        return result;
    if (!isPositiveIntegerString(slideStr) || !isPositiveIntegerString(shapeStr))
        return result;
    const sal_Int32 si = slideStr.toInt32();
    const sal_Int32 sh = shapeStr.toInt32();
    if (si < 1 || sh < 1)
        return result;
    result.slideIndex = si - 1;
    result.shapeIndex = sh - 1;
    result.valid = true;
    return result;
}

/// Ensure draw pages exist up to (and including) 0-based slideIdx; returns the page.
css::uno::Reference<css::drawing::XDrawPage> ensureSlide(
    const css::uno::Reference<css::drawing::XDrawPagesSupplier>& supp, sal_Int32 slideIdx)
{
    auto xPages = supp->getDrawPages();
    if (!xPages.is() || slideIdx < 0)
        return nullptr;
    auto xIndex(css::uno::Reference<css::container::XIndexAccess>(xPages, css::uno::UNO_QUERY));
    if (!xIndex.is())
        return nullptr;
    while (xIndex->getCount() <= slideIdx)
    {
        // insert at end
        xPages->insertNewByIndex(xIndex->getCount());
        xIndex.set(xPages, css::uno::UNO_QUERY);
        if (!xIndex.is())
            return nullptr;
    }
    return css::uno::Reference<css::drawing::XDrawPage>(xIndex->getByIndex(slideIdx),
                                                         css::uno::UNO_QUERY);
}

/// Create title + body text shapes from multi-line content (first line = title).
/// Lines starting with 讲稿/备注/Notes are written to the notes page when available.
ApplyResult impressFillSlide(
    const css::uno::Reference<css::drawing::XDrawPagesSupplier>& supp, sal_Int32 slideIdx,
    const OUString& text)
{
    ApplyResult r;
    try
    {
        auto xPage = ensureSlide(supp, slideIdx);
        if (!xPage.is())
        {
            r.error = u"Impress: cannot ensure slide "_ustr + OUString::number(slideIdx + 1);
            return r;
        }
        auto xSMgr = comphelper::getProcessComponentContext()->getServiceManager();
        if (!xSMgr.is())
        {
            r.error = u"Impress: no service manager"_ustr;
            return r;
        }
        auto xShapes(css::uno::Reference<css::drawing::XShapes>(xPage, css::uno::UNO_QUERY));
        if (!xShapes.is())
        {
            r.error = u"Impress: page has no XShapes"_ustr;
            return r;
        }

        // Split body vs speaker notes (讲稿： / 备注： / Notes:)
        OUStringBuffer bodyBuf;
        OUStringBuffer notesBuf;
        sal_Int32 pos = 0;
        while (pos <= text.getLength())
        {
            sal_Int32 nl = text.indexOf(u'\n', pos);
            if (nl < 0)
                nl = text.getLength();
            OUString line = text.copy(pos, nl - pos);
            if (!line.isEmpty() && line[line.getLength() - 1] == u'\r')
                line = line.copy(0, line.getLength() - 1);
            const OUString t = line.trim();
            const bool isNote = t.startsWith(u"讲稿"_ustr) || t.startsWith(u"备注"_ustr)
                                || t.startsWith(u"Notes"_ustr) || t.startsWith(u"notes"_ustr)
                                || t.startsWith(u"Speaker"_ustr);
            if (isNote)
            {
                sal_Int32 c = t.indexOf(u'：');
                if (c < 0)
                    c = t.indexOf(u':');
                OUString note = (c >= 0) ? t.copy(c + 1).trim() : t;
                if (!note.isEmpty())
                {
                    if (!notesBuf.isEmpty())
                        notesBuf.append(u' ');
                    notesBuf.append(note);
                }
            }
            else
            {
                if (!bodyBuf.isEmpty())
                    bodyBuf.append(u'\n');
                bodyBuf.append(line);
            }
            if (nl >= text.getLength())
                break;
            pos = nl + 1;
        }

        const OUString notesText = notesBuf.makeStringAndClear().trim();

        // Strip layout / theme / image meta lines from body before layout.
        // 版式：标题页|标题内容|分栏|章节   主题：商务蓝|简洁灰
        // 配图：描述 → placeholder box
        OUString layout = u"title_body"_ustr;
        OUString theme;
        std::vector<OUString> imageHints;
        OUStringBuffer cleanBody;
        {
            const OUString rawBody = bodyBuf.makeStringAndClear();
            sal_Int32 p = 0;
            while (p <= rawBody.getLength())
            {
                sal_Int32 nl2 = rawBody.indexOf(u'\n', p);
                if (nl2 < 0)
                    nl2 = rawBody.getLength();
                OUString line = rawBody.copy(p, nl2 - p).trim();
                const OUString low = line.toAsciiLowerCase();
                if (line.startsWith(u"版式"_ustr) || low.startsWith(u"layout"_ustr))
                {
                    sal_Int32 c = line.indexOf(u'：');
                    if (c < 0)
                        c = line.indexOf(u':');
                    OUString v = (c >= 0) ? line.copy(c + 1).trim() : OUString();
                    const OUString vl = v.toAsciiLowerCase();
                    if (vl.indexOf(u"标题页"_ustr) >= 0 || vl.indexOf(u"title only"_ustr) >= 0
                        || vl.indexOf(u"cover"_ustr) >= 0)
                        layout = u"title"_ustr;
                    else if (vl.indexOf(u"分栏"_ustr) >= 0 || vl.indexOf(u"two"_ustr) >= 0
                             || vl.indexOf(u"双栏"_ustr) >= 0)
                        layout = u"two_column"_ustr;
                    else if (vl.indexOf(u"章节"_ustr) >= 0 || vl.indexOf(u"section"_ustr) >= 0)
                        layout = u"section"_ustr;
                    else
                        layout = u"title_body"_ustr;
                }
                else if (line.startsWith(u"主题"_ustr) || low.startsWith(u"theme"_ustr))
                {
                    sal_Int32 c = line.indexOf(u'：');
                    if (c < 0)
                        c = line.indexOf(u':');
                    theme = (c >= 0) ? line.copy(c + 1).trim() : line;
                }
                else if (line.startsWith(u"配图"_ustr) || low.startsWith(u"image"_ustr)
                         || line.startsWith(u"插图"_ustr))
                {
                    sal_Int32 c = line.indexOf(u'：');
                    if (c < 0)
                        c = line.indexOf(u':');
                    OUString hint = (c >= 0) ? line.copy(c + 1).trim() : line;
                    if (!hint.isEmpty())
                        imageHints.push_back(hint);
                }
                else if (!line.isEmpty())
                {
                    if (!cleanBody.isEmpty())
                        cleanBody.append(u'\n');
                    cleanBody.append(line);
                }
                if (nl2 >= rawBody.getLength())
                    break;
                p = nl2 + 1;
            }
        }

        const OUString bodyText = cleanBody.makeStringAndClear().trim();
        OUString title = bodyText;
        OUString body;
        const sal_Int32 nl = bodyText.indexOf(u'\n');
        if (nl >= 0)
        {
            title = bodyText.copy(0, nl).trim();
            body = bodyText.copy(nl + 1).trim();
        }
        title = title.trim();

        auto makeTextShape = [&](const OUString& content, sal_Int32 x, sal_Int32 y, sal_Int32 w,
                                 sal_Int32 h) {
            auto xShape(css::uno::Reference<css::drawing::XShape>(
                xSMgr->createInstanceWithContext(u"com.sun.star.drawing.TextShape"_ustr,
                                                 comphelper::getProcessComponentContext()),
                css::uno::UNO_QUERY));
            if (!xShape.is())
                return css::uno::Reference<css::drawing::XShape>();
            xShape->setSize(css::awt::Size(w, h));
            xShape->setPosition(css::awt::Point(x, y));
            auto xShapeText(css::uno::Reference<css::text::XText>(xShape, css::uno::UNO_QUERY));
            if (xShapeText.is())
                xShapeText->setString(content);
            xShapes->add(xShape);
            return xShape;
        };

        auto applyThemeTint = [&](const css::uno::Reference<css::drawing::XShape>& xShape) {
            if (!xShape.is() || theme.isEmpty())
                return;
            try
            {
                css::uno::Reference<css::beans::XPropertySet> xPS(xShape, css::uno::UNO_QUERY);
                if (!xPS.is())
                    return;
                // Soft fill for calm business themes (HMM-independent ARGB via Color)
                sal_Int32 color = 0x00F5F7FA; // default cool gray
                const OUString tl = theme.toAsciiLowerCase();
                if (tl.indexOf(u"蓝"_ustr) >= 0 || tl.indexOf(u"blue"_ustr) >= 0)
                    color = 0x00E8F1FB;
                else if (tl.indexOf(u"灰"_ustr) >= 0 || tl.indexOf(u"gray"_ustr) >= 0
                         || tl.indexOf(u"grey"_ustr) >= 0)
                    color = 0x00F0F0F0;
                else if (tl.indexOf(u"绿"_ustr) >= 0 || tl.indexOf(u"green"_ustr) >= 0)
                    color = 0x00EAF6EE;
                xPS->setPropertyValue(u"FillStyle"_ustr,
                                      css::uno::Any(css::drawing::FillStyle_SOLID));
                xPS->setPropertyValue(u"FillColor"_ustr, css::uno::Any(color));
            }
            catch (const css::uno::Exception&)
            {
            }
        };

        // Layouts in HMM (1/100 mm), premium-business calm spacing
        if (layout == u"title"_ustr)
        {
            if (!title.isEmpty())
            {
                auto sh = makeTextShape(title, 2000, 7000, 24000, 4000);
                applyThemeTint(sh);
            }
            if (!body.isEmpty())
                makeTextShape(body, 3000, 12000, 22000, 4000);
        }
        else if (layout == u"section"_ustr)
        {
            if (!title.isEmpty())
            {
                auto sh = makeTextShape(title, 2000, 8000, 24000, 3500);
                applyThemeTint(sh);
            }
        }
        else if (layout == u"two_column"_ustr)
        {
            if (!title.isEmpty())
                makeTextShape(title, 1000, 800, 25000, 2500);
            // Split body on blank line or middle
            OUString left = body;
            OUString right;
            const sal_Int32 mid = body.indexOf(u"\n\n");
            if (mid >= 0)
            {
                left = body.copy(0, mid).trim();
                right = body.copy(mid + 2).trim();
            }
            else
            {
                const sal_Int32 half = body.getLength() / 2;
                sal_Int32 split = body.indexOf(u'\n', half > 0 ? half : 0);
                if (split < 0)
                    split = half;
                if (split > 0 && split < body.getLength())
                {
                    left = body.copy(0, split).trim();
                    right = body.copy(split + 1).trim();
                }
            }
            if (!left.isEmpty())
                makeTextShape(left, 1000, 4000, 12000, 12000);
            if (!right.isEmpty())
                makeTextShape(right, 14000, 4000, 12000, 12000);
        }
        else
        {
            // title_body (default)
            if (!title.isEmpty())
            {
                auto sh = makeTextShape(title, 1000, 800, 25000, 2500);
                applyThemeTint(sh);
            }
            if (!body.isEmpty())
                makeTextShape(body, 1000, 4000, 25000, 12000);
        }

        // Image placeholders (local, no network) — text boxes user can replace with media
        sal_Int32 imgY = 14000;
        for (size_t ii = 0; ii < imageHints.size() && ii < 3; ++ii)
        {
            OUString label = u"【配图占位】"_ustr + imageHints[ii];
            auto sh = makeTextShape(label, 18000, imgY, 9000, 3500);
            applyThemeTint(sh);
            imgY += 3800;
        }

        // Speaker notes on notes page (best-effort; ignore failure)
        if (!notesText.isEmpty())
        {
            try
            {
                css::uno::Reference<css::presentation::XPresentationPage> xPres(xPage,
                                                                                css::uno::UNO_QUERY);
                if (xPres.is())
                {
                    css::uno::Reference<css::drawing::XDrawPage> xNotes = xPres->getNotesPage();
                    if (xNotes.is())
                    {
                        auto xNoteShapes(
                            css::uno::Reference<css::drawing::XShapes>(xNotes, css::uno::UNO_QUERY));
                        if (xNoteShapes.is())
                        {
                            auto xShape(css::uno::Reference<css::drawing::XShape>(
                                xSMgr->createInstanceWithContext(
                                    u"com.sun.star.drawing.TextShape"_ustr,
                                    comphelper::getProcessComponentContext()),
                                css::uno::UNO_QUERY));
                            if (xShape.is())
                            {
                                xShape->setSize(css::awt::Size(25000, 8000));
                                xShape->setPosition(css::awt::Point(1000, 14000));
                                auto xShapeText(css::uno::Reference<css::text::XText>(
                                    xShape, css::uno::UNO_QUERY));
                                if (xShapeText.is())
                                    xShapeText->setString(notesText);
                                xNoteShapes->add(xShape);
                            }
                        }
                    }
                }
            }
            catch (const css::uno::Exception&)
            {
            }
        }

        SAL_INFO("kqoffice.ai.chat",
                 "Impress fill slide=" << (slideIdx + 1) << " title=\"" << title << "\""
                                      << " layout=" << layout << " theme=" << theme
                                      << " images=" << imageHints.size()
                                      << " notes=" << !notesText.isEmpty());
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Impress fill slide failed: "_ustr + e.Message;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Writer (XTextDocument) helpers
// ---------------------------------------------------------------------------

/// Obtain an XParagraphCursor for a specific paragraph in Writer.
/// Returns null if the paragraph index is out of range.
css::uno::Reference<css::text::XTextCursor> getParaCursor(
    const css::uno::Reference<css::text::XText>& xText, sal_Int32 paraIdx)
{
    auto xCursor = xText->createTextCursor();
    if (!xCursor.is())
        return nullptr;
    auto xParaCursor(css::uno::Reference<css::text::XParagraphCursor>(
        xCursor, css::uno::UNO_QUERY));
    if (!xParaCursor.is())
        return nullptr;
    // Navigate to the beginning of the document
    xParaCursor->gotoStart(false);
    // Advance to the target paragraph
    for (sal_Int32 i = 0; i < paraIdx; ++i)
    {
        if (!xParaCursor->gotoNextParagraph(false))
            return nullptr;
    }
    return xCursor;
}

ApplyResult writerInsert(
    const css::uno::Reference<css::text::XTextDocument>& doc,
    const OUString& target, const OUString& text)
{
    ApplyResult r;
    try
    {
        auto xText = doc->getText();

        const sal_Int32 paraIdx = parseParaTarget(target);
        if (paraIdx < 0)
        {
            // No para:N target -- insert at the document end
            auto xCursor = xText->createTextCursor();
            xCursor->gotoEnd(false);
            xText->insertString(xCursor, text, false);
            SAL_INFO("kqoffice.ai.chat",
                     "Writer insert at document end: text=\""
                         << text << "\"");
        }
        else
        {
            auto xCursor = getParaCursor(xText, paraIdx);
            if (!xCursor.is())
            {
                r.error = u"Writer insert: paragraph "_ustr
                    + OUString::number(paraIdx + 1) + u" not found"_ustr;
                return r;
            }
            xText->insertString(xCursor, text, false);
            SAL_INFO("kqoffice.ai.chat",
                     "Writer insert para=" << (paraIdx + 1)
                         << " text=\"" << text << "\"");
        }
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Writer insert failed: "_ustr + e.Message;
    }
    return r;
}

ApplyResult writerDelete(
    const css::uno::Reference<css::text::XTextDocument>& doc,
    const OUString& target)
{
    ApplyResult r;
    try
    {
        auto xText = doc->getText();
        const sal_Int32 paraIdx = parseParaTarget(target);
        if (paraIdx < 0)
        {
            r.error = u"Writer delete: target must be para:N"_ustr;
            return r;
        }

        auto xCursor = getParaCursor(xText, paraIdx);
        if (!xCursor.is())
        {
            r.error = u"Writer delete: paragraph "_ustr
                + OUString::number(paraIdx + 1) + u" not found"_ustr;
            return r;
        }

        // Select the full paragraph text, then clear it
        auto xParaCursor(css::uno::Reference<css::text::XParagraphCursor>(
            xCursor, css::uno::UNO_QUERY));
        if (xParaCursor.is())
            xParaCursor->gotoEndOfParagraph(true);

        xCursor->setString(u""_ustr);
        SAL_INFO("kqoffice.ai.chat",
                 "Writer delete para=" << (paraIdx + 1));
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Writer delete failed: "_ustr + e.Message;
    }
    return r;
}

ApplyResult writerReplace(
    const css::uno::Reference<css::text::XTextDocument>& doc,
    const OUString& target, const OUString& newText)
{
    ApplyResult r;
    try
    {
        auto xText = doc->getText();

        const sal_Int32 paraIdx = parseParaTarget(target);
        if (paraIdx < 0)
        {
            r.error = u"Writer replace: target must be para:N"_ustr;
            return r;
        }

        auto xCursor = getParaCursor(xText, paraIdx);
        if (!xCursor.is())
        {
            r.error = u"Writer replace: paragraph "_ustr
                + OUString::number(paraIdx + 1) + u" not found"_ustr;
            return r;
        }

        // Select the full paragraph and replace
        auto xParaCursor(css::uno::Reference<css::text::XParagraphCursor>(
            xCursor, css::uno::UNO_QUERY));
        if (xParaCursor.is())
            xParaCursor->gotoEndOfParagraph(true);

        xCursor->setString(newText);
        SAL_INFO("kqoffice.ai.chat",
                 "Writer replace para=" << (paraIdx + 1)
                     << " newText=\"" << newText << "\"");
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Writer replace failed: "_ustr + e.Message;
    }
    return r;
}

ApplyResult writerFormat(
    const css::uno::Reference<css::text::XTextDocument>& doc,
    const OUString& target, const OUString& formatSpec)
{
    ApplyResult r;
    try
    {
        auto xText = doc->getText();

        const sal_Int32 paraIdx = parseParaTarget(target);
        if (paraIdx < 0)
        {
            r.error = u"Writer format: target must be para:N"_ustr;
            return r;
        }

        auto xCursor = getParaCursor(xText, paraIdx);
        if (!xCursor.is())
        {
            r.error = u"Writer format: paragraph "_ustr
                + OUString::number(paraIdx + 1) + u" not found"_ustr;
            return r;
        }

        auto xProps(css::uno::Reference<css::beans::XPropertySet>(
            xCursor, css::uno::UNO_QUERY));
        if (!xProps.is())
        {
            r.error = u"Writer format: cannot get XPropertySet"_ustr;
            return r;
        }

        // Parse format spec: simple substring matching on a JSON-like string
        if (formatSpec.indexOf(u"bold"_ustr) >= 0)
            xProps->setPropertyValue(u"CharWeight"_ustr,
                css::uno::Any(css::awt::FontWeight::BOLD));

        if (formatSpec.indexOf(u"italic"_ustr) >= 0)
            xProps->setPropertyValue(u"CharPosture"_ustr,
                css::uno::Any(css::awt::FontSlant_ITALIC));

        if (formatSpec.indexOf(u"underline"_ustr) >= 0)
            xProps->setPropertyValue(u"CharUnderline"_ustr,
                css::uno::Any(static_cast<sal_Int16>(1))); // SINGLE

        // Extract font size value from "fontSize":<number>
        const sal_Int32 fsKeyPos = formatSpec.indexOf(u"fontSize"_ustr);
        if (fsKeyPos >= 0)
        {
            const sal_Int32 colonPos = formatSpec.indexOf(':', fsKeyPos);
            if (colonPos >= 0)
            {
                // Scan forward past the colon and whitespace to find the number
                sal_Int32 numStart = colonPos + 1;
                while (numStart < formatSpec.getLength()
                       && formatSpec[numStart] == ' ')
                    ++numStart;

                // Collect digits and decimal point
                sal_Int32 numEnd = numStart;
                while (numEnd < formatSpec.getLength()
                       && ((formatSpec[numEnd] >= '0' && formatSpec[numEnd] <= '9')
                           || formatSpec[numEnd] == '.'))
                    ++numEnd;

                if (numEnd > numStart)
                {
                    const OUString fsStr = formatSpec.copy(numStart, numEnd - numStart);
                    const float fs = fsStr.toFloat();
                    if (fs > 0.0f)
                        xProps->setPropertyValue(u"CharHeight"_ustr,
                            css::uno::Any(static_cast<float>(fs)));
                }
            }
        }

        SAL_INFO("kqoffice.ai.chat",
                 "Writer format para=" << (paraIdx + 1)
                     << " spec=\"" << formatSpec << "\"");
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Writer format failed: "_ustr + e.Message;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Calc (XSpreadsheetDocument) helpers
// ---------------------------------------------------------------------------

/// Get the first (active) spreadsheet from a Calc document.
css::uno::Reference<css::sheet::XSpreadsheet> getActiveSheet(
    const css::uno::Reference<css::sheet::XSpreadsheetDocument>& doc)
{
    auto xSheets = doc->getSheets();
    auto xIndex(css::uno::Reference<css::container::XIndexAccess>(
        xSheets, css::uno::UNO_QUERY));
    if (!xIndex.is() || xIndex->getCount() < 1)
        return nullptr;
    return css::uno::Reference<css::sheet::XSpreadsheet>(
        xIndex->getByIndex(0), css::uno::UNO_QUERY);
}

ApplyResult calcInsert(
    const css::uno::Reference<css::sheet::XSpreadsheetDocument>& doc,
    const OUString& target, const OUString& text)
{
    ApplyResult r;
    try
    {
        auto sheet = getActiveSheet(doc);
        if (!sheet.is())
        {
            r.error = u"Calc insert: no active sheet"_ustr;
            return r;
        }
        const auto [col, row] = parseCellTarget(target);
        if (col < 0 || row < 0)
        {
            r.error = u"Calc insert: invalid target \""_ustr + target
                          + u"\"; expected cell:XX"_ustr;
            return r;
        }
        auto xCell = sheet->getCellByPosition(col, row);
        xCell->setFormula(text);
        SAL_INFO("kqoffice.ai.chat",
                 "Calc insert cell=" << target
                     << " formula=\"" << text << "\"");
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Calc insert failed: "_ustr + e.Message;
    }
    return r;
}

ApplyResult calcDelete(
    const css::uno::Reference<css::sheet::XSpreadsheetDocument>& doc,
    const OUString& target)
{
    ApplyResult r;
    try
    {
        auto sheet = getActiveSheet(doc);
        if (!sheet.is())
        {
            r.error = u"Calc delete: no active sheet"_ustr;
            return r;
        }
        const auto [col, row] = parseCellTarget(target);
        if (col < 0 || row < 0)
        {
            r.error = u"Calc delete: invalid target \""_ustr + target
                          + u"\"; expected cell:XX"_ustr;
            return r;
        }
        auto xCell = sheet->getCellByPosition(col, row);
        xCell->setFormula(u""_ustr);
        SAL_INFO("kqoffice.ai.chat",
                 "Calc delete cell=" << target);
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Calc delete failed: "_ustr + e.Message;
    }
    return r;
}

ApplyResult calcReplace(
    const css::uno::Reference<css::sheet::XSpreadsheetDocument>& doc,
    const OUString& target, const OUString& newText)
{
    // Replace has the same semantics as insert for cells: setFormula
    return calcInsert(doc, target, newText);
}

ApplyResult calcFormat(
    const css::uno::Reference<css::sheet::XSpreadsheetDocument>& doc,
    const OUString& target, const OUString& formatSpec)
{
    ApplyResult r;
    try
    {
        auto sheet = getActiveSheet(doc);
        if (!sheet.is())
        {
            r.error = u"Calc format: no active sheet"_ustr;
            return r;
        }
        const auto [col, row] = parseCellTarget(target);
        if (col < 0 || row < 0)
        {
            r.error = u"Calc format: invalid target \""_ustr + target
                          + u"\"; expected cell:XX"_ustr;
            return r;
        }
        auto xCell = sheet->getCellByPosition(col, row);
        auto xProps(css::uno::Reference<css::beans::XPropertySet>(
            xCell, css::uno::UNO_QUERY));
        if (!xProps.is())
        {
            r.error = u"Calc format: cannot get XPropertySet"_ustr;
            return r;
        }

        if (formatSpec.indexOf(u"bold"_ustr) >= 0)
            xProps->setPropertyValue(u"CharWeight"_ustr,
                css::uno::Any(css::awt::FontWeight::BOLD));

        if (formatSpec.indexOf(u"italic"_ustr) >= 0)
            xProps->setPropertyValue(u"CharPosture"_ustr,
                css::uno::Any(css::awt::FontSlant_ITALIC));

        if (formatSpec.indexOf(u"CellBackColor"_ustr) >= 0)
        {
            const sal_Int32 keyPos = formatSpec.indexOf(u"CellBackColor"_ustr);
            const sal_Int32 colonPos = formatSpec.indexOf(':', keyPos);
            if (colonPos >= 0)
            {
                // Simple hex color parsing after "CellBackColor": "RRGGBB"
                sal_Int32 qStart = colonPos + 1;
                while (qStart < formatSpec.getLength()
                       && (formatSpec[qStart] == ' ' || formatSpec[qStart] == '"'))
                    ++qStart;
                if (qStart + 6 <= formatSpec.getLength())
                {
                    const OUString hexStr = formatSpec.copy(qStart, 6).toAsciiUpperCase();
                    if (isHexString(hexStr))
                    {
                        const sal_Int32 colorVal = hexStr.toInt32(16);
                        // UNO color is BGR (0x00BBGGRR), hex is RRGGBB
                        const sal_Int32 rv = (colorVal >> 16) & 0xFF;
                        const sal_Int32 gv = (colorVal >> 8) & 0xFF;
                        const sal_Int32 bv = colorVal & 0xFF;
                        const sal_Int32 bgrColor = (bv << 16) | (gv << 8) | rv;
                        xProps->setPropertyValue(u"CellBackColor"_ustr,
                            css::uno::Any(bgrColor));
                    }
                }
            }
        }

        SAL_INFO("kqoffice.ai.chat",
                 "Calc format cell=" << target
                     << " spec=\"" << formatSpec << "\"");
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Calc format failed: "_ustr + e.Message;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Impress / Draw (XDrawPagesSupplier) helpers
// ---------------------------------------------------------------------------

/// Get an XDrawPage by 0-based index from an XDrawPagesSupplier.
css::uno::Reference<css::drawing::XDrawPage> getSlideByIndex(
    const css::uno::Reference<css::drawing::XDrawPagesSupplier>& supp,
    sal_Int32 slideIdx)
{
    auto xPages = supp->getDrawPages();
    auto xIndex(css::uno::Reference<css::container::XIndexAccess>(
        xPages, css::uno::UNO_QUERY));
    if (!xIndex.is() || slideIdx < 0 || slideIdx >= xIndex->getCount())
        return nullptr;
    return css::uno::Reference<css::drawing::XDrawPage>(
        xIndex->getByIndex(slideIdx), css::uno::UNO_QUERY);
}

ApplyResult impressInsert(
    const css::uno::Reference<css::drawing::XDrawPagesSupplier>& supp,
    const OUString& target, const OUString& text)
{
    ApplyResult r;
    try
    {
        const auto stgt = parseSlideShapeTarget(target);
        if (!stgt.valid)
        {
            r.error = u"Impress insert: invalid target \""_ustr + target
                + u"\"; expected slide:N or slide:N:shape:M"_ustr;
            return r;
        }

        // Whole-slide content path (outline-to-slides)
        if (stgt.shapeIndex < 0)
            return impressFillSlide(supp, stgt.slideIndex, text);

        auto xPage = getSlideByIndex(supp, stgt.slideIndex);
        if (!xPage.is())
            xPage = ensureSlide(supp, stgt.slideIndex);
        if (!xPage.is())
        {
            r.error = u"Impress insert: slide "_ustr
                          + OUString::number(stgt.slideIndex + 1) + u" not found"_ustr;
            return r;
        }

        // Get multi-service factory from the component context
        auto xSMgr = comphelper::getProcessComponentContext()->getServiceManager();
        if (!xSMgr.is())
        {
            r.error = u"Impress insert: no service manager"_ustr;
            return r;
        }

        // Create a TextShape
        auto xShape(css::uno::Reference<css::drawing::XShape>(
            xSMgr->createInstanceWithContext(
                u"com.sun.star.drawing.TextShape"_ustr,
                comphelper::getProcessComponentContext()),
            css::uno::UNO_QUERY));
        if (!xShape.is())
        {
            r.error = u"Impress insert: failed to create TextShape"_ustr;
            return r;
        }

        // Default size and position (in 1/100 mm)
        xShape->setSize(css::awt::Size(20000, 5000));
        xShape->setPosition(css::awt::Point(5000, 5000));

        // Set text content
        auto xShapeText(css::uno::Reference<css::text::XText>(
            xShape, css::uno::UNO_QUERY));
        if (xShapeText.is())
            xShapeText->setString(text);

        // Add shape to the slide
        auto xShapes(css::uno::Reference<css::drawing::XShapes>(
            xPage, css::uno::UNO_QUERY));
        if (xShapes.is())
            xShapes->add(xShape);

        SAL_INFO("kqoffice.ai.chat",
                 "Impress insert slide=" << (stgt.slideIndex + 1)
                     << " text=\"" << text << "\"");
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Impress insert failed: "_ustr + e.Message;
    }
    return r;
}

ApplyResult impressDelete(
    const css::uno::Reference<css::drawing::XDrawPagesSupplier>& supp,
    const OUString& target)
{
    ApplyResult r;
    try
    {
        const auto stgt = parseSlideShapeTarget(target);
        if (!stgt.valid)
        {
            r.error = u"Impress delete: invalid target \""_ustr + target
                + u"\"; expected slide:N:shape:M"_ustr;
            return r;
        }

        auto xPage = getSlideByIndex(supp, stgt.slideIndex);
        if (!xPage.is())
        {
            r.error = u"Impress delete: slide "_ustr
                          + OUString::number(stgt.slideIndex + 1) + u" not found"_ustr;
            return r;
        }

        auto xIndex(css::uno::Reference<css::container::XIndexAccess>(
            xPage, css::uno::UNO_QUERY));
        if (!xIndex.is() || stgt.shapeIndex >= xIndex->getCount())
        {
            r.error = u"Impress delete: shape "_ustr + OUString::number(stgt.shapeIndex)
                          + u" out of range on slide "_ustr
                          + OUString::number(stgt.slideIndex + 1);
            return r;
        }

        auto xShape(css::uno::Reference<css::drawing::XShape>(
            xIndex->getByIndex(stgt.shapeIndex), css::uno::UNO_QUERY));
        if (xShape.is())
        {
            auto xShapes(css::uno::Reference<css::drawing::XShapes>(
                xPage, css::uno::UNO_QUERY));
            if (xShapes.is())
                xShapes->remove(xShape);
        }

        SAL_INFO("kqoffice.ai.chat",
                 "Impress delete slide=" << (stgt.slideIndex + 1)
                     << " shape=" << (stgt.shapeIndex));
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Impress delete failed: "_ustr + e.Message;
    }
    return r;
}

ApplyResult impressReplace(
    const css::uno::Reference<css::drawing::XDrawPagesSupplier>& supp,
    const OUString& target, const OUString& newText)
{
    ApplyResult r;
    try
    {
        const auto stgt = parseSlideShapeTarget(target);
        if (!stgt.valid)
        {
            r.error = u"Impress replace: invalid target \""_ustr + target
                + u"\"; expected slide:N or slide:N:shape:M"_ustr;
            return r;
        }

        if (stgt.shapeIndex < 0)
            return impressFillSlide(supp, stgt.slideIndex, newText);

        auto xPage = getSlideByIndex(supp, stgt.slideIndex);
        if (!xPage.is())
        {
            r.error = u"Impress replace: slide "_ustr
                          + OUString::number(stgt.slideIndex + 1) + u" not found"_ustr;
            return r;
        }

        auto xIndex(css::uno::Reference<css::container::XIndexAccess>(
            xPage, css::uno::UNO_QUERY));
        if (!xIndex.is() || stgt.shapeIndex >= xIndex->getCount())
        {
            r.error = u"Impress replace: shape "_ustr + OUString::number(stgt.shapeIndex)
                          + u" out of range on slide "_ustr
                          + OUString::number(stgt.slideIndex + 1);
            return r;
        }

        auto xShape(css::uno::Reference<css::drawing::XShape>(
            xIndex->getByIndex(stgt.shapeIndex), css::uno::UNO_QUERY));
        auto xShapeText(css::uno::Reference<css::text::XText>(
            xShape, css::uno::UNO_QUERY));
        if (!xShapeText.is())
        {
            r.error = u"Impress replace: shape does not support text"_ustr;
            return r;
        }

        xShapeText->setString(newText);
        SAL_INFO("kqoffice.ai.chat",
                 "Impress replace slide=" << (stgt.slideIndex + 1)
                     << " shape=" << (stgt.shapeIndex)
                     << " newText=\"" << newText << "\"");
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Impress replace failed: "_ustr + e.Message;
    }
    return r;
}

ApplyResult impressFormat(
    const css::uno::Reference<css::drawing::XDrawPagesSupplier>& supp,
    const OUString& target, const OUString& formatSpec)
{
    ApplyResult r;
    try
    {
        const auto stgt = parseSlideShapeTarget(target);
        if (!stgt.valid)
        {
            r.error = u"Impress format: invalid target \""_ustr + target
                + u"\"; expected slide:N:shape:M"_ustr;
            return r;
        }

        auto xPage = getSlideByIndex(supp, stgt.slideIndex);
        if (!xPage.is())
        {
            r.error = u"Impress format: slide "_ustr
                          + OUString::number(stgt.slideIndex + 1) + u" not found"_ustr;
            return r;
        }

        auto xIndex(css::uno::Reference<css::container::XIndexAccess>(
            xPage, css::uno::UNO_QUERY));
        if (!xIndex.is() || stgt.shapeIndex >= xIndex->getCount())
        {
            r.error = u"Impress format: shape "_ustr + OUString::number(stgt.shapeIndex)
                          + u" out of range on slide "_ustr
                          + OUString::number(stgt.slideIndex + 1);
            return r;
        }

        auto xShape(css::uno::Reference<css::drawing::XShape>(
            xIndex->getByIndex(stgt.shapeIndex), css::uno::UNO_QUERY));
        auto xProps(css::uno::Reference<css::beans::XPropertySet>(
            xShape, css::uno::UNO_QUERY));
        if (!xProps.is())
        {
            r.error = u"Impress format: cannot get XPropertySet"_ustr;
            return r;
        }

        if (formatSpec.indexOf(u"FillColor"_ustr) >= 0)
        {
            // Try to extract a hex color from the format spec
            const sal_Int32 keyPos = formatSpec.indexOf(u"FillColor"_ustr);
            const sal_Int32 colonPos = formatSpec.indexOf(':', keyPos);
            if (colonPos >= 0)
            {
                sal_Int32 qStart = colonPos + 1;
                while (qStart < formatSpec.getLength()
                       && (formatSpec[qStart] == ' ' || formatSpec[qStart] == '"'))
                    ++qStart;
                if (qStart + 6 <= formatSpec.getLength())
                {
                    const OUString hexStr = formatSpec.copy(qStart, 6).toAsciiUpperCase();
                    if (isHexString(hexStr))
                    {
                        const sal_Int32 colorVal = hexStr.toInt32(16);
                        // UNO color is BGR (0x00BBGGRR), hex is RRGGBB
                        const sal_Int32 red = (colorVal >> 16) & 0xFF;
                        const sal_Int32 grn = (colorVal >> 8) & 0xFF;
                        const sal_Int32 blu = colorVal & 0xFF;
                        const sal_Int32 bgrColor = (blu << 16) | (grn << 8) | red;
                        xProps->setPropertyValue(u"FillColor"_ustr,
                            css::uno::Any(bgrColor));
                        xProps->setPropertyValue(u"FillStyle"_ustr,
                            css::uno::Any(css::drawing::FillStyle_SOLID));
                    }
                }
            }
        }

        if (formatSpec.indexOf(u"LineColor"_ustr) >= 0)
        {
            const sal_Int32 keyPos = formatSpec.indexOf(u"LineColor"_ustr);
            const sal_Int32 colonPos = formatSpec.indexOf(':', keyPos);
            if (colonPos >= 0)
            {
                sal_Int32 qStart = colonPos + 1;
                while (qStart < formatSpec.getLength()
                       && (formatSpec[qStart] == ' ' || formatSpec[qStart] == '"'))
                    ++qStart;
                if (qStart + 6 <= formatSpec.getLength())
                {
                    const OUString hexStr = formatSpec.copy(qStart, 6).toAsciiUpperCase();
                    if (isHexString(hexStr))
                    {
                        const sal_Int32 colorVal = hexStr.toInt32(16);
                        const sal_Int32 red = (colorVal >> 16) & 0xFF;
                        const sal_Int32 grn = (colorVal >> 8) & 0xFF;
                        const sal_Int32 blu = colorVal & 0xFF;
                        const sal_Int32 bgrColor = (blu << 16) | (grn << 8) | red;
                        xProps->setPropertyValue(u"LineColor"_ustr,
                            css::uno::Any(bgrColor));
                    }
                }
            }
        }

        if (formatSpec.indexOf(u"TextSize"_ustr) >= 0)
        {
            const sal_Int32 keyPos = formatSpec.indexOf(u"TextSize"_ustr);
            const sal_Int32 colonPos = formatSpec.indexOf(':', keyPos);
            if (colonPos >= 0)
            {
                sal_Int32 numStart = colonPos + 1;
                while (numStart < formatSpec.getLength()
                       && formatSpec[numStart] == ' ')
                    ++numStart;
                sal_Int32 numEnd = numStart;
                while (numEnd < formatSpec.getLength()
                       && ((formatSpec[numEnd] >= '0' && formatSpec[numEnd] <= '9')
                           || formatSpec[numEnd] == '.'))
                    ++numEnd;
                if (numEnd > numStart)
                {
                    const OUString fsStr = formatSpec.copy(numStart, numEnd - numStart);
                    const float fs = fsStr.toFloat();
                    if (fs > 0.0f)
                        xProps->setPropertyValue(u"CharHeight"_ustr,
                            css::uno::Any(static_cast<float>(fs)));
                }
            }
        }

        SAL_INFO("kqoffice.ai.chat",
                 "Impress format slide=" << (stgt.slideIndex + 1)
                     << " shape=" << (stgt.shapeIndex)
                     << " spec=\"" << formatSpec << "\"");
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Impress format failed: "_ustr + e.Message;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Top-level dispatch: route an operation to the correct app type + op type
// ---------------------------------------------------------------------------

ApplyResult dispatchByDocType(
    const css::uno::Reference<css::frame::XModel>& model,
    const OUString& docType, const DiffOperation& op)
{
    if (docType == u"writer"_ustr)
    {
        auto doc(css::uno::Reference<css::text::XTextDocument>(
            model, css::uno::UNO_QUERY));
        if (!doc.is())
        {
            ApplyResult r;
            r.error = u"Writer document does not support XTextDocument"_ustr;
            return r;
        }
        if (op.opType == u"insert"_ustr)
            return writerInsert(doc, op.target, op.newText);
        if (op.opType == u"delete"_ustr)
            return writerDelete(doc, op.target);
        if (op.opType == u"replace"_ustr)
            return writerReplace(doc, op.target, op.newText);
        if (op.opType == u"format"_ustr)
            return writerFormat(doc, op.target, op.newText);
        ApplyResult r;
        r.error = u"Unsupported operation for writer: "_ustr + op.opType;
        return r;
    }

    if (docType == u"calc"_ustr)
    {
        auto doc(css::uno::Reference<css::sheet::XSpreadsheetDocument>(
            model, css::uno::UNO_QUERY));
        if (!doc.is())
        {
            ApplyResult r;
            r.error = u"Calc document does not support XSpreadsheetDocument"_ustr;
            return r;
        }
        // chart_insert is handled by DocumentAIApply (wizard dispatch), not UNO cell ops.
        if (op.opType == u"chart_insert"_ustr)
        {
            ApplyResult r;
            r.error = u"Unsupported operation for calc: chart_insert "
                      u"(use DocumentAIApply calc-chart-dispatch)"_ustr;
            return r;
        }
        if (op.opType == u"insert"_ustr)
            return calcInsert(doc, op.target, op.newText);
        if (op.opType == u"delete"_ustr)
            return calcDelete(doc, op.target);
        if (op.opType == u"replace"_ustr)
            return calcReplace(doc, op.target, op.newText);
        if (op.opType == u"format"_ustr)
            return calcFormat(doc, op.target, op.newText);
        ApplyResult r;
        r.error = u"Unsupported operation for calc: "_ustr + op.opType;
        return r;
    }

    if (docType == u"impress"_ustr)
    {
        auto supp(css::uno::Reference<css::drawing::XDrawPagesSupplier>(
            model, css::uno::UNO_QUERY));
        if (!supp.is())
        {
            ApplyResult r;
            r.error = u"Impress document does not support XDrawPagesSupplier"_ustr;
            return r;
        }
        if (op.opType == u"insert"_ustr)
            return impressInsert(supp, op.target, op.newText);
        if (op.opType == u"delete"_ustr)
            return impressDelete(supp, op.target);
        if (op.opType == u"replace"_ustr)
            return impressReplace(supp, op.target, op.newText);
        if (op.opType == u"format"_ustr)
            return impressFormat(supp, op.target, op.newText);
        ApplyResult r;
        r.error = u"Unsupported operation for impress: "_ustr + op.opType;
        return r;
    }

    ApplyResult r;
    r.error = u"Unsupported document type: "_ustr + docType;
    return r;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ApplyResult AgentChatDiffApplier::apply(const ApplyPlan& plan)
{
    ApplyResult result;
    result.success = true;

    SAL_INFO("kqoffice.ai.chat",
             "Applying plan: planId=\"" << plan.planId
                 << "\", operations=" << plan.operations.size());

    for (size_t i = 0; i < plan.operations.size(); ++i)
    {
        const DiffOperation& op = plan.operations[i];
        ApplyResult opResult = applyOperation(op);

        if (opResult.success)
        {
            result.appliedOps.push_back(op.opType);
            SAL_INFO("kqoffice.ai.chat",
                     "  Applied[" << i << "]: " << op.opType
                         << " target=" << op.target);
        }
        else
        {
            result.success = false;
            result.error = u"Operation["_ustr + OUString::number(static_cast<sal_Int32>(i))
                + u"] failed: "_ustr + opResult.error;
            SAL_WARN("kqoffice.ai.chat",
                     "  Failed[" << i << "]: " << op.opType
                         << " target=" << op.target
                         << " error=" << opResult.error);
            break; // Stop on first failure (transaction-like semantics)
        }
    }

    // Push successful plan onto undo stack
    if (result.success)
        pushUndo(plan);

    return result;
}

// ── Undo stack ────────────────────────────────────────────────────────

std::vector<ApplyPlan> AgentChatDiffApplier::s_undoStack;

void AgentChatDiffApplier::pushUndo(const ApplyPlan& plan)
{
    s_undoStack.push_back(plan);
    SAL_INFO("kqoffice.ai.chat",
             "Undo stack: pushed plan " << plan.planId
                 << " (stack depth=" << s_undoStack.size() << ")");
}

bool AgentChatDiffApplier::canUndo()
{
    return !s_undoStack.empty();
}

ApplyResult AgentChatDiffApplier::undo()
{
    ApplyResult result;
    if (s_undoStack.empty())
    {
        result.error = u"No operation to undo"_ustr;
        return result;
    }

    ApplyPlan lastPlan = s_undoStack.back();
    s_undoStack.pop_back();

    ApplyPlan inversePlan = lastPlan.inverse();

    SAL_INFO("kqoffice.ai.chat",
             "Undo: applying inverse of plan " << lastPlan.planId
                 << " (" << inversePlan.operations.size() << " inverse operations,"
                 << " stack depth=" << s_undoStack.size() << ")");

    result = apply(inversePlan);

    // Don't push the undo onto the undo stack (prevents undo-undo loops)
    if (result.success)
    {
        s_undoStack.pop_back(); // remove the inverse plan we just pushed
    }

    return result;
}

void AgentChatDiffApplier::clearUndoStack()
{
    s_undoStack.clear();
    SAL_INFO("kqoffice.ai.chat", "Undo stack cleared");
}

ApplyResult AgentChatDiffApplier::applyOperation(const DiffOperation& op)
{
    ApplyResult result;

    if (op.opType.isEmpty())
    {
        result.error = u"Empty opType"_ustr;
        return result;
    }

    if (op.target.isEmpty())
    {
        result.error = u"Empty target"_ustr;
        return result;
    }

    // Get the current document model
    auto model = getCurrentModel();
    if (!model.is())
    {
        result.error = u"No current document (getCurrentComponent returned null)"_ustr;
        SAL_WARN("kqoffice.ai.chat", "applyOperation: " << result.error);
        return result;
    }

    // Detect document type
    const OUString docType = detectDocumentType(model);
    if (docType.isEmpty())
    {
        result.error = u"Unrecognized document type (unsupported or untitled)"_ustr;
        SAL_WARN("kqoffice.ai.chat", "applyOperation: " << result.error
                     << " URL=" << model->getURL());
        return result;
    }

    // Dispatch by opType + docType
    result = dispatchByDocType(model, docType, op);

    SAL_INFO("kqoffice.ai.chat",
             "applyOperation: " << op.opType
                 << " docType=" << docType
                 << " target=" << op.target
                 << " success=" << (result.success ? "true" : "false")
                 << (result.success ? "" : " error=\"")
                 << (result.success ? "" : result.error)
                 << (result.success ? "" : "\""));

    return result;
}

bool AgentChatDiffApplier::canApply(const DiffOperation& op)
{
    if (op.opType.isEmpty() || op.target.isEmpty())
        return false;

    if (op.opType != u"insert"_ustr && op.opType != u"delete"_ustr
        && op.opType != u"replace"_ustr && op.opType != u"format"_ustr)
        return false;

    // For insert and replace, newText must be non-empty
    if ((op.opType == u"insert"_ustr || op.opType == u"replace"_ustr) && op.newText.isEmpty())
        return false;

    return true;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */