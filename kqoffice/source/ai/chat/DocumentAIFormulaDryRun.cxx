/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "DocumentAIFormulaDryRun.hxx"

#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
#include <com/sun/star/table/CellContentType.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>
#include <comphelper/processfactory.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cmath>
#include <cctype>
#include <map>
#include <vector>

namespace kqoffice::ai::chat
{
namespace
{
bool isKnownFunction(const OUString& nameUpper)
{
    // Keep short and practical — SUM-class + common office formulas.
    static const char* const kFns[] = {
        "SUM",       "SUMIF",    "SUMIFS",   "AVERAGE",  "AVERAGEIF", "COUNT",
        "COUNTA",    "COUNTIF",  "COUNTIFS", "MAX",      "MIN",       "IF",
        "IFS",       "AND",      "OR",       "NOT",      "ROUND",     "ROUNDUP",
        "ROUNDDOWN", "ABS",      "INT",      "MOD",      "POWER",     "SQRT",
        "PRODUCT",   "SUBTOTAL", "TRIM",     "CLEAN",    "LEFT",      "RIGHT",
        "MID",       "LEN",      "UPPER",    "LOWER",    "PROPER",    "CONCATENATE",
        "CONCAT",    "TEXTJOIN", "TEXT",     "VALUE",    "DATE",      "TODAY",
        "NOW",       "YEAR",     "MONTH",    "DAY",      "VLOOKUP",   "HLOOKUP",
        "XLOOKUP",   "INDEX",    "MATCH",    "INDIRECT", "OFFSET",    "ROW",
        "COLUMN",    "ROWS",     "COLUMNS",  "IFERROR",  "ISBLANK",   "ISNUMBER",
        "ISTEXT",    "N",        "T",        "NA",       "PI",        "TRUE",
        "FALSE",
    };
    for (const char* f : kFns)
    {
        if (nameUpper == OUString::createFromAscii(f))
            return true;
    }
    return false;
}

bool balancedParens(const OUString& s)
{
    sal_Int32 depth = 0;
    bool inStr = false;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u'"')
        {
            // simple toggle; ignore escaped "" as end
            if (inStr && i + 1 < s.getLength() && s[i + 1] == u'"')
            {
                ++i;
                continue;
            }
            inStr = !inStr;
            continue;
        }
        if (inStr)
            continue;
        if (c == u'(')
            ++depth;
        else if (c == u')')
        {
            --depth;
            if (depth < 0)
                return false;
        }
    }
    return depth == 0 && !inStr;
}

bool hasSuspiciousChineseBody(const OUString& body)
{
    // Pure Chinese sentence without cell refs / digits → likely prose not formula.
    sal_Int32 han = 0;
    sal_Int32 ascii = 0;
    for (sal_Int32 i = 0; i < body.getLength(); ++i)
    {
        const sal_Unicode c = body[i];
        if (c >= 0x4E00 && c <= 0x9FFF)
            ++han;
        else if ((c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9'))
            ++ascii;
    }
    return han >= 6 && ascii < 2;
}

OUString firstFunctionName(const OUString& body)
{
    // Skip leading spaces; take A-Z letters before '('
    sal_Int32 i = 0;
    while (i < body.getLength() && body[i] == u' ')
        ++i;
    sal_Int32 start = i;
    while (i < body.getLength())
    {
        const sal_Unicode c = body[i];
        if ((c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || c == u'_' || c == u'.')
            ++i;
        else
            break;
    }
    if (i == start)
        return OUString();
    if (i < body.getLength() && body[i] == u'(')
        return body.copy(start, i - start).toAsciiUpperCase();
    return OUString();
}

bool hasCellOrRangeToken(const OUString& body)
{
    // A1, $A$1, Sheet1!A1, A1:B10
    for (sal_Int32 i = 0; i < body.getLength(); ++i)
    {
        sal_Unicode c = body[i];
        if (c == u'$')
        {
            if (i + 1 < body.getLength())
                c = body[i + 1];
        }
        if ((c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z'))
        {
            sal_Int32 j = i;
            while (j < body.getLength())
            {
                const sal_Unicode d = body[j];
                if ((d >= u'A' && d <= u'Z') || (d >= u'a' && d <= u'z') || d == u'$')
                    ++j;
                else
                    break;
            }
            if (j < body.getLength() && body[j] >= u'0' && body[j] <= u'9')
                return true;
        }
    }
    return false;
}

/// Skip spaces in body from index i.
sal_Int32 skipSp(const OUString& s, sal_Int32 i)
{
    while (i < s.getLength() && s[i] == u' ')
        ++i;
    return i;
}

bool parseNumber(const OUString& s, sal_Int32& i, double& out)
{
    i = skipSp(s, i);
    if (i >= s.getLength())
        return false;
    sal_Int32 start = i;
    if (s[i] == u'+' || s[i] == u'-')
        ++i;
    bool any = false;
    while (i < s.getLength() && s[i] >= u'0' && s[i] <= u'9')
    {
        any = true;
        ++i;
    }
    if (i < s.getLength() && s[i] == u'.')
    {
        ++i;
        while (i < s.getLength() && s[i] >= u'0' && s[i] <= u'9')
        {
            any = true;
            ++i;
        }
    }
    if (!any)
        return false;
    out = s.copy(start, i - start).toDouble();
    return true;
}

// Recursive descent: expr = term ((+|-) term)*; term = factor ((*|/) factor)*; factor = number | (expr)
bool parseExpr(const OUString& s, sal_Int32& i, double& out);

bool parseFactor(const OUString& s, sal_Int32& i, double& out)
{
    i = skipSp(s, i);
    if (i < s.getLength() && s[i] == u'(')
    {
        ++i;
        if (!parseExpr(s, i, out))
            return false;
        i = skipSp(s, i);
        if (i >= s.getLength() || s[i] != u')')
            return false;
        ++i;
        return true;
    }
    return parseNumber(s, i, out);
}

bool parseTerm(const OUString& s, sal_Int32& i, double& out)
{
    if (!parseFactor(s, i, out))
        return false;
    for (;;)
    {
        i = skipSp(s, i);
        if (i >= s.getLength())
            break;
        const sal_Unicode op = s[i];
        if (op != u'*' && op != u'/')
            break;
        ++i;
        double rhs = 0;
        if (!parseFactor(s, i, rhs))
            return false;
        if (op == u'*')
            out *= rhs;
        else
        {
            if (rhs == 0.0)
                return false;
            out /= rhs;
        }
    }
    return true;
}

bool parseExpr(const OUString& s, sal_Int32& i, double& out)
{
    if (!parseTerm(s, i, out))
        return false;
    for (;;)
    {
        i = skipSp(s, i);
        if (i >= s.getLength())
            break;
        const sal_Unicode op = s[i];
        if (op != u'+' && op != u'-')
            break;
        ++i;
        double rhs = 0;
        if (!parseTerm(s, i, rhs))
            return false;
        if (op == u'+')
            out += rhs;
        else
            out -= rhs;
    }
    return true;
}

bool parseLiteralList(const OUString& inside, std::vector<double>& nums)
{
    sal_Int32 i = 0;
    while (i < inside.getLength())
    {
        i = skipSp(inside, i);
        if (i >= inside.getLength())
            break;
        double v = 0;
        if (!parseNumber(inside, i, v))
            return false;
        nums.push_back(v);
        i = skipSp(inside, i);
        if (i < inside.getLength() && inside[i] == u',')
        {
            ++i;
            continue;
        }
        if (i < inside.getLength())
            return false;
    }
    return !nums.empty();
}

void tallySandbox(FormulaDryRunReport& rep, const FormulaDryRunItem& item)
{
    if (item.sandboxStatus == u"ok"_ustr || item.sandboxStatus == u"ok-snapshot"_ustr)
        ++rep.sandboxOk;
    else if (item.sandboxStatus == u"needs-sheet"_ustr)
        ++rep.sandboxNeedsSheet;
    else if (item.sandboxStatus == u"error"_ustr)
        ++rep.sandboxError;
}

OUString colIndexToLettersSnap(sal_Int32 col)
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

OUString normalizeAddrKey(const OUString& rAddr)
{
    OUString a = rAddr.trim().toAsciiUpperCase();
    // strip $
    OUStringBuffer b;
    for (sal_Int32 i = 0; i < a.getLength(); ++i)
        if (a[i] != u'$')
            b.append(a[i]);
    return b.makeStringAndClear();
}

OUString buildReportSummary(const FormulaDryRunReport& rep)
{
    OUStringBuffer b;
    b.append(u"公式 dry-run "_ustr);
    b.append(OUString::number(rep.okCount));
    b.append(u"/"_ustr);
    b.append(OUString::number(rep.checked));
    b.append(u" 通过"_ustr);
    if (rep.sandboxOk + rep.sandboxNeedsSheet + rep.sandboxError > 0)
    {
        b.append(u" · 沙箱求值 ok="_ustr);
        b.append(OUString::number(rep.sandboxOk));
        b.append(u" sheet="_ustr);
        b.append(OUString::number(rep.sandboxNeedsSheet));
        b.append(u" err="_ustr);
        b.append(OUString::number(rep.sandboxError));
    }
    if (rep.snapshotCells > 0)
    {
        b.append(u" · 快照格="_ustr);
        b.append(OUString::number(rep.snapshotCells));
    }
    if (rep.badCount > 0)
    {
        b.append(u" · 失败 "_ustr);
        b.append(OUString::number(rep.badCount));
        sal_Int32 shown = 0;
        for (const auto& it : rep.items)
        {
            if (it.ok)
                continue;
            if (++shown > 2)
                break;
            b.append(u"；"_ustr);
            if (!it.target.isEmpty())
            {
                b.append(it.target);
                b.append(u" "_ustr);
            }
            b.append(it.issue);
        }
    }
    return b.makeStringAndClear();
}
} // namespace

OUString DocumentAIFormulaDryRun::normalizeFormula(const OUString& rFormula)
{
    OUString f = rFormula.trim();
    if (f.startsWith(u"＝"_ustr))
        f = u"="_ustr + f.copy(1);
    if (!f.startsWith(u"="_ustr) && !f.isEmpty())
        f = u"="_ustr + f;
    return f;
}

bool DocumentAIFormulaDryRun::looksLikeFormula(const OUString& rText)
{
    const OUString f = normalizeFormula(rText);
    return f.getLength() >= 2 && f.startsWith(u"="_ustr);
}

bool CellValueSnapshot::lookupNumber(const OUString& rAddr, double& rOut) const
{
    const OUString key = normalizeAddrKey(rAddr);
    for (const auto& c : cells)
    {
        if (normalizeAddrKey(c.addr) == key)
        {
            if (!c.isNumber)
                return false;
            rOut = c.number;
            return true;
        }
    }
    return false;
}

CellValueSnapshot DocumentAIFormulaDryRun::captureSelectionSnapshot(sal_Int32 nMaxCells)
{
    CellValueSnapshot snap;
    if (nMaxCells < 1)
        nMaxCells = 1;
    if (nMaxCells > 1024)
        nMaxCells = 1024;
    try
    {
        auto xContext = comphelper::getProcessComponentContext();
        auto xDesktop = css::frame::Desktop::create(xContext);
        auto xModel = css::uno::Reference<css::frame::XModel>(xDesktop->getCurrentComponent(),
                                                              css::uno::UNO_QUERY);
        if (!xModel.is())
            return snap;
        auto xCtrl = xModel->getCurrentController();
        if (!xCtrl.is())
            return snap;
        auto xView = css::uno::Reference<css::sheet::XSpreadsheetView>(xCtrl, css::uno::UNO_QUERY);
        if (!xView.is())
            return snap;
        auto xSheet = xView->getActiveSheet();
        if (!xSheet.is())
            return snap;
        auto xSelSup = css::uno::Reference<css::view::XSelectionSupplier>(xCtrl, css::uno::UNO_QUERY);
        if (!xSelSup.is())
            return snap;
        auto anySel = xSelSup->getSelection();
        if (!anySel.hasValue())
            return snap;

        auto xAddr = css::uno::Reference<css::sheet::XCellRangeAddressable>(anySel, css::uno::UNO_QUERY);
        auto xRange = css::uno::Reference<css::table::XCellRange>(anySel, css::uno::UNO_QUERY);
        if (!xAddr.is() || !xRange.is())
        {
            auto xCell = css::uno::Reference<css::table::XCell>(anySel, css::uno::UNO_QUERY);
            if (!xCell.is())
                return snap;
            // single cell without address — skip
            return snap;
        }
        const auto addr = xAddr->getRangeAddress();
        const OUString sc = colIndexToLettersSnap(addr.StartColumn);
        const OUString ec = colIndexToLettersSnap(addr.EndColumn);
        const sal_Int32 sr = addr.StartRow + 1;
        const sal_Int32 er = addr.EndRow + 1;
        if (addr.StartColumn == addr.EndColumn && addr.StartRow == addr.EndRow)
            snap.rangeLabel = u"cell:"_ustr + sc + OUString::number(sr);
        else
            snap.rangeLabel = u"range:"_ustr + sc + OUString::number(sr) + u":"_ustr + ec
                              + OUString::number(er);

        sal_Int32 count = 0;
        for (sal_Int32 r = addr.StartRow; r <= addr.EndRow && count < nMaxCells; ++r)
        {
            for (sal_Int32 c = addr.StartColumn; c <= addr.EndColumn && count < nMaxCells; ++c)
            {
                auto xCell = xRange->getCellByPosition(c - addr.StartColumn, r - addr.StartRow);
                if (!xCell.is())
                    continue;
                CellSnapshotEntry e;
                e.addr = colIndexToLettersSnap(c) + OUString::number(r + 1);
                e.text = xCell->getFormula();
                try
                {
                    const auto t = xCell->getType();
                    if (t == css::table::CellContentType_VALUE
                        || t == css::table::CellContentType_FORMULA)
                    {
                        e.number = xCell->getValue();
                        e.isNumber = true;
                    }
                    else if (t == css::table::CellContentType_TEXT)
                    {
                        css::uno::Reference<css::text::XText> xText(xCell, css::uno::UNO_QUERY);
                        if (xText.is())
                            e.text = xText->getString();
                        // try parse number from text
                        OUString ts = e.text.trim();
                        sal_Int32 ii = 0;
                        double nv = 0;
                        if (parseNumber(ts, ii, nv) && ii >= ts.getLength())
                        {
                            e.number = nv;
                            e.isNumber = true;
                        }
                    }
                }
                catch (...)
                {
                }
                snap.cells.push_back(e);
                ++count;
            }
        }
    }
    catch (const css::uno::Exception& ex)
    {
        SAL_WARN("kqoffice.ai.chat", "captureSelectionSnapshot: " << ex.Message);
    }
    catch (...)
    {
    }
    return snap;
}

OUString DocumentAIFormulaDryRun::substituteSnapshot(const OUString& rFormulaBody,
                                                     const CellValueSnapshot& rSnap,
                                                     bool bRequireAll, bool& rHadRefs,
                                                     bool& rAllResolved)
{
    rHadRefs = false;
    rAllResolved = true;
    if (rSnap.empty() || rFormulaBody.isEmpty())
        return rFormulaBody;

    // Walk body; replace A1 / $A$1 / ranges A1:A3 when fully known.
    OUStringBuffer out;
    sal_Int32 i = 0;
    const OUString body = rFormulaBody;
    while (i < body.getLength())
    {
        // skip string literals
        if (body[i] == u'"')
        {
            out.append(body[i++]);
            while (i < body.getLength())
            {
                out.append(body[i]);
                if (body[i] == u'"')
                {
                    ++i;
                    break;
                }
                if (body[i] == u'\\' && i + 1 < body.getLength())
                {
                    ++i;
                    out.append(body[i]);
                }
                ++i;
            }
            continue;
        }
        // letter start of cell ref?
        sal_Int32 j = i;
        if (body[j] == u'$')
            ++j;
        if (j < body.getLength()
            && ((body[j] >= u'A' && body[j] <= u'Z') || (body[j] >= u'a' && body[j] <= u'z')))
        {
            sal_Int32 colStart = j;
            while (j < body.getLength()
                   && ((body[j] >= u'A' && body[j] <= u'Z') || (body[j] >= u'a' && body[j] <= u'z')))
                ++j;
            if (j < body.getLength() && body[j] == u'$')
                ++j;
            sal_Int32 rowStart = j;
            while (j < body.getLength() && body[j] >= u'0' && body[j] <= u'9')
                ++j;
            if (rowStart < j && colStart < rowStart)
            {
                // possible range A1:B2
                if (j < body.getLength() && body[j] == u':')
                {
                    sal_Int32 k = j + 1;
                    if (k < body.getLength() && body[k] == u'$')
                        ++k;
                    while (k < body.getLength()
                           && ((body[k] >= u'A' && body[k] <= u'Z')
                               || (body[k] >= u'a' && body[k] <= u'z')))
                        ++k;
                    if (k < body.getLength() && body[k] == u'$')
                        ++k;
                    sal_Int32 row2 = k;
                    while (k < body.getLength() && body[k] >= u'0' && body[k] <= u'9')
                        ++k;
                    if (row2 < k)
                    {
                        rHadRefs = true;
                        // Expand range values from snapshot into comma list if all present
                        const OUString left = normalizeAddrKey(body.copy(i, j - i));
                        const OUString right = normalizeAddrKey(body.copy(j + 1, k - (j + 1)));
                        // only same-column or same-row simple ranges in snapshot
                        std::vector<double> vals;
                        bool allOk = true;
                        for (const auto& c : rSnap.cells)
                        {
                            // if cell is between left and right lexicographically weak — only use
                            // cells whose addr is in snapshot and matches range by inclusion via
                            // collecting all snapshot cells (caller selection is the range)
                            (void)left;
                            (void)right;
                            if (c.isNumber)
                                vals.push_back(c.number);
                            else
                                allOk = false;
                        }
                        // Prefer: if selection snapshot equals this range usage, dump all numbers
                        if (allOk && !vals.empty()
                            && (rSnap.rangeLabel.indexOf(u"range:"_ustr) >= 0
                                || rSnap.size() > 1))
                        {
                            out.append(u'(');
                            for (size_t vi = 0; vi < vals.size(); ++vi)
                            {
                                if (vi)
                                    out.append(u',');
                                out.append(OUString::number(vals[vi]));
                            }
                            out.append(u')');
                            i = k;
                            continue;
                        }
                        rAllResolved = false;
                        out.append(body.copy(i, k - i));
                        i = k;
                        continue;
                    }
                }
                // single cell
                rHadRefs = true;
                const OUString addr = body.copy(i, j - i);
                double v = 0;
                if (rSnap.lookupNumber(addr, v))
                {
                    out.append(OUString::number(v));
                    i = j;
                    continue;
                }
                rAllResolved = false;
                if (bRequireAll)
                {
                    out.append(body.copy(i, j - i));
                    i = j;
                    continue;
                }
                out.append(body.copy(i, j - i));
                i = j;
                continue;
            }
        }
        out.append(body[i]);
        ++i;
    }
    if (bRequireAll && rHadRefs && !rAllResolved)
        return OUString();
    return out.makeStringAndClear();
}

FormulaDryRunItem DocumentAIFormulaDryRun::checkFormula(const OUString& rFormula,
                                                        const OUString& rTarget,
                                                        const CellValueSnapshot* pSnap)
{
    FormulaDryRunItem item;
    item.target = rTarget;
    item.formula = normalizeFormula(rFormula);
    if (item.formula.isEmpty() || item.formula == u"="_ustr)
    {
        item.ok = false;
        item.issue = u"empty-formula · 公式为空"_ustr;
        return item;
    }
    if (!item.formula.startsWith(u"="_ustr))
    {
        item.ok = false;
        item.issue = u"missing-equals · 公式必须以 = 开头"_ustr;
        return item;
    }
    const OUString body = item.formula.copy(1).trim();
    if (body.isEmpty())
    {
        item.ok = false;
        item.issue = u"empty-body · = 后无内容"_ustr;
        return item;
    }
    if (!balancedParens(item.formula))
    {
        item.ok = false;
        item.issue = u"unbalanced-parens · 括号不匹配"_ustr;
        return item;
    }
    if (hasSuspiciousChineseBody(body))
    {
        item.ok = false;
        item.issue = u"prose-not-formula · 疑似中文叙述而非公式"_ustr;
        return item;
    }
    // Reject common bad tokens from LLM
    if (body.indexOf(u"```"_ustr) >= 0 || body.indexOf(u"==="_ustr) >= 0)
    {
        item.ok = false;
        item.issue = u"markup-leak · 公式含 markdown/标记"_ustr;
        return item;
    }

    const OUString fn = firstFunctionName(body);
    if (!fn.isEmpty())
    {
        if (!isKnownFunction(fn))
        {
            // Soft warn: still ok for custom/add-in names with parens + cell-like args
            if (!hasCellOrRangeToken(body) && body.indexOf(u'(') >= 0)
            {
                item.ok = false;
                item.issue = u"unknown-function · 未知函数 "_ustr + fn;
                return item;
            }
        }
        // Known fn or custom with refs — require '(' for function form
        if (body.indexOf(u'(') < 0)
        {
            item.ok = false;
            item.issue = u"fn-missing-call · 函数名后缺少 ()"_ustr;
            return item;
        }
        item.ok = true;
        sandboxEvaluate(item, pSnap);
        return item;
    }

    // Arithmetic / cell ref formulas: =A1+B1, =100*0.1
    bool hasOperand = false;
    for (sal_Int32 i = 0; i < body.getLength(); ++i)
    {
        const sal_Unicode c = body[i];
        if ((c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z'))
        {
            hasOperand = true;
            break;
        }
    }
    if (!hasOperand)
    {
        item.ok = false;
        item.issue = u"no-operand · 缺少数字或单元格引用"_ustr;
        return item;
    }
    item.ok = true;
    sandboxEvaluate(item, pSnap);
    return item;
}

void DocumentAIFormulaDryRun::sandboxEvaluate(FormulaDryRunItem& rItem,
                                              const CellValueSnapshot* pSnap)
{
    rItem.sandboxStatus = u"none"_ustr;
    rItem.sandboxValue = 0.0;
    rItem.sandboxNote.clear();
    if (!rItem.ok || rItem.formula.isEmpty())
        return;

    OUString body = rItem.formula.startsWith(u"="_ustr) ? rItem.formula.copy(1).trim()
                                                        : rItem.formula.trim();
    if (body.isEmpty())
        return;

    // Optional snapshot substitution for cell refs in selection.
    bool usedSnap = false;
    if (pSnap && !pSnap->empty() && hasCellOrRangeToken(body))
    {
        bool had = false, all = true;
        // For SUM(A1:A10) style with selection covering that range, rewrite as SUM(v,v,…)
        OUString sub = body;
        // Expand SUM|AVERAGE|…(range) using all snapshot numbers when body is mostly one fn+range
        const OUString fn0 = firstFunctionName(body);
        if (!fn0.isEmpty() && (fn0 == u"SUM"_ustr || fn0 == u"AVERAGE"_ustr || fn0 == u"MIN"_ustr
                               || fn0 == u"MAX"_ustr || fn0 == u"PRODUCT"_ustr || fn0 == u"COUNT"_ustr))
        {
            const sal_Int32 lp = body.indexOf(u'(');
            const sal_Int32 rp = body.lastIndexOf(u')');
            if (lp >= 0 && rp > lp)
            {
                const OUString inside = body.copy(lp + 1, rp - lp - 1).trim();
                if (hasCellOrRangeToken(inside) && !hasCellOrRangeToken(fn0))
                {
                    // Use entire snapshot as argument list
                    bool anyNonNum = false;
                    OUStringBuffer list;
                    for (size_t i = 0; i < pSnap->cells.size(); ++i)
                    {
                        if (!pSnap->cells[i].isNumber)
                        {
                            anyNonNum = true;
                            break;
                        }
                        if (i)
                            list.append(u',');
                        list.append(OUString::number(pSnap->cells[i].number));
                    }
                    if (!anyNonNum && !pSnap->cells.empty())
                    {
                        body = fn0 + u"("_ustr + list.makeStringAndClear() + u")"_ustr;
                        usedSnap = true;
                        had = true;
                        all = true;
                    }
                }
            }
        }
        if (!usedSnap)
        {
            sub = substituteSnapshot(body, *pSnap, /*bRequireAll*/ false, had, all);
            if (had && all && !sub.isEmpty())
            {
                body = sub;
                usedSnap = true;
            }
            else if (had && !all)
            {
                // Try partial — still attempt if no remaining cell tokens
                if (!hasCellOrRangeToken(sub))
                {
                    body = sub;
                    usedSnap = true;
                }
                else
                {
                    rItem.sandboxStatus = u"needs-sheet"_ustr;
                    rItem.sandboxNote
                        = u"快照未覆盖全部引用 · 格="_ustr + OUString::number(pSnap->size())
                          + u" · 写入后由表格引擎求值"_ustr;
                    return;
                }
            }
        }
    }

    // Cell/range dependency → cannot pure-eval without sheet.
    if (hasCellOrRangeToken(body))
    {
        rItem.sandboxStatus = u"needs-sheet"_ustr;
        rItem.sandboxNote = pSnap && !pSnap->empty()
                                ? u"仍有未解析引用 · 写入后由表格引擎求值"_ustr
                                : u"依赖单元格/区域 · 选区快照可提升沙箱覆盖"_ustr;
        return;
    }

    // SUM/AVERAGE/MIN/MAX/PRODUCT of literal list
    const OUString fn = firstFunctionName(body);
    if (!fn.isEmpty())
    {
        const sal_Int32 lp = body.indexOf(u'(');
        const sal_Int32 rp = body.lastIndexOf(u')');
        if (lp < 0 || rp <= lp)
        {
            rItem.sandboxStatus = u"error"_ustr;
            rItem.sandboxNote = u"沙箱：函数括号不完整"_ustr;
            return;
        }
        const OUString inside = body.copy(lp + 1, rp - lp - 1);

        // ROUND(number, digits) / ABS / INT — pure numeric
        if (fn == u"ROUND"_ustr || fn == u"ABS"_ustr || fn == u"INT"_ustr || fn == u"SQRT"_ustr)
        {
            sal_Int32 ii = 0;
            double num = 0;
            if (!parseNumber(inside, ii, num) && !parseExpr(inside, ii = 0, num))
            {
                // ROUND(1.236, 2)
                const sal_Int32 comma = inside.indexOf(u',');
                if (fn == u"ROUND"_ustr && comma > 0)
                {
                    OUString left = inside.copy(0, comma).trim();
                    OUString right = inside.copy(comma + 1).trim();
                    sal_Int32 a = 0, b = 0;
                    double n = 0, dig = 0;
                    if (parseNumber(left, a, n) && parseNumber(right, b, dig))
                    {
                        double scale = 1;
                        const sal_Int32 d = static_cast<sal_Int32>(dig);
                        for (sal_Int32 k = 0; k < d; ++k)
                            scale *= 10;
                        for (sal_Int32 k = 0; k < -d; ++k)
                            scale /= 10;
                        const double vv = (d >= 0) ? (std::round(n * scale) / scale)
                                                   : (std::round(n * scale) / scale);
                        rItem.sandboxStatus = u"ok"_ustr;
                        rItem.sandboxValue = vv;
                        rItem.sandboxNote = u"ROUND="_ustr + OUString::number(vv);
                        return;
                    }
                }
                rItem.sandboxStatus = u"error"_ustr;
                rItem.sandboxNote = u"沙箱："_ustr + fn + u" 参数无法求值"_ustr;
                return;
            }
            double vv = num;
            if (fn == u"ABS"_ustr)
                vv = num < 0 ? -num : num;
            else if (fn == u"INT"_ustr)
                vv = static_cast<double>(static_cast<sal_Int64>(num));
            else if (fn == u"SQRT"_ustr)
            {
                if (num < 0)
                {
                    rItem.sandboxStatus = u"error"_ustr;
                    rItem.sandboxNote = u"沙箱：SQRT 负数"_ustr;
                    return;
                }
                vv = std::sqrt(num);
            }
            // bare ROUND(x) → 0 digits
            if (fn == u"ROUND"_ustr)
                vv = std::round(num);
            rItem.sandboxStatus = u"ok"_ustr;
            rItem.sandboxValue = vv;
            rItem.sandboxNote = fn + u"="_ustr + OUString::number(vv);
            return;
        }

        // IF(cond, a, b) where cond is pure compare: 1>0, 2=2, 3<1
        if (fn == u"IF"_ustr)
        {
            // split top-level commas
            std::vector<OUString> parts;
            sal_Int32 depth = 0;
            sal_Int32 start = 0;
            for (sal_Int32 i = 0; i < inside.getLength(); ++i)
            {
                const sal_Unicode c = inside[i];
                if (c == u'(')
                    ++depth;
                else if (c == u')')
                    --depth;
                else if (c == u',' && depth == 0)
                {
                    parts.push_back(inside.copy(start, i - start).trim());
                    start = i + 1;
                }
            }
            parts.push_back(inside.copy(start).trim());
            if (parts.size() < 2)
            {
                rItem.sandboxStatus = u"error"_ustr;
                rItem.sandboxNote = u"沙箱：IF 参数不足"_ustr;
                return;
            }
            auto evalPart = [&](const OUString& part, double& outV) -> bool {
                sal_Int32 j = 0;
                return parseExpr(part, j, outV) && skipSp(part, j) >= part.getLength();
            };
            auto evalCond = [&](const OUString& cond, bool& outB) -> bool {
                // n op m
                for (const auto& op : { u">="_ustr, u"<="_ustr, u"<>"_ustr, u"="_ustr, u">"_ustr,
                                        u"<"_ustr })
                {
                    const sal_Int32 p = cond.indexOf(op);
                    if (p <= 0)
                        continue;
                    double L = 0, R = 0;
                    if (!evalPart(cond.copy(0, p).trim(), L)
                        || !evalPart(cond.copy(p + op.getLength()).trim(), R))
                        return false;
                    if (op == u">="_ustr)
                        outB = L >= R;
                    else if (op == u"<="_ustr)
                        outB = L <= R;
                    else if (op == u"<>"_ustr)
                        outB = L != R;
                    else if (op == u"="_ustr)
                        outB = L == R;
                    else if (op == u">"_ustr)
                        outB = L > R;
                    else
                        outB = L < R;
                    return true;
                }
                double v = 0;
                if (!evalPart(cond, v))
                    return false;
                outB = v != 0.0;
                return true;
            };
            bool cond = false;
            if (!evalCond(parts[0], cond))
            {
                rItem.sandboxStatus = u"needs-sheet"_ustr;
                rItem.sandboxNote = u"沙箱：IF 条件含非纯数字"_ustr;
                return;
            }
            double chosen = 0;
            const OUString& branch = cond ? parts[1] : (parts.size() > 2 ? parts[2] : OUString());
            if (branch.isEmpty())
            {
                rItem.sandboxStatus = u"ok"_ustr;
                rItem.sandboxValue = 0;
                rItem.sandboxNote = u"IF=FALSE(空)"_ustr;
                return;
            }
            if (!evalPart(branch, chosen))
            {
                rItem.sandboxStatus = u"error"_ustr;
                rItem.sandboxNote = u"沙箱：IF 分支无法求值"_ustr;
                return;
            }
            rItem.sandboxStatus = u"ok"_ustr;
            rItem.sandboxValue = chosen;
            rItem.sandboxNote = u"IF="_ustr + OUString::number(chosen);
            return;
        }

        if (fn != u"SUM"_ustr && fn != u"AVERAGE"_ustr && fn != u"MIN"_ustr && fn != u"MAX"_ustr
            && fn != u"PRODUCT"_ustr && fn != u"COUNT"_ustr)
        {
            rItem.sandboxStatus = u"needs-sheet"_ustr;
            rItem.sandboxNote = u"沙箱：函数 "_ustr + fn + u" 暂不纯求值"_ustr;
            return;
        }
        std::vector<double> nums;
        if (!parseLiteralList(inside, nums))
        {
            rItem.sandboxStatus = u"error"_ustr;
            rItem.sandboxNote = u"沙箱：参数非纯数字列表"_ustr;
            return;
        }
        double v = 0;
        if (fn == u"SUM"_ustr || fn == u"COUNT"_ustr)
        {
            if (fn == u"COUNT"_ustr)
                v = static_cast<double>(nums.size());
            else
                for (double n : nums)
                    v += n;
        }
        else if (fn == u"AVERAGE"_ustr)
        {
            for (double n : nums)
                v += n;
            v /= static_cast<double>(nums.size());
        }
        else if (fn == u"MIN"_ustr)
        {
            v = nums.front();
            for (double n : nums)
                if (n < v)
                    v = n;
        }
        else if (fn == u"MAX"_ustr)
        {
            v = nums.front();
            for (double n : nums)
                if (n > v)
                    v = n;
        }
        else if (fn == u"PRODUCT"_ustr)
        {
            v = 1;
            for (double n : nums)
                v *= n;
        }
        rItem.sandboxStatus = usedSnap ? u"ok-snapshot"_ustr : u"ok"_ustr;
        rItem.sandboxValue = v;
        rItem.sandboxNote = (usedSnap ? u"快照·"_ustr : OUString()) + fn + u"="_ustr
                            + OUString::number(v);
        return;
    }

    // Pure arithmetic expression
    sal_Int32 i = 0;
    double v = 0;
    if (!parseExpr(body, i, v))
    {
        rItem.sandboxStatus = u"error"_ustr;
        rItem.sandboxNote = u"沙箱：算术表达式无法求值"_ustr;
        return;
    }
    i = skipSp(body, i);
    if (i < body.getLength())
    {
        rItem.sandboxStatus = u"error"_ustr;
        rItem.sandboxNote = u"沙箱：表达式有未解析尾部"_ustr;
        return;
    }
    rItem.sandboxStatus = usedSnap ? u"ok-snapshot"_ustr : u"ok"_ustr;
    rItem.sandboxValue = v;
    rItem.sandboxNote = (usedSnap ? u"快照·算术="_ustr : u"算术="_ustr) + OUString::number(v);
}

FormulaDryRunReport DocumentAIFormulaDryRun::checkPlan(const ApplyPlan& rPlan,
                                                       const CellValueSnapshot* pSnap)
{
    FormulaDryRunReport rep;
    if (pSnap)
        rep.snapshotCells = pSnap->size();
    for (const auto& op : rPlan.operations)
    {
        OUString text = op.newText.trim();
        if (text.startsWith(u"＝"_ustr))
            text = u"="_ustr + text.copy(1);
        if (!text.startsWith(u"="_ustr))
            continue;
        auto item = checkFormula(text, op.target, pSnap);
        rep.items.push_back(item);
        ++rep.checked;
        if (item.ok)
            ++rep.okCount;
        else
            ++rep.badCount;
        tallySandbox(rep, item);
    }
    rep.summaryZh = buildReportSummary(rep);
    return rep;
}

FormulaDryRunReport DocumentAIFormulaDryRun::checkText(const OUString& rText,
                                                       const CellValueSnapshot* pSnap)
{
    FormulaDryRunReport rep;
    if (pSnap)
        rep.snapshotCells = pSnap->size();
    if (rText.isEmpty())
    {
        rep.summaryZh = u"公式 dry-run · 无文本"_ustr;
        return rep;
    }
    // Prefer structured write-back parse
    if (AgentChatDiffExtractor::looksLikeCalcFormulaWriteback(rText)
        || AgentChatDiffExtractor::looksLikeCalcCleanWriteback(rText))
    {
        ApplyPlan p = AgentChatDiffExtractor::looksLikeCalcCleanWriteback(rText)
                          ? AgentChatDiffExtractor::extractCalcCleanWritebackPlan(rText)
                          : AgentChatDiffExtractor::extractCalcFormulaWritebackPlan(rText);
        return checkPlan(p, pSnap);
    }
    auto formulas = AgentChatDiffExtractor::extractAllFormulas(rText);
    if (formulas.empty())
    {
        const OUString one = AgentChatDiffExtractor::extractLeadingFormula(rText);
        if (!one.isEmpty())
            formulas.push_back(one);
    }
    for (const auto& f : formulas)
    {
        if (rep.checked >= 32)
            break;
        auto item = checkFormula(f, OUString(), pSnap);
        rep.items.push_back(item);
        ++rep.checked;
        if (item.ok)
            ++rep.okCount;
        else
            ++rep.badCount;
        tallySandbox(rep, item);
    }
    rep.summaryZh = buildReportSummary(rep);
    return rep;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
