/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5 Day-0: Async Cowork Task Store).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskStore.hxx"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::cowork
{
namespace
{
OUString envVar(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v) return OUString();
    return OUString(v, std::strlen(v), RTL_TEXTENCODING_UTF8);
}

/// Minimal JSON string escape — mirrors EvidenceRecorder::appendEscaped
/// so the two stores produce byte-comparable envelopes for fields they
/// share in shape (evidence id strings, timestamps, etc.).
void appendEscaped(OUStringBuffer& buf, const OUString& s)
{
    buf.append('"');
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        sal_Unicode c = s[i];
        switch (c)
        {
            case '"':  buf.append("\\\""); break;
            case '\\': buf.append("\\\\"); break;
            case '\n': buf.append("\\n");  break;
            case '\r': buf.append("\\r");  break;
            case '\t': buf.append("\\t");  break;
            default:
                if (c < 0x20)
                {
                    char tmp[8];
                    std::snprintf(tmp, sizeof(tmp), "\\u%04x",
                                  static_cast<unsigned>(c));
                    buf.appendAscii(tmp);
                }
                else
                {
                    buf.append(c);
                }
        }
    }
    buf.append('"');
}

OUString ensureMonthDir(const OUString& root, const OUString& monthDir)
{
    OUString dir = root + "/" + monthDir;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url)
            != osl::FileBase::E_None)
        return OUString();
    osl::FileBase::RC rc = osl::Directory::createPath(url);
    if (rc != osl::FileBase::E_None && rc != osl::FileBase::E_EXIST)
        return OUString();
    return dir;
}

OUString currentMonthDir()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    std::time_t secs = static_cast<std::time_t>(tv.Seconds);
    std::tm utc{};
    gmtime_r(&secs, &utc);
    char ym[8];
    std::snprintf(ym, sizeof(ym), "%04d-%02d",
                  utc.tm_year + 1900, utc.tm_mon + 1);
    return OUString::createFromAscii(ym);
}

/// Extract the key from a line like `"key": ...`. Returns empty on
/// failure. Input is the trimmed, trailing-comma-stripped line body.
OUString extractKey(const OString& line)
{
    if (line.isEmpty() || line[0] != '"') return OUString();
    sal_Int32 q2 = line.indexOf('"', 1);
    if (q2 < 0) return OUString();
    return OStringToOUString(line.copy(1, q2 - 1), RTL_TEXTENCODING_UTF8);
}

/// Extract a JSON string literal value from a line like
/// `"key": "value with \"escapes\""`. Returns empty on any parse
/// failure OR on literal `null`. Handles standard JSON escapes and
/// \uXXXX for sub-0x80 points (Day-0 writer never emits non-ASCII \u).
OUString extractStringValue(const OString& line)
{
    sal_Int32 colon = line.indexOf(':');
    if (colon < 0) return OUString();
    // locate opening quote after ': '
    sal_Int32 start = -1;
    for (sal_Int32 i = colon + 1; i < line.getLength(); ++i)
    {
        char c = line[i];
        if (c == '"') { start = i + 1; break; }
        if (c != ' ' && c != '\t') return OUString(); // e.g. `null` / number
    }
    if (start < 0) return OUString();

    OStringBuffer raw;
    bool escape = false;
    bool closed = false;
    for (sal_Int32 i = start; i < line.getLength(); ++i)
    {
        char c = line[i];
        if (escape)
        {
            switch (c)
            {
                case '"':  raw.append('"');  break;
                case '\\': raw.append('\\'); break;
                case '/':  raw.append('/');  break;
                case 'n':  raw.append('\n'); break;
                case 'r':  raw.append('\r'); break;
                case 't':  raw.append('\t'); break;
                case 'u':
                {
                    if (i + 4 >= line.getLength()) return OUString();
                    char hex[5] = { line[i+1], line[i+2], line[i+3],
                                    line[i+4], '\0' };
                    unsigned v = 0;
                    std::sscanf(hex, "%x", &v);
                    if (v < 0x80)
                        raw.append(static_cast<char>(v));
                    else
                        return OUString(); // Day-0 writer never emits this
                    i += 4;
                    break;
                }
                default: raw.append(c); break;
            }
            escape = false;
        }
        else if (c == '\\')
        {
            escape = true;
        }
        else if (c == '"')
        {
            closed = true;
            break;
        }
        else
        {
            raw.append(c);
        }
    }
    if (!closed) return OUString();
    return OStringToOUString(raw.makeStringAndClear(),
                             RTL_TEXTENCODING_UTF8);
}

sal_Int32 extractIntValue(const OString& line)
{
    sal_Int32 colon = line.indexOf(':');
    if (colon < 0) return 0;
    OString rest = line.copy(colon + 1);
    sal_Int32 comma = rest.indexOf(',');
    if (comma >= 0) rest = rest.copy(0, comma);
    return rest.trim().toInt32();
}

/// Parse an inline `"key": ["a", "b", ...]` array into `out`. Expects
/// `data` to be the full line body with the brackets on the same line,
/// matching the Day-0 writer's compact output for `source_docs` and
/// `evidence_ids`.
void parseStringArray(const OString& data, std::vector<OUString>& out)
{
    sal_Int32 lb = data.indexOf('[');
    sal_Int32 rb = data.lastIndexOf(']');
    if (lb < 0 || rb <= lb) return;
    OString inner = data.copy(lb + 1, rb - lb - 1);
    sal_Int32 p = 0;
    while (p < inner.getLength())
    {
        sal_Int32 q1 = inner.indexOf('"', p);
        if (q1 < 0) break;
        sal_Int32 q2 = q1 + 1;
        while (q2 < inner.getLength())
        {
            if (inner[q2] == '\\') { q2 += 2; continue; }
            if (inner[q2] == '"')  break;
            ++q2;
        }
        if (q2 >= inner.getLength()) break;
        out.push_back(OStringToOUString(inner.copy(q1 + 1, q2 - q1 - 1),
                                        RTL_TEXTENCODING_UTF8));
        p = q2 + 1;
    }
}

} // namespace

// --- Public API -------------------------------------------------------

OUString TaskStore::resolveRootDir()
{
    // 1. explicit test / deployment override
    OUString d = envVar("KQOFFICE_AI_TASKS_DIR");
    if (!d.isEmpty()) return d;

    // 2. TMPDIR (macOS always sets this; linux usually does)
    OUString tmp = envVar("TMPDIR");
    if (!tmp.isEmpty())
    {
        while (tmp.endsWith("/"))
            tmp = tmp.copy(0, tmp.getLength() - 1);
        return tmp + "/kqoffice-ai-tasks";
    }

    // 3. last resort
    return u"/tmp/kqoffice-ai-tasks"_ustr;
}

