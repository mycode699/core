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
#include <AgentChatSelectionCapture.hxx>

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
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
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

        // M-I0: notes-only payload (讲稿：… without title/body) — update notes page only.
        if (bodyBuf.isEmpty() && !notesText.isEmpty())
        {
            try
            {
                css::uno::Reference<css::presentation::XPresentationPage> xPres(
                    xPage, css::uno::UNO_QUERY);
                if (xPres.is())
                {
                    css::uno::Reference<css::drawing::XDrawPage> xNotes = xPres->getNotesPage();
                    if (xNotes.is())
                    {
                        auto xNoteShapes(css::uno::Reference<css::drawing::XShapes>(
                            xNotes, css::uno::UNO_QUERY));
                        if (xNoteShapes.is())
                        {
                            // Prefer reusing an existing text shape on notes page.
                            bool wrote = false;
                            css::uno::Reference<css::container::XIndexAccess> xIdx(
                                xNoteShapes, css::uno::UNO_QUERY);
                            if (xIdx.is())
                            {
                                const sal_Int32 n = xIdx->getCount();
                                for (sal_Int32 si = 0; si < n; ++si)
                                {
                                    css::uno::Reference<css::drawing::XShape> sh(
                                        xIdx->getByIndex(si), css::uno::UNO_QUERY);
                                    css::uno::Reference<css::text::XText> xt(
                                        sh, css::uno::UNO_QUERY);
                                    if (sh.is() && xt.is())
                                    {
                                        xt->setString(notesText);
                                        wrote = true;
                                        break;
                                    }
                                }
                            }
                            if (!wrote)
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
                                    wrote = true;
                                }
                            }
                            if (wrote)
                            {
                                r.success = true;
                                SAL_INFO("kqoffice.ai.chat",
                                         "Impress notes-only slide=" << (slideIdx + 1)
                                                                     << " notesLen="
                                                                     << notesText.getLength());
                                return r;
                            }
                        }
                    }
                }
            }
            catch (const css::uno::Exception& e)
            {
                r.error = u"Impress notes-only failed: "_ustr + e.Message;
                return r;
            }
            r.error = u"Impress notes-only: notes page unavailable"_ustr;
            return r;
        }

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

        // Reuse existing text shapes when re-applying to the same page (avoid stacking shapes).
        std::vector<css::uno::Reference<css::drawing::XShape>> existingText;
        try
        {
            css::uno::Reference<css::container::XIndexAccess> xIdx(xShapes, css::uno::UNO_QUERY);
            if (xIdx.is())
            {
                const sal_Int32 n = xIdx->getCount();
                for (sal_Int32 si = 0; si < n && existingText.size() < 6; ++si)
                {
                    css::uno::Reference<css::drawing::XShape> sh(xIdx->getByIndex(si),
                                                                 css::uno::UNO_QUERY);
                    css::uno::Reference<css::text::XText> xt(sh, css::uno::UNO_QUERY);
                    if (sh.is() && xt.is())
                        existingText.push_back(sh);
                }
            }
        }
        catch (const css::uno::Exception&)
        {
        }
        sal_Int32 nextExisting = 0;

        auto setOrMakeTextShape = [&](const OUString& content, sal_Int32 x, sal_Int32 y,
                                      sal_Int32 w, sal_Int32 h) {
            if (nextExisting < static_cast<sal_Int32>(existingText.size()))
            {
                auto xShape = existingText[static_cast<size_t>(nextExisting++)];
                try
                {
                    xShape->setSize(css::awt::Size(w, h));
                    xShape->setPosition(css::awt::Point(x, y));
                    css::uno::Reference<css::text::XText> xShapeText(xShape, css::uno::UNO_QUERY);
                    if (xShapeText.is())
                        xShapeText->setString(content);
                    return xShape;
                }
                catch (const css::uno::Exception&)
                {
                }
            }
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

        auto makeTextShape = [&](const OUString& content, sal_Int32 x, sal_Int32 y, sal_Int32 w,
                                 sal_Int32 h) {
            return setOrMakeTextShape(content, x, y, w, h);
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

/// Prefer live view selection when target is "selection" or selection matches oldText.
bool tryWriterReplaceViewSelection(
    const css::uno::Reference<css::frame::XModel>& model, const OUString& newText,
    const OUString& oldText)
{
    try
    {
        auto xController = model->getCurrentController();
        if (!xController.is())
            return false;
        auto xVcs = css::uno::Reference<css::text::XTextViewCursorSupplier>(
            xController, css::uno::UNO_QUERY);
        if (!xVcs.is())
            return false;
        auto xView = xVcs->getViewCursor();
        if (!xView.is())
            return false;
        const OUString selected = xView->getString();
        if (selected.isEmpty())
            return false;
        // Match full selection, or selection equals staged oldText (after trim).
        if (!oldText.isEmpty() && selected.trim() != oldText.trim()
            && selected != oldText)
            return false;
        xView->setString(newText);
        SAL_INFO("kqoffice.ai.chat",
                 "Writer replace via view selection len=" << selected.getLength());
        return true;
    }
    catch (const css::uno::Exception&)
    {
        return false;
    }
}

/// Replace first occurrence of oldText inside a paragraph (selection-sized spans).
bool tryWriterReplaceSpanInParagraph(
    const css::uno::Reference<css::text::XText>& xText, sal_Int32 paraIdx,
    const OUString& oldText, const OUString& newText)
{
    if (oldText.isEmpty() || !xText.is())
        return false;
    try
    {
        auto xCursor = getParaCursor(xText, paraIdx);
        if (!xCursor.is())
            return false;
        auto xParaCursor(css::uno::Reference<css::text::XParagraphCursor>(
            xCursor, css::uno::UNO_QUERY));
        if (!xParaCursor.is())
            return false;
        // Select full paragraph content.
        xParaCursor->gotoStartOfParagraph(false);
        xParaCursor->gotoEndOfParagraph(true);
        const OUString paraText = xCursor->getString();
        const sal_Int32 at = paraText.indexOf(oldText);
        if (at < 0)
        {
            // Soft match: trimmed
            const OUString trimmed = oldText.trim();
            if (trimmed.isEmpty())
                return false;
            const sal_Int32 at2 = paraText.indexOf(trimmed);
            if (at2 < 0)
                return false;
            // Reselect and replace span via character walk.
            xParaCursor->gotoStartOfParagraph(false);
            if (at2 > 0)
                xCursor->goRight(at2, false);
            xCursor->goRight(trimmed.getLength(), true);
            xCursor->setString(newText);
            return true;
        }
        xParaCursor->gotoStartOfParagraph(false);
        if (at > 0)
            xCursor->goRight(at, false);
        xCursor->goRight(oldText.getLength(), true);
        xCursor->setString(newText);
        SAL_INFO("kqoffice.ai.chat",
                 "Writer replace span in para=" << (paraIdx + 1)
                     << " oldLen=" << oldText.getLength());
        return true;
    }
    catch (const css::uno::Exception&)
    {
        return false;
    }
}

ApplyResult writerReplace(
    const css::uno::Reference<css::text::XTextDocument>& doc,
    const OUString& target, const OUString& newText, const OUString& oldText)
{
    ApplyResult r;
    try
    {
        auto xText = doc->getText();
        const OUString cleanNew = AgentChatDiffApplier::sanitizeApplyText(newText);
        if (cleanNew.isEmpty())
        {
            r.error = u"Writer replace: empty newText after sanitize"_ustr;
            return r;
        }

        css::uno::Reference<css::frame::XModel> xModel(doc, css::uno::UNO_QUERY);

        // 1) Explicit selection target → replace live view selection.
        if (target == u"selection"_ustr || target.startsWith(u"selection:"_ustr))
        {
            if (tryWriterReplaceViewSelection(xModel, cleanNew, OUString()))
            {
                r.success = true;
                return r;
            }
            // M-W1 review fixes: selection may be empty — search first matching span.
            if (!oldText.isEmpty())
            {
                for (sal_Int32 pi = 0; pi < 500; ++pi)
                {
                    if (tryWriterReplaceSpanInParagraph(xText, pi, oldText, cleanNew))
                    {
                        r.success = true;
                        return r;
                    }
                    // Stop when we pass last paragraph (helper returns false for OOB).
                    auto xProbe = getParaCursor(xText, pi);
                    if (!xProbe.is())
                        break;
                }
                r.error = u"Writer replace: oldText not found for review fix"_ustr;
                return r;
            }
            // Fall through to para: if selection vanished.
        }
        // 2) Live selection still matches staged oldText → replace selection (选区改写).
        else if (!oldText.isEmpty()
                 && tryWriterReplaceViewSelection(xModel, cleanNew, oldText))
        {
            r.success = true;
            return r;
        }

        const sal_Int32 paraIdx = parseParaTarget(target);
        if (paraIdx < 0)
        {
            if (tryWriterReplaceViewSelection(xModel, cleanNew, OUString()))
            {
                r.success = true;
                return r;
            }
            // Document-wide search when only oldText is known (review fix list).
            if (!oldText.isEmpty())
            {
                for (sal_Int32 pi = 0; pi < 500; ++pi)
                {
                    if (tryWriterReplaceSpanInParagraph(xText, pi, oldText, cleanNew))
                    {
                        r.success = true;
                        return r;
                    }
                    auto xProbe = getParaCursor(xText, pi);
                    if (!xProbe.is())
                        break;
                }
            }
            r.error = u"Writer replace: target must be para:N or selection"_ustr;
            return r;
        }

        // 3) Prefer replacing only the oldText span inside the paragraph (选区改写).
        if (!oldText.isEmpty()
            && tryWriterReplaceSpanInParagraph(xText, paraIdx, oldText, cleanNew))
        {
            r.success = true;
            return r;
        }

        auto xCursor = getParaCursor(xText, paraIdx);
        if (!xCursor.is())
        {
            r.error = u"Writer replace: paragraph "_ustr
                + OUString::number(paraIdx + 1) + u" not found"_ustr;
            return r;
        }

        // 4) Fallback: full paragraph replace (legacy behavior).
        auto xParaCursor(css::uno::Reference<css::text::XParagraphCursor>(
            xCursor, css::uno::UNO_QUERY));
        if (xParaCursor.is())
            xParaCursor->gotoEndOfParagraph(true);

        xCursor->setString(cleanNew);
        SAL_INFO("kqoffice.ai.chat",
                 "Writer replace full para=" << (paraIdx + 1)
                     << " newLen=" << cleanNew.getLength());
        r.success = true;
    }
    catch (const css::uno::Exception& e)
    {
        r.error = u"Writer replace failed: "_ustr + e.Message;
    }
    return r;
}

/// Soft-normalize title for matching (collapse spaces, strip trailing punct).
OUString softTitleKey(const OUString& s)
{
    OUString t = s.trim();
    while (!t.isEmpty()
           && (t.endsWith(u"。"_ustr) || t.endsWith(u"."_ustr) || t.endsWith(u"："_ustr)
               || t.endsWith(u":"_ustr) || t.endsWith(u"、"_ustr) || t.endsWith(u";"_ustr)
               || t.endsWith(u"；"_ustr)))
        t = t.copy(0, t.getLength() - 1).trim();
    // collapse runs of whitespace
    OUStringBuffer b;
    bool prevSpace = false;
    for (sal_Int32 i = 0; i < t.getLength(); ++i)
    {
        const sal_Unicode c = t[i];
        if (c == u' ' || c == u'\t' || c == u'\u00a0')
        {
            if (!prevSpace)
                b.append(u' ');
            prevSpace = true;
        }
        else
        {
            prevSpace = false;
            b.append(c);
        }
    }
    return b.makeStringAndClear();
}

/// Find first paragraph whose text contains needle (trimmed). Returns 0-based index or -1.
sal_Int32 findParaIndexByText(const css::uno::Reference<css::text::XText>& xText,
                              const OUString& needle)
{
    if (!xText.is() || needle.isEmpty())
        return -1;
    const OUString want = softTitleKey(needle);
    if (want.isEmpty())
        return -1;
    try
    {
        sal_Int32 softHit = -1;
        for (sal_Int32 idx = 0; idx < 4000; ++idx)
        {
            auto xCursor = getParaCursor(xText, idx);
            if (!xCursor.is())
                break;
            auto xParaCursor(css::uno::Reference<css::text::XParagraphCursor>(
                xCursor, css::uno::UNO_QUERY));
            if (!xParaCursor.is())
                break;
            xParaCursor->gotoStartOfParagraph(false);
            xParaCursor->gotoEndOfParagraph(true);
            const OUString raw = xCursor->getString().trim();
            if (raw.isEmpty())
                continue;
            const OUString t = softTitleKey(raw);
            if (t == want || t.indexOf(want) >= 0)
                return idx;
            // Short para fully contained in model title line
            if (want.getLength() >= 2 && want.getLength() <= 120 && t.getLength() <= 200
                && want.indexOf(t) >= 0 && t.getLength() >= 2)
            {
                if (softHit < 0)
                    softHit = idx;
            }
            // Prefix match (first 12 chars) for long titles slightly rephrased
            if (want.getLength() >= 8 && t.getLength() >= 8)
            {
                const OUString wp = want.copy(0, 8);
                const OUString tp = t.copy(0, 8);
                if (wp == tp && softHit < 0)
                    softHit = idx;
            }
        }
        return softHit;
    }
    catch (const css::uno::Exception&)
    {
    }
    return -1;
}

ApplyResult writerFormat(
    const css::uno::Reference<css::text::XTextDocument>& doc,
    const OUString& target, const OUString& formatSpec,
    const OUString& titleHint = OUString())
{
    ApplyResult r;
    try
    {
        auto xText = doc->getText();

        sal_Int32 paraIdx = parseParaTarget(target);
        // search:标题 or title:… → locate paragraph by text (layout-polish H1|title)
        if (paraIdx < 0
            && (target.startsWith(u"search:"_ustr) || target.startsWith(u"title:"_ustr)))
        {
            const OUString needle
                = target.startsWith(u"search:"_ustr) ? target.copy(7) : target.copy(6);
            paraIdx = findParaIndexByText(xText, needle);
            if (paraIdx < 0 && !titleHint.isEmpty())
                paraIdx = findParaIndexByText(xText, titleHint);
        }
        if (paraIdx < 0 && !titleHint.isEmpty())
            paraIdx = findParaIndexByText(xText, titleHint);
        if (paraIdx < 0)
        {
            r.error = u"Writer format: target must be para:N 或 search:标题（未找到匹配段）"_ustr;
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
        // M-W1: heading:N / ParaStyleName:Heading N → paragraph style (outline write-back).
        auto trySetParaStyle = [&](const OUString& rStyleName) -> bool {
            try
            {
                xProps->setPropertyValue(u"ParaStyleName"_ustr, css::uno::Any(rStyleName));
                return true;
            }
            catch (const css::uno::Exception&)
            {
                return false;
            }
        };
        sal_Int32 headingLevel = 0;
        const sal_Int32 hPos = formatSpec.indexOf(u"heading:"_ustr);
        if (hPos >= 0 && hPos + 8 < formatSpec.getLength())
        {
            const sal_Unicode c = formatSpec[hPos + 8];
            if (c >= u'1' && c <= u'3')
                headingLevel = c - u'0';
        }
        if (headingLevel == 0)
        {
            if (formatSpec.indexOf(u"Heading 1"_ustr) >= 0 || formatSpec.indexOf(u"标题 1"_ustr) >= 0
                || formatSpec.indexOf(u"标题1"_ustr) >= 0)
                headingLevel = 1;
            else if (formatSpec.indexOf(u"Heading 2"_ustr) >= 0
                     || formatSpec.indexOf(u"标题 2"_ustr) >= 0)
                headingLevel = 2;
            else if (formatSpec.indexOf(u"Heading 3"_ustr) >= 0
                     || formatSpec.indexOf(u"标题 3"_ustr) >= 0)
                headingLevel = 3;
        }
        if (headingLevel >= 1 && headingLevel <= 3)
        {
            // English first (default template), then Chinese localized names.
            const OUString en = u"Heading "_ustr + OUString::number(headingLevel);
            const OUString zh = u"标题 "_ustr + OUString::number(headingLevel);
            const OUString zh2 = u"标题"_ustr + OUString::number(headingLevel);
            if (!trySetParaStyle(en) && !trySetParaStyle(zh) && !trySetParaStyle(zh2))
            {
                r.error = u"Writer format: cannot set heading style level "_ustr
                          + OUString::number(headingLevel);
                return r;
            }
        }

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

/// Prefer the sheet shown in the current view; fall back to first sheet.
css::uno::Reference<css::sheet::XSpreadsheet> getActiveSheet(
    const css::uno::Reference<css::sheet::XSpreadsheetDocument>& doc)
{
    try
    {
        css::uno::Reference<css::frame::XModel> xModel(doc, css::uno::UNO_QUERY);
        if (xModel.is())
        {
            auto xController = xModel->getCurrentController();
            css::uno::Reference<css::sheet::XSpreadsheetView> xView(xController,
                                                                    css::uno::UNO_QUERY);
            if (xView.is())
            {
                auto xActive = xView->getActiveSheet();
                if (xActive.is())
                    return xActive;
            }
        }
    }
    catch (const css::uno::Exception&)
    {
    }
    auto xSheets = doc->getSheets();
    auto xIndex(css::uno::Reference<css::container::XIndexAccess>(
        xSheets, css::uno::UNO_QUERY));
    if (!xIndex.is() || xIndex->getCount() < 1)
        return nullptr;
    return css::uno::Reference<css::sheet::XSpreadsheet>(
        xIndex->getByIndex(0), css::uno::UNO_QUERY);
}

void setCellContent(const css::uno::Reference<css::table::XCell>& xCell, const OUString& rText)
{
    if (!xCell.is())
        return;
    OUString t = rText.trim();
    // Fullwidth equals / formula
    if (t.startsWith(u"＝"_ustr))
        t = u"="_ustr + t.copy(1);
    if (t.startsWith(u"="_ustr))
        xCell->setFormula(t);
    else
        xCell->setFormula(t); // LO accepts plain values via setFormula too
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
        const OUString clean = AgentChatDiffApplier::sanitizeApplyText(text);
        setCellContent(xCell, clean);
        SAL_INFO("kqoffice.ai.chat",
                 "Calc insert cell=" << target
                     << " formula=\"" << clean << "\"");
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
            return writerReplace(doc, op.target, op.newText, op.oldText);
        if (op.opType == u"format"_ustr)
            return writerFormat(doc, op.target, op.newText, op.oldText);
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

    // For insert and replace, newText must be non-empty (after sanitize).
    if ((op.opType == u"insert"_ustr || op.opType == u"replace"_ustr)
        && sanitizeApplyText(op.newText).isEmpty())
        return false;

    return true;
}

OUString AgentChatDiffApplier::sanitizeApplyText(const OUString& rText)
{
    OUString t = rText.trim();
    if (t.isEmpty())
        return t;

    // Strip markdown fenced blocks ```...``` (keep inner).
    if (t.startsWith(u"```"_ustr))
    {
        sal_Int32 nl = t.indexOf(u'\n');
        if (nl > 0)
            t = t.copy(nl + 1);
        const sal_Int32 endFence = t.lastIndexOf(u"```"_ustr);
        if (endFence >= 0)
            t = t.copy(0, endFence);
        t = t.trim();
    }

    // Drop common Chinese/English lead-ins from model replies.
    static const sal_Unicode* prefixes[] = {
        u"改写后：", u"改写：", u"正式改写：", u"润色后：", u"润色：",
        u"扩写：", u"精简：", u"如下：", u"如下：\n",
        u"Rewritten:", u"Rewrite:", u"Here is the rewritten text:",
        u"Here's the rewritten text:", u"Output:",
    };
    for (const sal_Unicode* p : prefixes)
    {
        const OUString pre(p);
        if (t.startsWith(pre))
        {
            t = t.copy(pre.getLength()).trim();
            break;
        }
        // Case-insensitive for English prefixes
        if (pre.getLength() > 0 && pre[0] < 128
            && t.getLength() >= pre.getLength()
            && t.copy(0, pre.getLength()).equalsIgnoreAsciiCase(pre))
        {
            t = t.copy(pre.getLength()).trim();
            break;
        }
    }

    // Strip one layer of wrapping quotes / Chinese quotes.
    if (t.getLength() >= 2)
    {
        const sal_Unicode a = t[0];
        const sal_Unicode b = t[t.getLength() - 1];
        if ((a == u'"' && b == u'"') || (a == u'\'' && b == u'\'')
            || (a == u'“' && b == u'”') || (a == u'「' && b == u'」')
            || (a == u'『' && b == u'』'))
            t = t.copy(1, t.getLength() - 2).trim();
    }

    // Drop trailing “主文档未改” style model chatter lines.
    const sal_Int32 chat = t.indexOf(u"\n主文档未改"_ustr);
    if (chat > 0)
        t = t.copy(0, chat).trim();

    return t;
}

ApplyPlan AgentChatDiffApplier::normalizePlanForApply(const ApplyPlan& rPlan)
{
    ApplyPlan out = rPlan;
    const SelectionContext sel = AgentChatSelectionCapture::captureCurrent();

    for (auto& op : out.operations)
    {
        if (op.opType.isEmpty())
            op.opType = u"replace"_ustr;

        op.newText = sanitizeApplyText(op.newText);

        // Fill empty target from live selection position.
        if (op.target.isEmpty() && !sel.position.isEmpty())
            op.target = sel.position;

        // Writer 选区改写: if live selection matches oldText (or old empty + has sel),
        // prefer target=selection so we don't overwrite the whole paragraph.
        if ((op.opType == u"replace"_ustr || op.opType.isEmpty())
            && sel.surface == u"writer"_ustr && sel.length > 0)
        {
            const bool matchOld = op.oldText.isEmpty()
                                  || sel.text.trim() == op.oldText.trim()
                                  || sel.text == op.oldText;
            if (matchOld)
            {
                if (op.oldText.isEmpty())
                    op.oldText = sel.text;
                op.target = u"selection"_ustr;
            }
        }

        // Calc range: → top-left cell for simple write.
        if (sel.surface == u"calc"_ustr && op.target.startsWith(u"range:"_ustr))
        {
            OUString rest = op.target.copy(6);
            const sal_Int32 colon = rest.indexOf(u':');
            op.target = u"cell:"_ustr + (colon > 0 ? rest.copy(0, colon) : rest);
        }

        // Calc formula hygiene: ensure leading '=' for SUM/AVERAGE style lines.
        if (sel.surface == u"calc"_ustr
            && (op.opType == u"replace"_ustr || op.opType == u"insert"_ustr || op.opType.isEmpty()))
        {
            OUString t = op.newText.trim();
            if (t.startsWith(u"＝"_ustr))
                t = u"="_ustr + t.copy(1);
            // Common model slips: "SUM(A1:A10)" without '='
            if (!t.startsWith(u"="_ustr) && t.getLength() >= 4)
            {
                const OUString up = t.toAsciiUpperCase();
                if (up.startsWith(u"SUM("_ustr) || up.startsWith(u"AVERAGE("_ustr)
                    || up.startsWith(u"COUNT("_ustr) || up.startsWith(u"MAX("_ustr)
                    || up.startsWith(u"MIN("_ustr) || up.startsWith(u"IF("_ustr)
                    || up.startsWith(u"VLOOKUP("_ustr) || up.startsWith(u"XLOOKUP("_ustr)
                    || up.startsWith(u"INDEX("_ustr) || up.startsWith(u"MATCH("_ustr)
                    || up.startsWith(u"ROUND("_ustr) || up.startsWith(u"CONCATENATE("_ustr)
                    || up.startsWith(u"TEXT("_ustr))
                    t = u"="_ustr + t;
            }
            op.newText = t;
            if (op.target.isEmpty() && !sel.position.isEmpty())
            {
                if (sel.position.startsWith(u"range:"_ustr))
                {
                    OUString rest = sel.position.copy(6);
                    const sal_Int32 colon = rest.indexOf(u':');
                    op.target = u"cell:"_ustr + (colon > 0 ? rest.copy(0, colon) : rest);
                }
                else
                    op.target = sel.position;
            }
        }
    }
    return out;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */