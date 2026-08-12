/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AgentChatSelectionCapture.
 *
 * Day-2: Real UNO-based selection capture for Writer/Calc/Impress.
 * Uses XTextRange/XTextCursor for Writer, XCell/XCellRange for Calc,
 * and XDrawPage/XShape for Impress to capture actual document content.
 */

#include <AgentChatSelectionCapture.hxx>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <com/sun/star/text/XParagraphCursor.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <comphelper/processfactory.hxx>
#include <sal/log.hxx>

using namespace kqoffice::ai::chat;

namespace
{

/// Convert column index (0-based) to letter(s): 0→A, 25→Z, 26→AA.
OUString colIndexToLetters(sal_Int32 col)
{
    OUString result;
    sal_Int32 n = col + 1;
    while (n > 0)
    {
        n--;
        result = OUString(static_cast<sal_Unicode>('A' + (n % 26))) + result;
        n /= 26;
    }
    return result;
}

/// Detect document type from UNO model via service queries.
OUString detectDocumentType(const css::uno::Reference<css::frame::XModel>& xModel)
{
    if (!xModel.is())
        return u""_ustr;

    css::uno::Reference<css::text::XTextDocument> xText(xModel, css::uno::UNO_QUERY);
    if (xText.is())
        return u"writer"_ustr;

    css::uno::Reference<css::sheet::XSpreadsheetDocument> xSheet(xModel, css::uno::UNO_QUERY);
    if (xSheet.is())
        return u"calc"_ustr;

    css::uno::Reference<css::drawing::XDrawPagesSupplier> xDraw(xModel, css::uno::UNO_QUERY);
    if (xDraw.is())
        return u"impress"_ustr;

    // Fallback: try URL suffix
    try
    {
        const OUString url = xModel->getURL();
        if (url.endsWith(".odt") || url.endsWith(".docx") || url.endsWith(".doc"))
            return u"writer"_ustr;
        if (url.endsWith(".ods") || url.endsWith(".xlsx") || url.endsWith(".xls"))
            return u"calc"_ustr;
        if (url.endsWith(".odp") || url.endsWith(".pptx") || url.endsWith(".ppt"))
            return u"impress"_ustr;
    }
    catch (const css::uno::Exception&) {}

    return u""_ustr;
}

/// Get current document model.
css::uno::Reference<css::frame::XModel> getCurrentModel()
{
    try
    {
        auto xDesktop = css::frame::Desktop::create(
            comphelper::getProcessComponentContext());
        return css::uno::Reference<css::frame::XModel>(
            xDesktop->getCurrentComponent(), css::uno::UNO_QUERY);
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat",
                 "SelectionCapture: getCurrentModel failed: " << e.Message);
    }
    return css::uno::Reference<css::frame::XModel>();
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────

SelectionContext AgentChatSelectionCapture::captureCurrent()
{
    try
    {
        auto xModel = getCurrentModel();
        if (!xModel.is())
        {
            SAL_WARN("kqoffice.ai.chat",
                     "SelectionCapture: no current document");
            SelectionContext ctx;
            ctx.surface = u"none"_ustr;
            return ctx;
        }

        const OUString docType = detectDocumentType(xModel);
        if (docType == "writer")
            return captureFromWriter();
        if (docType == "calc")
            return captureFromCalc();
        if (docType == "impress")
            return captureFromImpress();

        SAL_INFO("kqoffice.ai.chat",
                 "SelectionCapture: unsupported document type");
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat",
                 "SelectionCapture: captureCurrent failed: " << e.Message);
    }

    SelectionContext ctx;
    ctx.surface = u"unknown"_ustr;
    return ctx;
}

