/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AgentChatDiffExtractor.
 *
 * Parses LLM output looking for JSON code blocks (```json ... ```).
 * Extracts operations array, validates required fields, and
 * serializes back to JSON.
 */

#include <AgentChatDiffExtractor.hxx>

#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <algorithm>
#include <utility>

using namespace kqoffice::ai::chat;

namespace
{
/// Find the start of a JSON block in LLM output.
/// Looks for ```json marker and returns position after it, or -1.
sal_Int32 findJsonBlockStart(const OUString& text, sal_Int32 fromPos)
{
    const OUString marker = u"```json"_ustr;
    const sal_Int32 idx = text.indexOf(marker, fromPos);
    if (idx < 0)
        return -1;
    return idx + marker.getLength();
}

/// Find the end of a JSON block (closing ```).
sal_Int32 findJsonBlockEnd(const OUString& text, sal_Int32 fromPos)
{
    const OUString marker = u"```"_ustr;
    const sal_Int32 idx = text.indexOf(marker, fromPos);
    if (idx < 0)
        return text.getLength(); // no closing marker — use rest of text
    return idx;
}

/// Trim whitespace from both ends of an OUString.
OUString trimString(const OUString& s)
{
    return s.trim();
}

/// Extract a JSON string value by key from a JSON fragment.
/// Simple scanner — not a full JSON parser. Finds "key":"value" patterns.
/// Returns the value if found, empty string otherwise.
OUString extractJsonStringValue(const OUString& json, const OUString& key)
{
    OUString pattern = u"\""_ustr + key + u"\":\""_ustr;
    const sal_Int32 start = json.indexOf(pattern);
    if (start < 0)
    {
        // Try with spacing: "key": "..."
        pattern = u"\""_ustr + key + u"\": \""_ustr;
        const sal_Int32 start2 = json.indexOf(pattern);
        if (start2 < 0)
            return u""_ustr;
        return extractJsonStringValue(json, key); // recurse with found start
    }

    const sal_Int32 valueStart = start + pattern.getLength();
    if (valueStart >= json.getLength())
        return u""_ustr;

    // Scan to closing quote, respecting escape sequences
    OUStringBuffer buf;
    bool escaped = false;
    for (sal_Int32 i = valueStart; i < json.getLength(); ++i)
    {
        const sal_Unicode c = json[i];
        if (escaped)
        {
            switch (c)
            {
                case 'n': buf.append('\n'); break;
                case 't': buf.append('\t'); break;
                case 'r': buf.append('\r'); break;
                case '\\': buf.append('\\'); break;
                case '"': buf.append('"'); break;
                default: buf.append(c); break;
            }
            escaped = false;
        }
        else if (c == '\\')
        {
            escaped = true;
        }
        else if (c == '"')
        {
            return buf.makeStringAndClear();
        }
        else
        {
            buf.append(c);
        }
    }

    return buf.makeStringAndClear();
}
} // anonymous namespace

ApplyPlan AgentChatDiffExtractor::extract(const OUString& llmOutput)
{
    ApplyPlan plan;
    plan.rawOutput = llmOutput;

    if (llmOutput.isEmpty())
    {
        SAL_WARN("kqoffice.ai.chat", "Empty LLM output");
        return plan;
    }

    // Find the first JSON block
    const sal_Int32 blockStart = findJsonBlockStart(llmOutput, 0);
    if (blockStart < 0)
    {
        // No JSON block found — try treating entire output as JSON
        SAL_INFO("kqoffice.ai.chat", "No ```json block found, trying raw output");
        // Fallback: parse whole output
    }

    const sal_Int32 contentStart = (blockStart >= 0) ? blockStart : 0;
    const sal_Int32 blockEnd = (blockStart >= 0)
                                   ? findJsonBlockEnd(llmOutput, blockStart)
                                   : llmOutput.getLength();

    OUString jsonContent = trimString(llmOutput.copy(contentStart, blockEnd - contentStart));

    if (jsonContent.isEmpty())
    {
        return plan;
    }

    // Extract planId from JSON
    const OUString planId = extractJsonStringValue(jsonContent, u"plan_id"_ustr);
    plan.planId = planId;

    // Simple operation parsing: look for objects with op_type, target, etc.
    // Scan for each operation object in the operations array
    const OUString opMarker = u"\"op_type\""_ustr;
    sal_Int32 searchPos = 0;
    unsigned int opCount = 0;

    while (true)
    {
        const sal_Int32 opStart = jsonContent.indexOf(opMarker, searchPos);
        if (opStart < 0)
            break;

        // Find the enclosing object boundary (scan backwards for '{')
        sal_Int32 objStart = opStart;
        while (objStart > 0 && jsonContent[objStart] != '{')
            --objStart;

        // Find the enclosing object end (scan forward for matching '}')
        int depth = 0;
        sal_Int32 objEnd = objStart;
        while (objEnd < jsonContent.getLength())
        {
            if (jsonContent[objEnd] == '{') ++depth;
            else if (jsonContent[objEnd] == '}') { --depth; if (depth == 0) { ++objEnd; break; } }
            ++objEnd;
        }

        if (objStart < objEnd)
        {
            OUString opFragment = jsonContent.copy(objStart, objEnd - objStart);
            DiffOperation op = AgentChatDiffExtractor::parseOperation(opFragment);
            if (!op.opType.isEmpty())
            {
                plan.operations.push_back(op);
                ++opCount;
            }
        }

        searchPos = objEnd;
    }

    SAL_INFO("kqoffice.ai.chat",
             "Extracted " << opCount << " operations from LLM output, planId=\""
                 << plan.planId << "\"");

    return plan;
}

bool AgentChatDiffExtractor::validate(const ApplyPlan& plan)
{
    if (plan.planId.isEmpty())
    {
        SAL_WARN("kqoffice.ai.chat", "Plan validation failed: empty planId");
        return false;
    }

    if (plan.operations.empty())
    {
        SAL_WARN("kqoffice.ai.chat", "Plan validation failed: no operations");
        return false;
    }

    for (size_t i = 0; i < plan.operations.size(); ++i)
    {
        const DiffOperation& op = plan.operations[i];

        if (op.opType.isEmpty())
        {
            SAL_WARN("kqoffice.ai.chat",
                     "Plan validation failed: operation[" << i << "] missing opType");
            return false;
        }

        if (op.target.isEmpty())
        {
            SAL_WARN("kqoffice.ai.chat",
                     "Plan validation failed: operation[" << i << "] missing target");
            return false;
        }

        // Validate opType is one of the known types
        if (op.opType != u"insert"_ustr && op.opType != u"delete"_ustr
            && op.opType != u"replace"_ustr && op.opType != u"format"_ustr)
        {
            SAL_WARN("kqoffice.ai.chat",
                     "Plan validation failed: operation[" << i << "] unknown opType: "
                         << op.opType);
            return false;
        }

        // For replace/insert, newText should not be empty
        if ((op.opType == u"insert"_ustr || op.opType == u"replace"_ustr) && op.newText.isEmpty())
        {
            SAL_WARN("kqoffice.ai.chat",
                     "Plan validation failed: operation[" << i << "] opType="
                         << op.opType << " but newText is empty");
            return false;
        }
    }

    return true;
}

OUString AgentChatDiffExtractor::toJson(const ApplyPlan& plan)
{
    OUStringBuffer buf;
    buf.append(u"{");

    buf.append(u"\"plan_id\":\"");
    // Escape JSON string
    for (sal_Int32 i = 0; i < plan.planId.getLength(); ++i)
    {
        const sal_Unicode c = plan.planId[i];
        if (c == '"') buf.append(u"\\\"");
        else if (c == '\\') buf.append(u"\\\\");
        else buf.append(c);
    }
    buf.append(u"\",\"operations\":[");

    for (size_t i = 0; i < plan.operations.size(); ++i)
    {
        if (i > 0)
            buf.append(u",");

        const DiffOperation& op = plan.operations[i];
        buf.append(u"{");
        buf.append(u"\"op_type\":\"");
        buf.append(op.opType);
        buf.append(u"\",\"target\":\"");
        buf.append(op.target);
        buf.append(u"\"");

        if (!op.oldText.isEmpty())
        {
            buf.append(u",\"old_text\":\"");
            buf.append(op.oldText);
            buf.append(u"\"");
        }

        if (!op.newText.isEmpty())
        {
            buf.append(u",\"new_text\":\"");
            buf.append(op.newText);
            buf.append(u"\"");
        }

        buf.append(u"}");
    }

    buf.append(u"]}");

    return buf.makeStringAndClear();
}

DiffOperation AgentChatDiffExtractor::parseOperation(const OUString& jsonFragment)
{
    DiffOperation op;

    op.opType = extractJsonStringValue(jsonFragment, u"op_type"_ustr);
    op.target = extractJsonStringValue(jsonFragment, u"target"_ustr);
    op.oldText = extractJsonStringValue(jsonFragment, u"old_text"_ustr);
    op.newText = extractJsonStringValue(jsonFragment, u"new_text"_ustr);

    return op;
}

OUString AgentChatDiffExtractor::extractLeadingFormula(const OUString& rText)
{
    if (rText.isEmpty())
        return OUString();

    // Prefer fenced code block content that starts with '='
    sal_Int32 fence = rText.indexOf(u"```"_ustr);
    if (fence >= 0)
    {
        sal_Int32 contentStart = fence + 3;
        // skip optional language tag
        while (contentStart < rText.getLength() && rText[contentStart] != u'\n')
            ++contentStart;
        if (contentStart < rText.getLength() && rText[contentStart] == u'\n')
            ++contentStart;
        sal_Int32 fenceEnd = rText.indexOf(u"```"_ustr, contentStart);
        if (fenceEnd > contentStart)
        {
            const OUString block = rText.copy(contentStart, fenceEnd - contentStart).trim();
            const sal_Int32 nl = block.indexOf(u'\n');
            const OUString first = (nl >= 0 ? block.copy(0, nl) : block).trim();
            if (first.startsWith(u"="_ustr) && first.getLength() > 1)
                return first;
        }
    }

    // Scan lines for formula / 「公式：」
    sal_Int32 pos = 0;
    while (pos <= rText.getLength())
    {
        sal_Int32 nl = rText.indexOf(u'\n', pos);
        if (nl < 0)
            nl = rText.getLength();
        OUString line = rText.copy(pos, nl - pos).trim();
        // strip common markdown bullets/backticks
        if (line.startsWith(u"`"_ustr) && line.endsWith(u"`"_ustr) && line.getLength() > 2)
            line = line.copy(1, line.getLength() - 2).trim();
        if (line.startsWith(u"公式"_ustr))
        {
            const sal_Int32 colon = line.indexOf(u'：');
            const sal_Int32 colon2 = line.indexOf(u':');
            sal_Int32 c = colon >= 0 ? colon : colon2;
            if (c >= 0)
                line = line.copy(c + 1).trim();
        }
        if (line.startsWith(u"="_ustr) && line.getLength() > 1)
        {
            // Reject pure "==" or markdown headings
            bool hasAlphaOrFunc = false;
            for (sal_Int32 i = 1; i < line.getLength(); ++i)
            {
                const sal_Unicode ch = line[i];
                if ((ch >= u'A' && ch <= u'Z') || (ch >= u'a' && ch <= u'z')
                    || (ch >= u'0' && ch <= u'9') || ch == u'(' || ch == u'[')
                {
                    hasAlphaOrFunc = true;
                    break;
                }
            }
            if (hasAlphaOrFunc)
                return line;
        }
        if (nl >= rText.getLength())
            break;
        pos = nl + 1;
    }
    return OUString();
}

std::vector<OUString> AgentChatDiffExtractor::extractAllFormulas(const OUString& rText)
{
    std::vector<OUString> out;
    if (rText.isEmpty())
        return out;
    // Prefer formulas from first fenced code block if present
    OUString scan = rText;
    sal_Int32 fence = rText.indexOf(u"```"_ustr);
    if (fence >= 0)
    {
        sal_Int32 contentStart = fence + 3;
        while (contentStart < rText.getLength() && rText[contentStart] != u'\n')
            ++contentStart;
        if (contentStart < rText.getLength() && rText[contentStart] == u'\n')
            ++contentStart;
        sal_Int32 fenceEnd = rText.indexOf(u"```"_ustr, contentStart);
        if (fenceEnd > contentStart)
            scan = rText.copy(contentStart, fenceEnd - contentStart);
    }
    sal_Int32 pos = 0;
    while (pos <= scan.getLength())
    {
        sal_Int32 nl = scan.indexOf(u'\n', pos);
        if (nl < 0)
            nl = scan.getLength();
        OUString line = scan.copy(pos, nl - pos).trim();
        if (line.startsWith(u"`"_ustr) && line.endsWith(u"`"_ustr) && line.getLength() > 2)
            line = line.copy(1, line.getLength() - 2).trim();
        if (line.startsWith(u"公式"_ustr))
        {
            const sal_Int32 colon = line.indexOf(u'：');
            const sal_Int32 colon2 = line.indexOf(u':');
            sal_Int32 c = colon >= 0 ? colon : colon2;
            if (c >= 0)
                line = line.copy(c + 1).trim();
        }
        if (line.startsWith(u"="_ustr) && line.getLength() > 1)
        {
            bool hasAlphaOrFunc = false;
            for (sal_Int32 i = 1; i < line.getLength(); ++i)
            {
                const sal_Unicode ch = line[i];
                if ((ch >= u'A' && ch <= u'Z') || (ch >= u'a' && ch <= u'z')
                    || (ch >= u'0' && ch <= u'9') || ch == u'(' || ch == u'[')
                {
                    hasAlphaOrFunc = true;
                    break;
                }
            }
            if (hasAlphaOrFunc)
                out.push_back(line);
        }
        if (nl >= scan.getLength())
            break;
        pos = nl + 1;
    }
    return out;
}

namespace
{
OUString colIndexToLettersLocal(sal_Int32 col)
{
    OUString result;
    sal_Int32 n = col + 1;
    while (n > 0)
    {
        n--;
        result = OUString(static_cast<sal_Unicode>(u'A' + (n % 26))) + result;
        n /= 26;
    }
    return result;
}

bool parseCellRef(const OUString& ref, sal_Int32& col, sal_Int32& row)
{
    col = 0;
    row = 0;
    sal_Int32 i = 0;
    while (i < ref.getLength())
    {
        const sal_Unicode c = ref[i];
        if (c >= u'A' && c <= u'Z')
            col = col * 26 + (c - u'A' + 1);
        else if (c >= u'a' && c <= u'z')
            col = col * 26 + (c - u'a' + 1);
        else
            break;
        ++i;
    }
    if (col == 0)
        return false;
    col -= 1;
    if (i >= ref.getLength())
        return false;
    const OUString rowStr = ref.copy(i);
    for (sal_Int32 k = 0; k < rowStr.getLength(); ++k)
        if (rowStr[k] < u'0' || rowStr[k] > u'9')
            return false;
    row = rowStr.toInt32();
    if (row < 1)
        return false;
    row -= 1;
    return true;
}
} // namespace

ApplyPlan AgentChatDiffExtractor::makeFormulaCellPlan(const OUString& rCellTarget,
                                                       const OUString& rFormula,
                                                       const OUString& rOldText)
{
    ApplyPlan plan;
    if (rCellTarget.isEmpty() || rFormula.isEmpty())
        return plan;
    OUString formula = rFormula.trim();
    if (!formula.startsWith(u"="_ustr))
        formula = u"="_ustr + formula;
    DiffOperation op;
    op.opType = u"replace"_ustr;
    op.target = rCellTarget.startsWith(u"cell:"_ustr) ? rCellTarget : (u"cell:"_ustr + rCellTarget);
    op.oldText = rOldText;
    op.newText = formula;
    plan.planId = u"ap-formula-cell"_ustr;
    plan.operations.push_back(op);
    plan.rawOutput = formula;
    return plan;
}

ApplyPlan AgentChatDiffExtractor::makeFormulaRangePlan(const OUString& rPosition,
                                                       const std::vector<OUString>& rFormulas,
                                                       const OUString& rOldText)
{
    ApplyPlan plan;
    if (rFormulas.empty())
        return plan;

    if (rFormulas.size() == 1
        && (rPosition.isEmpty() || rPosition.startsWith(u"cell:"_ustr)
            || !rPosition.startsWith(u"range:"_ustr)))
    {
        OUString cell = rPosition;
        if (cell.isEmpty() || cell.startsWith(u"range:"_ustr))
            cell = u"cell:A1"_ustr;
        return makeFormulaCellPlan(cell, rFormulas.front(), rOldText);
    }

    sal_Int32 startCol = 0, startRow = 0, endCol = 0, endRow = 0;
    bool haveRange = false;
    if (rPosition.startsWith(u"range:"_ustr))
    {
        OUString rest = rPosition.copy(6);
        const sal_Int32 colon = rest.indexOf(u':');
        if (colon > 0)
        {
            OUString a = rest.copy(0, colon);
            OUString b = rest.copy(colon + 1);
            if (parseCellRef(a, startCol, startRow) && parseCellRef(b, endCol, endRow))
            {
                if (endCol < startCol)
                    std::swap(endCol, startCol);
                if (endRow < startRow)
                    std::swap(endRow, startRow);
                haveRange = true;
            }
        }
    }
    else if (rPosition.startsWith(u"cell:"_ustr))
    {
        if (parseCellRef(rPosition.copy(5), startCol, startRow))
        {
            endCol = startCol;
            endRow = startRow + static_cast<sal_Int32>(rFormulas.size()) - 1;
            haveRange = true;
        }
    }
    if (!haveRange)
    {
        startCol = 0;
        startRow = 0;
        endCol = 0;
        endRow = static_cast<sal_Int32>(rFormulas.size()) - 1;
    }

    // Single formula on multi-cell range → top-left only (safe default)
    if (rFormulas.size() == 1 && (endCol > startCol || endRow > startRow))
    {
        return makeFormulaCellPlan(u"cell:"_ustr + colIndexToLettersLocal(startCol)
                                       + OUString::number(startRow + 1),
                                   rFormulas.front(), rOldText);
    }

    plan.planId = u"ap-formula-range"_ustr;
    size_t fi = 0;
    const sal_Int32 maxOps = 64;
    for (sal_Int32 r = startRow; r <= endRow && static_cast<sal_Int32>(plan.operations.size()) < maxOps;
         ++r)
    {
        for (sal_Int32 c = startCol;
             c <= endCol && static_cast<sal_Int32>(plan.operations.size()) < maxOps; ++c)
        {
            if (fi >= rFormulas.size())
                break;
            OUString formula = rFormulas[fi++].trim();
            if (!formula.startsWith(u"="_ustr))
                formula = u"="_ustr + formula;
            DiffOperation op;
            op.opType = u"replace"_ustr;
            op.target = u"cell:"_ustr + colIndexToLettersLocal(c) + OUString::number(r + 1);
            if (fi == 1)
                op.oldText = rOldText;
            op.newText = formula;
            plan.operations.push_back(op);
        }
        if (fi >= rFormulas.size())
            break;
    }
    if (!plan.operations.empty())
        plan.rawOutput = plan.operations.front().newText;
    return plan;
}

