/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W3 Day-1a: Apply Plan
 * Validator).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V2 W3 Day-1a — pure-logic validator for the m3-02 ApplyPlan schema.
 *
 * Authoritative spec: docs/schemas/apply-plan.schema.json (frozen V1.5
 * contract). The validator must reject every payload the JSON Schema
 * would reject, matching it field-by-field. No third-party JSON parser:
 * the same linear-scan style used in OllamaAdapter::parseModelsJson and
 * RecentStore::parseRecentJson keeps the build hermetic.
 *
 * The validator runs **before** any document mutation. Failure modes
 * map to ValidationCode so a localized UI layer can surface the right
 * "stale revision" / "schema mismatch" message without re-implementing
 * the schema itself.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_APPLYPLANVALIDATOR_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_APPLYPLANVALIDATOR_HXX

#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <kqoffice/source/ai/i18n/AiI18nStrings.hxx>

namespace kqoffice::ai
{
enum class ApplyPlanValidationCode
{
    Ok,
    NotJsonObject,           ///< body is empty / does not start with '{'
    MissingField,            ///< required key absent
    SchemaVersionMismatch,   ///< schema_version != "m3-02"
    IdPatternMismatch,       ///< id / preview_action_id / capability_id bad
    RevisionPreconditionBad, ///< revision_precondition != "document-revision-match"
    DeterministicNotTrue,
    RollbackRequiredNotTrue,
    UndoGroupModeBad,        ///< undo_group.mode != "one-user-action"
    UndoLabelEmpty,          ///< undo_group.label_zh empty
    FailureBehaviorBad,      ///< document_mutation_on_failure != "none"
    FailureMessageEmpty,     ///< failure_behavior.message_zh empty
    OperationSummaryEmpty,   ///< operation_summary_zh empty
    RepeatedDiagnosticsBad,  ///< repeated_diagnostics_required != true
};

struct ApplyPlanValidationResult
{
    ApplyPlanValidationCode code = ApplyPlanValidationCode::Ok;
    OUString errorPath; ///< JSON pointer-style: "/undo_group/label_zh"