bool TaskStore::write(const AsyncTaskEnvelope& env)
{
    // Month dir from createdAt's first 7 chars when plausibly YYYY-MM;
    // fall back to current UTC otherwise.
    OUString monthDir;
    if (env.createdAt.getLength() >= 7 && env.createdAt[4] == '-')
        monthDir = env.createdAt.copy(0, 7);
    else
        monthDir = currentMonthDir();

    OUString root = resolveRootDir();
    OUString dir = ensureMonthDir(root, monthDir);
    if (dir.isEmpty()) return false;

    OUString path = dir + "/" + env.taskId + ".json";

    OUStringBuffer body(512);
    body.append("{\n");
    body.append("  \"schema_version\": ");
    body.append(env.schemaVersion);
    body.append(",\n  \"task_id\": ");
    appendEscaped(body, env.taskId);
    body.append(",\n  \"kind\": ");
    appendEscaped(body, taskKindToken(env.kind));
    body.append(",\n  \"state\": ");
    appendEscaped(body, taskStateToken(env.state));
    body.append(",\n  \"title\": ");
    appendEscaped(body, env.title);
    body.append(",\n  \"created_at\": ");
    appendEscaped(body, env.createdAt);
    body.append(",\n  \"updated_at\": ");
    appendEscaped(body, env.updatedAt);
    body.append(",\n  \"service_mode\": ");
    appendEscaped(body, env.serviceMode);

    // input object — one line per sub-field; arrays stay inline
    body.append(",\n  \"input\": {\n");
    body.append("    \"source_docs\": [");
    for (size_t i = 0; i < env.sourceDocs.size(); ++i)
    {
        if (i > 0) body.append(", ");
        appendEscaped(body, env.sourceDocs[i]);
    }
    body.append("],\n");
    body.append("    \"user_prompt\": ");
    appendEscaped(body, env.userPrompt);
    body.append(",\n    \"target_template\": ");
    appendEscaped(body, env.targetTemplate);
    body.append("\n  }");

    // steps array — compact when empty, one item per pretty-printed block otherwise
    if (env.steps.empty())
    {
        body.append(",\n  \"steps\": []");
    }
    else
    {
        body.append(",\n  \"steps\": [");
        for (size_t i = 0; i < env.steps.size(); ++i)
        {
            if (i > 0) body.append(",");
            body.append("\n    {\n");
            body.append("      \"step_id\": ");
            appendEscaped(body, env.steps[i].stepId);
            body.append(",\n      \"title\": ");
            appendEscaped(body, env.steps[i].title);
            body.append(",\n      \"state\": ");
            appendEscaped(body, taskStepStateToken(env.steps[i].state));
            if (!env.steps[i].evidenceId.isEmpty())
            {
                body.append(",\n      \"evidence\": ");
                appendEscaped(body, env.steps[i].evidenceId);
            }
            body.append("\n    }");
        }
        body.append("\n  ]");
    }

    // result_plan_id — null vs string
    body.append(",\n  \"result_plan_id\": ");
    if (env.resultPlanId.isEmpty())
        body.append("null");
    else
        appendEscaped(body, env.resultPlanId);

    // evidence_ids — inline compact array
    body.append(",\n  \"evidence_ids\": [");
    for (size_t i = 0; i < env.evidenceIds.size(); ++i)
    {
        if (i > 0) body.append(", ");
        appendEscaped(body, env.evidenceIds[i]);
    }
    body.append("]");

    // failure_reason — emitted only when non-empty (schema-optional)
    if (!env.failureReason.isEmpty())
    {
        body.append(",\n  \"failure_reason\": ");
        appendEscaped(body, env.failureReason);
    }

    body.append("\n}\n");

    OString utf8 = OUStringToOString(body.makeStringAndClear(),
                                     RTL_TEXTENCODING_UTF8);

    OString sysPath = OUStringToOString(path, RTL_TEXTENCODING_UTF8);
    std::FILE* fp = std::fopen(sysPath.getStr(), "wb");
    if (!fp) return false;
    std::fwrite(utf8.getStr(), 1, utf8.getLength(), fp);
    std::fclose(fp);
    return true;
}