ApplyPlan AgentChatDiffExtractor::extractOutlineSlidePlan(const OUString& rText)
{
    ApplyPlan plan;
    plan.planId = u"ap-outline-slides"_ustr;
    plan.rawOutput = rText;
    if (rText.isEmpty())
        return plan;

    struct SlideDraft
    {
        OUString title;
        std::vector<OUString> bullets;
        OUString notes; // 讲稿 / speaker notes
    };
    std::vector<SlideDraft> slides;

    auto isHeading = [](const OUString& line, OUString& rTitleOut) -> bool {
        OUString s = line.trim();
        if (s.isEmpty())
            return false;
        // ## 1. Title  /  ## Title
        if (s.startsWith(u"##"_ustr))
        {
            s = s.copy(2).trim();
            // drop leading number.
            sal_Int32 i = 0;
            while (i < s.getLength() && s[i] >= u'0' && s[i] <= u'9')
                ++i;
            if (i > 0 && i < s.getLength() && (s[i] == u'.' || s[i] == u'、' || s[i] == u')'))
            {
                ++i;
                while (i < s.getLength() && s[i] == u' ')
                    ++i;
                s = s.copy(i);
            }
            rTitleOut = s.trim();
            return !rTitleOut.isEmpty();
        }
        // 1. Title / 1、Title
        sal_Int32 i = 0;
        while (i < s.getLength() && s[i] >= u'0' && s[i] <= u'9')
            ++i;
        if (i > 0 && i < s.getLength()
            && (s[i] == u'.' || s[i] == u'、' || s[i] == u')' || s[i] == u':'))
        {
            ++i;
            while (i < s.getLength() && s[i] == u' ')
                ++i;
            rTitleOut = s.copy(i).trim();
            // Reject if it looks like a bullet-only line that is too short formula-like
            return !rTitleOut.isEmpty() && rTitleOut.getLength() < 80;
        }
        // 幻灯 N：Title
        if (s.startsWith(u"幻灯"_ustr) || s.startsWith(u"幻灯片"_ustr) || s.startsWith(u"Slide"_ustr)
            || s.startsWith(u"slide"_ustr))
        {
            sal_Int32 c = s.indexOf(u'：');
            if (c < 0)
                c = s.indexOf(u':');
            if (c >= 0)
            {
                rTitleOut = s.copy(c + 1).trim();
                return !rTitleOut.isEmpty();
            }
        }
        return false;
    };

    sal_Int32 pos = 0;
    while (pos <= rText.getLength())
    {
        sal_Int32 nl = rText.indexOf(u'\n', pos);
        if (nl < 0)
            nl = rText.getLength();
        OUString line = rText.copy(pos, nl - pos);
        // strip trailing CR
        if (!line.isEmpty() && line[line.getLength() - 1] == u'\r')
            line = line.copy(0, line.getLength() - 1);

        OUString title;
        if (isHeading(line, title))
        {
            slides.push_back(SlideDraft{ title, {}, OUString() });
        }
        else if (!slides.empty())
        {
            OUString b = line.trim();
            if (b.startsWith(u"讲稿"_ustr) || b.startsWith(u"备注"_ustr)
                || b.startsWith(u"Notes"_ustr) || b.startsWith(u"notes"_ustr)
                || b.startsWith(u"Speaker"_ustr))
            {
                sal_Int32 c = b.indexOf(u'：');
                if (c < 0)
                    c = b.indexOf(u':');
                OUString noteBody = (c >= 0) ? b.copy(c + 1).trim() : b;
                if (!noteBody.isEmpty())
                {
                    if (!slides.back().notes.isEmpty())
                        slides.back().notes += u" "_ustr;
                    slides.back().notes += noteBody;
                }
            }
            else if (b.startsWith(u"版式"_ustr) || b.startsWith(u"主题"_ustr)
                     || b.startsWith(u"配图"_ustr) || b.startsWith(u"插图"_ustr)
                     || b.startsWith(u"Layout"_ustr) || b.startsWith(u"layout"_ustr)
                     || b.startsWith(u"Theme"_ustr) || b.startsWith(u"Image"_ustr))
            {
                // Keep meta lines for impressFillSlide layout/theme/image parsing.
                slides.back().bullets.push_back(b);
            }
            else if (b.startsWith(u"-"_ustr) || b.startsWith(u"*"_ustr) || b.startsWith(u"•"_ustr)
                     || b.startsWith(u"·"_ustr))
            {
                sal_Int32 i = 0;
                while (i < b.getLength()
                       && (b[i] == u'-' || b[i] == u'*' || b[i] == u'•' || b[i] == u'·'
                           || b[i] == u' '))
                    ++i;
                b = b.copy(i).trim();
                if (!b.isEmpty())
                    slides.back().bullets.push_back(b);
            }
        }

        if (nl >= rText.getLength())
            break;
        pos = nl + 1;
    }

    sal_Int32 slideNo = 1;
    for (const auto& s : slides)
    {
        if (s.title.isEmpty())
            continue;
        OUStringBuffer body;
        body.append(s.title);
        for (const auto& bullet : s.bullets)
        {
            body.append(u'\n');
            body.append(bullet);
        }
        if (!s.notes.isEmpty())
        {
            body.append(u"\n讲稿："_ustr);
            body.append(s.notes);
        }
        DiffOperation op;
        op.opType = u"insert"_ustr;
        op.target = u"slide:"_ustr + OUString::number(slideNo);
        op.newText = body.makeStringAndClear();
        plan.operations.push_back(op);
        ++slideNo;
    }

    SAL_INFO("kqoffice.ai.chat",
             "extractOutlineSlidePlan: slides=" << plan.operations.size());
    return plan;
}