    bool ok() const { return code == ApplyPlanValidationCode::Ok; }
};

/// Header-only so the cppunit test can validate inline JSON literals
/// without dragging libkqoffice_ai into the test binary.
class ApplyPlanValidator
{
public:
    static inline ApplyPlanValidationResult validate(const OString& body);
};

/// Map a ValidationResult to a localized zh-CN toast string suitable for
/// display by SwView / status-bar surfaces. Header-only so the same
/// function can be exercised under cppunit (pure-logic) and from the
/// future SwDocShell::applyDiagnosticsPlan call site without an extra
/// dispatch layer.
///
/// The message is intentionally short and includes the `errorPath` when
/// non-empty so the user can see *which* field tripped the guard. UI
/// layers that need richer styling (icon, bold field name) can re-render
/// using the `code` directly; this helper is the canonical fallback.
inline OUString applyPlanValidationMessage(
    const ApplyPlanValidationResult& r);

/// Map a ValidationCode to its short stable status string used in the
/// evidence record `status` field. Distinct from the localized toast:
/// these tokens are kebab-case ASCII so audit tooling can grep them
/// regardless of the user's locale. Returns "ok" for success.
inline OString applyPlanValidationStatus(ApplyPlanValidationCode code);

namespace detail
{
inline void apv_skipWs(const char* s, sal_Int32 n, sal_Int32& i)
{
    while (i < n
           && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
        ++i;
}

/// Locate the first occurrence of a top-level `"key"` followed (after
/// whitespace) by a `:`. Returns the index of the byte AFTER the colon
/// (i.e. start of the value), or -1 if absent. Quote-aware: a literal
/// `"key"` inside another string value is skipped. Brace-aware: nested
/// objects are not descended (top-level lookup only).
inline sal_Int32 apv_findTopKey(const OString& body, const char* keyQuoted)
{
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();
    const sal_Int32 keyLen = static_cast<sal_Int32>(::strlen(keyQuoted));

    // Find the opening '{'.
    sal_Int32 i = 0;
    apv_skipWs(s, n, i);
    if (i >= n || s[i] != '{')
        return -1;
    ++i;

    int depth = 1;
    bool inStr = false;
    while (i < n)
    {
        const char c = s[i];
        if (inStr)
        {
            if (c == '\\' && i + 1 < n) { i += 2; continue; }
            if (c == '"') inStr = false;
            ++i;
            continue;
        }
        if (c == '"')
        {
            // Possible key. Only at depth 1 do we test for a match.
            if (depth == 1 && i + keyLen <= n
                && body.match(keyQuoted, i))
            {
                sal_Int32 afterKey = i + keyLen;
                sal_Int32 j = afterKey;
                apv_skipWs(s, n, j);
                if (j < n && s[j] == ':')
                {
                    return j + 1;
                }
            }
            inStr = true;
            ++i;
            continue;
        }
        if (c == '{') { ++depth; ++i; continue; }
        if (c == '}') { --depth; ++i; if (depth == 0) break; continue; }
        ++i;
    }
    return -1;
}

/// Read a JSON string literal at `from` (must point to the opening `"`,
/// allowing leading whitespace). On success returns the decoded string
/// and advances *outEnd to one past the closing `"`. Returns isPresent
/// = false when the value is not a string (null/number/etc.).
struct apv_StringRead
{
    bool isPresent = false;
    OString value;
};

inline apv_StringRead apv_readString(const OString& body, sal_Int32 from)
{
    apv_StringRead r;
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();
    sal_Int32 i = from;
    apv_skipWs(s, n, i);
    if (i >= n || s[i] != '"') return r;
    ++i;
    OStringBuffer val;
    while (i < n)
    {
        const char c = s[i];
        if (c == '\\' && i + 1 < n)
        {
            const char esc = s[i + 1];
            switch (esc)
            {
                case '"':  val.append('"'); break;
                case '\\': val.append('\\'); break;
                case '/':  val.append('/'); break;
                case 'n':  val.append('\n'); break;
                case 't':  val.append('\t'); break;
                case 'r':  val.append('\r'); break;
                case 'b':  val.append('\b'); break;
                case 'f':  val.append('\f'); break;
                default:   val.append(esc); break;
            }
            i += 2;
            continue;
        }
        if (c == '"') break;
        val.append(c);
        ++i;
    }
    if (i >= n) return r; // truncated
    r.isPresent = true;
    r.value = val.makeStringAndClear();
    return r;
}

/// Read a literal `true` / `false` token at `from`. Returns
/// {found, value}. Whitespace before the token is skipped.
struct apv_BoolRead
{
    bool isPresent = false;
    bool value = false;
};

inline apv_BoolRead apv_readBool(const OString& body, sal_Int32 from)
{
    apv_BoolRead r;
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();
    sal_Int32 i = from;
    apv_skipWs(s, n, i);
    if (i + 4 <= n && body.match("true", i))
    {
        r.isPresent = true; r.value = true; return r;
    }
    if (i + 5 <= n && body.match("false", i))
    {
        r.isPresent = true; r.value = false; return r;
    }
    return r;
}

/// Validate the schema's id pattern: ^[a-z0-9][a-z0-9.-]{2,80}$
inline bool apv_idPatternOk(const OString& s)
{
    const sal_Int32 n = s.getLength();
    if (n < 3 || n > 81) return false;
    auto isHead = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    auto isBody = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '.' || c == '-';
    };
    if (!isHead(s[0])) return false;
    for (sal_Int32 i = 1; i < n; ++i)
        if (!isBody(s[i])) return false;
    return true;
}
} // namespace detail

inline ApplyPlanValidationResult ApplyPlanValidator::validate(
    const OString& body)
{
    using detail::apv_findTopKey;
    using detail::apv_readString;
    using detail::apv_readBool;
    using detail::apv_idPatternOk;

    ApplyPlanValidationResult res;

    // Cheap sanity: must be a JSON object.
    sal_Int32 i = 0;
    detail::apv_skipWs(body.getStr(), body.getLength(), i);
    if (i >= body.getLength() || body[i] != '{')
    {
        res.code = ApplyPlanValidationCode::NotJsonObject;
        return res;
    }

    // schema_version must be the literal "m3-02".
    {
        sal_Int32 p = apv_findTopKey(body, "\"schema_version\"");
        if (p < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/schema_version"_ustr;
            return res;
        }
        auto sv = apv_readString(body, p);
        if (!sv.isPresent || sv.value != "m3-02")
        {
            res.code = ApplyPlanValidationCode::SchemaVersionMismatch;
            res.errorPath = u"/schema_version"_ustr;
            return res;
        }
    }

    // id pattern
    auto checkIdField = [&](const char* keyQuoted, const OUString& path)
        -> ApplyPlanValidationResult {
        ApplyPlanValidationResult r;
        sal_Int32 p = apv_findTopKey(body, keyQuoted);
        if (p < 0)
        {
            r.code = ApplyPlanValidationCode::MissingField;
            r.errorPath = path;
            return r;
        }
        auto sv = apv_readString(body, p);
        if (!sv.isPresent || !apv_idPatternOk(sv.value))
        {
            r.code = ApplyPlanValidationCode::IdPatternMismatch;
            r.errorPath = path;
            return r;
        }
        return r;
    };
    if (auto r = checkIdField("\"id\"", u"/id"_ustr); !r.ok()) return r;
    if (auto r = checkIdField("\"preview_action_id\"",
                              u"/preview_action_id"_ustr); !r.ok())
        return r;
    if (auto r = checkIdField("\"capability_id\"",
                              u"/capability_id"_ustr); !r.ok())
        return r;

    // revision_precondition const
    {
        sal_Int32 p = apv_findTopKey(body, "\"revision_precondition\"");
        if (p < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/revision_precondition"_ustr;
            return res;
        }
        auto sv = apv_readString(body, p);
        if (!sv.isPresent || sv.value != "document-revision-match")
        {
            res.code = ApplyPlanValidationCode::RevisionPreconditionBad;
            res.errorPath = u"/revision_precondition"_ustr;
            return res;
        }
    }

    // operation_summary_zh required + non-empty
    {
        sal_Int32 p = apv_findTopKey(body, "\"operation_summary_zh\"");
        if (p < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/operation_summary_zh"_ustr;
            return res;
        }
        auto sv = apv_readString(body, p);
        if (!sv.isPresent || sv.value.isEmpty())
        {
            res.code = ApplyPlanValidationCode::OperationSummaryEmpty;
            res.errorPath = u"/operation_summary_zh"_ustr;
            return res;
        }
    }

    // deterministic === true
    {
        sal_Int32 p = apv_findTopKey(body, "\"deterministic\"");
        if (p < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/deterministic"_ustr;
            return res;
        }
        auto bv = apv_readBool(body, p);
        if (!bv.isPresent || !bv.value)
        {
            res.code = ApplyPlanValidationCode::DeterministicNotTrue;
            res.errorPath = u"/deterministic"_ustr;
            return res;
        }
    }

    // rollback_required === true
    {
        sal_Int32 p = apv_findTopKey(body, "\"rollback_required\"");
        if (p < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/rollback_required"_ustr;
            return res;
        }
        auto bv = apv_readBool(body, p);
        if (!bv.isPresent || !bv.value)
        {
            res.code = ApplyPlanValidationCode::RollbackRequiredNotTrue;
            res.errorPath = u"/rollback_required"_ustr;
            return res;
        }
    }

    // undo_group is itself an object — descend by slicing.
    {
        sal_Int32 p = apv_findTopKey(body, "\"undo_group\"");
        if (p < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/undo_group"_ustr;
            return res;
        }
        // Find the opening { after `:`.
        sal_Int32 n = body.getLength();
        const char* s = body.getStr();
        sal_Int32 q = p;
        detail::apv_skipWs(s, n, q);
        if (q >= n || s[q] != '{')
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/undo_group"_ustr;
            return res;
        }
        // Slice through the matching brace, depth + string aware.
        sal_Int32 sliceStart = q;
        int depth = 0;
        bool inStr = false;
        sal_Int32 sliceEnd = -1;
        for (sal_Int32 j = q; j < n; ++j)
        {
            const char c = s[j];
            if (inStr)
            {
                if (c == '\\' && j + 1 < n) { ++j; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') { ++depth; continue; }
            if (c == '}') { --depth; if (depth == 0) { sliceEnd = j + 1; break; } }
        }
        if (sliceEnd < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/undo_group"_ustr;
            return res;
        }
        OString slice = body.copy(sliceStart, sliceEnd - sliceStart);

        sal_Int32 mp = apv_findTopKey(slice, "\"mode\"");
        if (mp < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/undo_group/mode"_ustr;
            return res;
        }
        auto mv = apv_readString(slice, mp);
        if (!mv.isPresent || mv.value != "one-user-action")
        {
            res.code = ApplyPlanValidationCode::UndoGroupModeBad;
            res.errorPath = u"/undo_group/mode"_ustr;
            return res;
        }
        sal_Int32 lp = apv_findTopKey(slice, "\"label_zh\"");
        if (lp < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/undo_group/label_zh"_ustr;
            return res;
        }
        auto lv = apv_readString(slice, lp);
        if (!lv.isPresent || lv.value.isEmpty())
        {
            res.code = ApplyPlanValidationCode::UndoLabelEmpty;
            res.errorPath = u"/undo_group/label_zh"_ustr;
            return res;
        }
    }

    // failure_behavior subobject
    {
        sal_Int32 p = apv_findTopKey(body, "\"failure_behavior\"");
        if (p < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/failure_behavior"_ustr;
            return res;
        }
        sal_Int32 n = body.getLength();
        const char* s = body.getStr();
        sal_Int32 q = p;
        detail::apv_skipWs(s, n, q);
        if (q >= n || s[q] != '{')
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/failure_behavior"_ustr;
            return res;
        }
        sal_Int32 sliceStart = q;
        int depth = 0;
        bool inStr = false;
        sal_Int32 sliceEnd = -1;
        for (sal_Int32 j = q; j < n; ++j)
        {
            const char c = s[j];
            if (inStr)
            {
                if (c == '\\' && j + 1 < n) { ++j; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') { ++depth; continue; }
            if (c == '}') { --depth; if (depth == 0) { sliceEnd = j + 1; break; } }
        }
        if (sliceEnd < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/failure_behavior"_ustr;
            return res;
        }
        OString slice = body.copy(sliceStart, sliceEnd - sliceStart);

        sal_Int32 dmp = apv_findTopKey(slice,
            "\"document_mutation_on_failure\"");
        if (dmp < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/failure_behavior/document_mutation_on_failure"_ustr;
            return res;
        }
        auto dmv = apv_readString(slice, dmp);
        if (!dmv.isPresent || dmv.value != "none")
        {
            res.code = ApplyPlanValidationCode::FailureBehaviorBad;
            res.errorPath = u"/failure_behavior/document_mutation_on_failure"_ustr;
            return res;
        }
        sal_Int32 mzp = apv_findTopKey(slice, "\"message_zh\"");
        if (mzp < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/failure_behavior/message_zh"_ustr;
            return res;
        }
        auto mzv = apv_readString(slice, mzp);
        if (!mzv.isPresent || mzv.value.isEmpty())
        {
            res.code = ApplyPlanValidationCode::FailureMessageEmpty;
            res.errorPath = u"/failure_behavior/message_zh"_ustr;
            return res;
        }
    }

    // repeated_diagnostics_required === true
    {
        sal_Int32 p = apv_findTopKey(body,
            "\"repeated_diagnostics_required\"");
        if (p < 0)
        {
            res.code = ApplyPlanValidationCode::MissingField;
            res.errorPath = u"/repeated_diagnostics_required"_ustr;
            return res;
        }
        auto bv = apv_readBool(body, p);
        if (!bv.isPresent || !bv.value)
        {
            res.code = ApplyPlanValidationCode::RepeatedDiagnosticsBad;
            res.errorPath = u"/repeated_diagnostics_required"_ustr;
            return res;
        }
    }

    return res; // ok
}

inline OUString applyPlanValidationMessage(
    const ApplyPlanValidationResult& r)
{
    using Code = ApplyPlanValidationCode;
    using kqoffice::ai::i18n::get;
    // Base message per code via i18n string provider. The errorPath
    // (when non-empty) is appended using the locale-aware field suffix
    // format so the user sees which field failed without us needing a
    // per-path template explosion.
    OUString base;
    switch (r.code)
    {
        case Code::Ok:
            return u""_ustr; // no toast on success
        case Code::NotJsonObject:
            base = get(u"applyplan.error.not_json_long"_ustr);
            break;
        case Code::MissingField:
            base = get(u"applyplan.error.missing_field_long"_ustr);
            break;
        case Code::SchemaVersionMismatch:
            base = get(u"applyplan.error.schema_mismatch_long"_ustr);
            break;
        case Code::IdPatternMismatch:
            base = get(u"applyplan.error.id_pattern_long"_ustr);
            break;
        case Code::RevisionPreconditionBad:
            base = get(u"applyplan.error.revision_bad_long"_ustr);
            break;
        case Code::DeterministicNotTrue:
            base = get(u"applyplan.error.deterministic"_ustr);
            break;
        case Code::RollbackRequiredNotTrue:
            base = get(u"applyplan.error.rollback_required"_ustr);
            break;
        case Code::UndoGroupModeBad:
            base = get(u"applyplan.error.undo_group_mode"_ustr);
            break;
        case Code::UndoLabelEmpty:
            base = get(u"applyplan.error.undo_label_empty"_ustr);
            break;
        case Code::FailureBehaviorBad:
            base = get(u"applyplan.error.failure_behavior_bad"_ustr);
            break;
        case Code::FailureMessageEmpty:
            base = get(u"applyplan.error.failure_empty"_ustr);
            break;
        case Code::OperationSummaryEmpty:
            base = get(u"applyplan.error.summary_empty"_ustr);
            break;
        case Code::RepeatedDiagnosticsBad:
            base = get(u"applyplan.error.repeated_diagnostics"_ustr);
            break;
    }
    if (r.errorPath.isEmpty())
        return base;
    return base + kqoffice::ai::i18n::format(u"i18n.field_suffix"_ustr, r.errorPath);
}

inline OString applyPlanValidationStatus(ApplyPlanValidationCode code)
{
    using Code = ApplyPlanValidationCode;
    switch (code)
    {
        case Code::Ok:                       return "ok"_ostr;
        case Code::NotJsonObject:            return "apply-plan-not-json"_ostr;
        case Code::MissingField:             return "apply-plan-missing-field"_ostr;
        case Code::SchemaVersionMismatch:    return "apply-plan-schema-version-mismatch"_ostr;
        case Code::IdPatternMismatch:        return "apply-plan-id-pattern"_ostr;
        case Code::RevisionPreconditionBad:  return "apply-plan-revision-precondition"_ostr;
        case Code::DeterministicNotTrue:     return "apply-plan-deterministic-false"_ostr;
        case Code::RollbackRequiredNotTrue:  return "apply-plan-rollback-false"_ostr;
        case Code::UndoGroupModeBad:         return "apply-plan-undo-group-mode"_ostr;
        case Code::UndoLabelEmpty:           return "apply-plan-undo-label-empty"_ostr;
        case Code::FailureBehaviorBad:       return "apply-plan-failure-behavior"_ostr;
        case Code::FailureMessageEmpty:      return "apply-plan-failure-message-empty"_ostr;
        case Code::OperationSummaryEmpty:    return "apply-plan-operation-summary-empty"_ostr;
        case Code::RepeatedDiagnosticsBad:   return "apply-plan-repeated-diagnostics"_ostr;
    }
    // Unreachable under -Werror=switch; defensive default for debug builds.
    return "apply-plan-unknown"_ostr;
}

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