bool TaskStore::read(const OUString& monthDir,
                     const OUString& taskId,
                     AsyncTaskEnvelope& out)
{
    OUString root = resolveRootDir();
    OUString path = root + "/" + monthDir + "/" + taskId + ".json";
    OString sysPath = OUStringToOString(path, RTL_TEXTENCODING_UTF8);
    std::FILE* fp = std::fopen(sysPath.getStr(), "rb");
    if (!fp) return false;

    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz < 0) { std::fclose(fp); return false; }
    std::vector<char> buf(static_cast<size_t>(sz));
    if (sz > 0)
        std::fread(buf.data(), 1, static_cast<size_t>(sz), fp);
    std::fclose(fp);
    OString contents(buf.data(), static_cast<sal_Int32>(sz));

    out = AsyncTaskEnvelope();

    // Line-by-line walk. The writer's indentation encodes scope exactly:
    //   2-space field       → top-level
    //   `"input": {`        → enters input object until matching `}`
    //   `"steps": [`        → enters steps array until matching `]`
    //   `{` inside steps    → step item until matching `}` / `},`
    bool inInput = false;
    bool inSteps = false;
    bool inStepItem = false;
    TaskStep currentStep;

    sal_Int32 lineStart = 0;
    for (sal_Int32 i = 0; i <= contents.getLength(); ++i)
    {
        if (i < contents.getLength() && contents[i] != '\n')
            continue;

        OString line = contents.copy(lineStart, i - lineStart);
        lineStart = i + 1;
        OString body = line.trim();
        if (body.isEmpty()) continue;

        OString data = body;
        if (data.endsWith(","))
            data = data.copy(0, data.getLength() - 1);

        // --- Closures ---------------------------------------------------
        if (data == "}")
        {
            if (inStepItem)
            {
                out.steps.push_back(currentStep);
                currentStep = TaskStep();
                inStepItem = false;
            }
            else if (inInput)
            {
                inInput = false;
            }
            // else top-level doc close — ignore
            continue;
        }
        if (data == "]")
        {
            if (inSteps && !inStepItem)
                inSteps = false;
            continue;
        }
        if (data == "{")
        {
            if (inSteps && !inStepItem)
            {
                inStepItem = true;
                currentStep = TaskStep();
            }
            continue;
        }

        // --- Openings ---------------------------------------------------
        if (data == "\"input\": {")
        {
            inInput = true;
            continue;
        }
        if (data == "\"steps\": [")
        {
            inSteps = true;
            continue;
        }
        if (data == "\"steps\": []")
            continue;

        OUString key = extractKey(data);
        if (key.isEmpty()) continue;

        if (inStepItem)
        {
            if (key == "step_id")
                currentStep.stepId = extractStringValue(data);
            else if (key == "title")
                currentStep.title = extractStringValue(data);
            else if (key == "state")
                parseTaskStepState(extractStringValue(data), currentStep.state);
            else if (key == "evidence")
                currentStep.evidenceId = extractStringValue(data);
        }
        else if (inInput)
        {
            if (key == "source_docs")
                parseStringArray(data, out.sourceDocs);
            else if (key == "user_prompt")
                out.userPrompt = extractStringValue(data);
            else if (key == "target_template")
                out.targetTemplate = extractStringValue(data);
        }
        else
        {
            if (key == "schema_version")
                out.schemaVersion = extractIntValue(data);
            else if (key == "task_id")
                out.taskId = extractStringValue(data);
            else if (key == "kind")
                parseTaskKind(extractStringValue(data), out.kind);
            else if (key == "state")
                parseTaskState(extractStringValue(data), out.state);
            else if (key == "title")
                out.title = extractStringValue(data);
            else if (key == "created_at")
                out.createdAt = extractStringValue(data);
            else if (key == "updated_at")
                out.updatedAt = extractStringValue(data);
            else if (key == "service_mode")
                out.serviceMode = extractStringValue(data);
            else if (key == "result_plan_id")
                out.resultPlanId = extractStringValue(data); // empty on `null`
            else if (key == "failure_reason")
                out.failureReason = extractStringValue(data);
            else if (key == "evidence_ids")
                parseStringArray(data, out.evidenceIds);
        }
    }

    return !out.taskId.isEmpty();
}

std::vector<OUString> TaskStore::listByState(const OUString& monthDir,
                                             TaskState state)
{
    std::vector<OUString> result;
    OUString root = resolveRootDir();
    OUString dir = root + "/" + monthDir;

    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url)
            != osl::FileBase::E_None)
        return result;

    osl::Directory d(url);
    if (d.open() != osl::FileBase::E_None) return result;

    osl::DirectoryItem item;
    while (d.getNextItem(item) == osl::FileBase::E_None)
    {
        osl::FileStatus st(osl_FileStatus_Mask_FileName);
        if (item.getFileStatus(st) != osl::FileBase::E_None) continue;
        OUString name = st.getFileName();
        if (!name.endsWith(".json")) continue;
        OUString taskId = name.copy(0, name.getLength() - 5);

        AsyncTaskEnvelope env;
        if (read(monthDir, taskId, env) && env.state == state)
            result.push_back(taskId);
    }
    d.close();
    return result;
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