SelectionContext AgentChatSelectionCapture::captureFromWriter()
{
    SelectionContext ctx;
    ctx.surface = u"writer"_ustr;

    try
    {
        auto xModel = getCurrentModel();
        if (!xModel.is())
            return ctx;

        auto xTextDoc = css::uno::Reference<css::text::XTextDocument>(
            xModel, css::uno::UNO_QUERY);
        if (!xTextDoc.is())
            return ctx;

        // Get the view cursor to find current selection position
        auto xController = xModel->getCurrentController();
        if (!xController.is())
            return ctx;

        auto xViewCursorSupplier = css::uno::Reference<
            css::text::XTextViewCursorSupplier>(xController, css::uno::UNO_QUERY);
        if (!xViewCursorSupplier.is())
        {
            // Fallback: get all text
            auto xText = xTextDoc->getText();
            ctx.text = xText->getString();
            ctx.position = u"para:1"_ustr;
            ctx.length = ctx.text.getLength();
            SAL_INFO("kqoffice.ai.chat",
                     "Writer selection: full document, len=" << ctx.length);
            return ctx;
        }

        auto xViewCursor = xViewCursorSupplier->getViewCursor();
        if (!xViewCursor.is())
            return ctx;

        // Check if there's a selection (cursor has range)
        auto xTextCursor = css::uno::Reference<css::text::XTextCursor>(
            xViewCursor, css::uno::UNO_QUERY);
        if (!xTextCursor.is())
            return ctx;

        // Get selected text (empty when caret-only → complete path).
        ctx.text = xViewCursor->getString();
        ctx.length = ctx.text.getLength();

        // Count *paragraphs* from document start + enrich paragraph context for complete.
        auto xText = xTextDoc->getText();
        auto xStartRange = xTextCursor->getStart();
        auto xEndRange = xTextCursor->getEnd();
        sal_Int32 paraIdx = 0; // 0-based
        if (xStartRange.is() && xText.is())
        {
            try
            {
                auto xCursor = xText->createTextCursorByRange(xStartRange);
                css::uno::Reference<css::text::XParagraphCursor> xPara(
                    xCursor, css::uno::UNO_QUERY);
                if (xPara.is())
                {
                    sal_Int32 steps = 0;
                    while (xPara->gotoPreviousParagraph(false))
                    {
                        ++paraIdx;
                        if (++steps > 50000)
                            break;
                    }
                }
            }
            catch (const css::uno::Exception&)
            {
                paraIdx = 0;
            }

            // Paragraph-local before/after for high-quality ghost complete.
            try
            {
                auto xParaCursor = xText->createTextCursorByRange(xStartRange);
                css::uno::Reference<css::text::XParagraphCursor> xPara(
                    xParaCursor, css::uno::UNO_QUERY);
                if (xPara.is())
                {
                    // Full paragraph
                    xPara->gotoStartOfParagraph(false);
                    xPara->gotoEndOfParagraph(true);
                    ctx.paraText = xPara->getString();
                    if (ctx.paraText.getLength() > 2000)
                        ctx.paraText = ctx.paraText.copy(0, 2000);

                    // Before caret: start-of-para → caret
                    auto xBefore = xText->createTextCursorByRange(xStartRange);
                    css::uno::Reference<css::text::XParagraphCursor> xB(
                        xBefore, css::uno::UNO_QUERY);
                    if (xB.is())
                    {
                        xB->gotoStartOfParagraph(false);
                        xB->gotoRange(xStartRange, true);
                        ctx.beforeText = xB->getString();
                        // Keep up to 2k so DocumentAIInputPrefs::inlineContextChars can use it.
                        if (ctx.beforeText.getLength() > 2000)
                            ctx.beforeText
                                = ctx.beforeText.copy(ctx.beforeText.getLength() - 2000);
                    }

                    // After caret: caret end → end-of-para
                    auto xAfterStart = xEndRange.is() ? xEndRange : xStartRange;
                    auto xAfter = xText->createTextCursorByRange(xAfterStart);
                    css::uno::Reference<css::text::XParagraphCursor> xA(
                        xAfter, css::uno::UNO_QUERY);
                    if (xA.is())
                    {
                        xA->gotoEndOfParagraph(true);
                        ctx.afterText = xA->getString();
                        if (ctx.afterText.getLength() > 1000)
                            ctx.afterText = ctx.afterText.copy(0, 1000);
                    }
                }
            }
            catch (const css::uno::Exception&)
            {
                // Context is best-effort; selection path still works.
            }
        }
        ctx.position = u"para:"_ustr + OUString::number(paraIdx + 1);

        SAL_INFO("kqoffice.ai.chat",
                 "Writer selection: pos=" << ctx.position
                     << " len=" << ctx.length
                     << " before=" << ctx.beforeText.getLength()
                     << " after=" << ctx.afterText.getLength()
                     << " hasSelection=" << (ctx.length > 0 ? "true" : "false"));
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat",
                 "Writer selection capture failed: " << e.Message);
    }

    return ctx;
}

