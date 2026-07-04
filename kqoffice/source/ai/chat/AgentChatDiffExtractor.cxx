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

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