bool AgentChatDiffExtractor::looksLikeChartIntent(const OUString& rText)
{
    if (rText.isEmpty())
        return false;
    const OUString low = rText.toAsciiLowerCase();
    return low.indexOf(u"图表"_ustr) >= 0 || low.indexOf(u"chart"_ustr) >= 0
           || low.indexOf(u"柱状"_ustr) >= 0 || low.indexOf(u"饼图"_ustr) >= 0
           || low.indexOf(u"折线"_ustr) >= 0 || low.indexOf(u"条形"_ustr) >= 0
           || low.indexOf(u"柱形"_ustr) >= 0 || low.indexOf(u"散点"_ustr) >= 0
           || low.indexOf(u"bar chart"_ustr) >= 0 || low.indexOf(u"pie chart"_ustr) >= 0
           || low.indexOf(u"line chart"_ustr) >= 0 || low.indexOf(u"插入图"_ustr) >= 0
           || low.indexOf(u"可视化"_ustr) >= 0;
}

ApplyPlan AgentChatDiffExtractor::makeChartInsertPlan(const OUString& rRangeOrCell,
                                                      const OUString& rAdvice)
{
    ApplyPlan plan;
    plan.planId = u"ap-chart-insert"_ustr;
    plan.rawOutput = rAdvice;
    DiffOperation op;
    op.opType = u"chart_insert"_ustr;
    OUString tgt = rRangeOrCell.trim();
    if (tgt.isEmpty())
        tgt = u"selection"_ustr;
    // Normalize range:/cell: prefixes for display; dispatch uses live selection.
    op.target = tgt;
    op.newText = rAdvice.isEmpty()
                     ? u"Insert chart for current Calc selection (wizard after approval)"_ustr
                     : rAdvice;
    plan.operations.push_back(op);
    return plan;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