SelectionContext AgentChatSelectionCapture::captureFromCalc()
{
    SelectionContext ctx;
    ctx.surface = u"calc"_ustr;

    try
    {
        auto xModel = getCurrentModel();
        if (!xModel.is())
            return ctx;

        auto xController = xModel->getCurrentController();
        if (!xController.is())
            return ctx;

        // Try to get spreadsheet view for selection
        auto xSheetView = css::uno::Reference<css::sheet::XSpreadsheetView>(
            xController, css::uno::UNO_QUERY);
        if (!xSheetView.is())
        {
            SAL_INFO("kqoffice.ai.chat", "Calc: no spreadsheet view");
            return ctx;
        }

        auto xSheet = xSheetView->getActiveSheet();
        if (!xSheet.is())
            return ctx;

        // Get the current selection as cell range
        auto xSelectionSupplier = css::uno::Reference<css::view::XSelectionSupplier>(
            xController, css::uno::UNO_QUERY);
        if (!xSelectionSupplier.is())
            return ctx;
        auto xSelection = xSelectionSupplier->getSelection();
        if (!xSelection.hasValue())
            return ctx;

        // Try to get cell range from selection
        auto xCellRange = css::uno::Reference<css::table::XCellRange>(
            xSelection, css::uno::UNO_QUERY);
        if (xCellRange.is())
        {
            // Get range address
            auto xRangeAddr = css::uno::Reference<css::sheet::XCellRangeAddressable>(
                xSelection, css::uno::UNO_QUERY);
            if (xRangeAddr.is())
            {
                auto addr = xRangeAddr->getRangeAddress();
                OUString startCol = colIndexToLetters(addr.StartColumn);
                OUString endCol = colIndexToLetters(addr.EndColumn);
                sal_Int32 startRow = addr.StartRow + 1;
                sal_Int32 endRow = addr.EndRow + 1;

                if (addr.StartColumn == addr.EndColumn && addr.StartRow == addr.EndRow)
                {
                    // Single cell
                    ctx.position = u"cell:"_ustr + startCol + OUString::number(startRow);
                    auto xCell = xSheet->getCellByPosition(addr.StartColumn, addr.StartRow);
                    ctx.text = xCell->getFormula();
                }
                else
                {
                    // Range
                    ctx.position = u"range:"_ustr + startCol + OUString::number(startRow)
                        + u":"_ustr + endCol + OUString::number(endRow);
                    // Get first cell content as representative
                    auto xCell = xSheet->getCellByPosition(addr.StartColumn, addr.StartRow);
                    ctx.text = xCell->getFormula();
                }
            }
        }
        else
        {
            // Try single cell
            auto xCell = css::uno::Reference<css::table::XCell>(
                xSelection, css::uno::UNO_QUERY);
            if (xCell.is())
            {
                ctx.text = xCell->getFormula();
                ctx.position = u"cell:A1"_ustr;
            }
        }

        ctx.length = ctx.text.getLength();

        SAL_INFO("kqoffice.ai.chat",
                 "Calc selection: pos=" << ctx.position
                     << " len=" << ctx.length);
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat",
                 "Calc selection capture failed: " << e.Message);
    }

    return ctx;
}

SelectionContext AgentChatSelectionCapture::captureFromImpress()
{
    SelectionContext ctx;
    ctx.surface = u"impress"_ustr;

    try
    {
        auto xModel = getCurrentModel();
        if (!xModel.is())
            return ctx;

        auto xDrawSupplier = css::uno::Reference<
            css::drawing::XDrawPagesSupplier>(xModel, css::uno::UNO_QUERY);
        if (!xDrawSupplier.is())
            return ctx;

        auto xController = xModel->getCurrentController();
        if (!xController.is())
            return ctx;

        // Try to get the current slide from the draw pages
        auto xPages = xDrawSupplier->getDrawPages();
        auto xIndex = css::uno::Reference<css::container::XIndexAccess>(
            xPages, css::uno::UNO_QUERY);
        if (!xIndex.is())
            return ctx;

        // Try to find which slide is active and what's selected
        sal_Int32 slideIdx = 0;
        OUString selectedText;

        for (sal_Int32 i = 0; i < xIndex->getCount(); i++)
        {
            auto xPage = css::uno::Reference<css::drawing::XDrawPage>(
                xIndex->getByIndex(i), css::uno::UNO_QUERY);
            if (!xPage.is())
                continue;

            auto xShapes = css::uno::Reference<css::drawing::XShapes>(
                xPage, css::uno::UNO_QUERY);
            if (!xShapes.is())
                continue;

            // Check each shape for text content
            for (sal_Int32 j = 0; j < xShapes->getCount(); j++)
            {
                auto xShape = css::uno::Reference<css::drawing::XShape>(
                    xShapes->getByIndex(j), css::uno::UNO_QUERY);
                if (!xShape.is())
                    continue;

                auto xShapeText = css::uno::Reference<css::text::XText>(
                    xShape, css::uno::UNO_QUERY);
                if (xShapeText.is())
                {
                    OUString text = xShapeText->getString();
                    if (!text.isEmpty())
                    {
                        if (!selectedText.isEmpty())
                            selectedText += "\n";
                        selectedText += text;
                        slideIdx = i;
                    }
                }
            }
        }

        ctx.text = selectedText;
        ctx.position = u"slide:"_ustr + OUString::number(slideIdx + 1);
        ctx.length = selectedText.getLength();

        SAL_INFO("kqoffice.ai.chat",
                 "Impress selection: pos=" << ctx.position
                     << " len=" << ctx.length);
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.chat",
                 "Impress selection capture failed: " << e.Message);
    }

    return ctx;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
