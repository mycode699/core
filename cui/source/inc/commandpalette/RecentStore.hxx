/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2 Day-1a: Recent Store).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V2 W2 Day-1a — recent-command persistence for Cmd+K palette ranking.
 *
 * Spec: docs/product/v2/w2-cmd-palette-spec.md §"历史与个性化".
 *   File: ${UserInstallation}/cmdpalette/recent.json
 *   Schema:
 *     {
 *       "version": 1,
 *       "entries": [
 *         {"unoCommand": ".uno:Bold", "lastUsed": "...", "useCount": 42}
 *       ]
 *     }
 *
 * Pure parser/serializer is header-only so cppunit links without libcui;
 * disk-bound load/save lives in RecentStore.cxx.
 */

#ifndef INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_RECENTSTORE_HXX
#define INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_RECENTSTORE_HXX

#include <commandpalette/FuzzyMatcher.hxx>

#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <algorithm>
#include <vector>

namespace cui::commandpalette
{
struct RecentEntry
{
    OUString unoCommand;     ///< ".uno:Bold"
    OUString lastUsed;       ///< ISO-8601 timestamp string, opaque to ranker
    sal_Int32 useCount = 0;  ///< monotonically incremented on dispatch
};

class RecentStore
{
public:
    /// Parse recent.json. Returns the entries vector; on any error
    /// (missing version, malformed JSON, wrong types) returns empty —
    /// callers treat empty as "no history yet".
    static inline std::vector<RecentEntry> parseRecentJson(
        const OString& body);

    /// Serialize entries to the canonical recent.json layout with
    /// version=1 and pretty-ish two-space indentation. Stable enough
    /// to round-trip through parseRecentJson.
    static inline OString serializeRecentJson(
        const std::vector<RecentEntry>& entries);

    /// Bump useCount for `unoCommand` (creates entry if absent),
    /// stamps lastUsed, then sorts the result by useCount desc / last
    /// use desc and caps at `maxEntries`. Pure function — caller
    /// re-saves on success.
    static inline std::vector<RecentEntry> bump(
        std::vector<RecentEntry> entries,
        const OUString& unoCommand,
        const OUString& nowIso,
        std::size_t maxEntries = 50);

    /// Apply recent useCounts to a corpus by writing into
    /// CommandEntry.frequency. Frequency is `useCount * 10` so the
    /// existing FuzzyMatcher recency boost (frequency / 10) maps 1:1
    /// to use count without a second knob.
    static inline void applyFrequencies(
        std::vector<CommandEntry>& corpus,
        const std::vector<RecentEntry>& recents);

    /// Load entries from `${UserInstallation}/cmdpalette/recent.json`.
    /// Empty vector on any error.
    static std::vector<RecentEntry> loadFromUser(
        const OUString& userInstallation);

    /// Persist entries to `${UserInstallation}/cmdpalette/recent.json`.
    /// Best-effort: returns false on any I/O error.
    static bool saveToUser(const OUString& userInstallation,
                           const std::vector<RecentEntry>& entries);

    /// Load → bump useCount for `unoCommand` → save. Best-effort; no-op
    /// when `unoCommand` is empty. Called from the palette popover after
    /// a successful dispatch (not from sfx2 — avoids cui↔sfx2 cycles).
    static void recordUse(const OUString& userInstallation,
                          const OUString& unoCommand);
};

namespace detail
{
inline void appendRecentJsonEscaped(OStringBuffer& out, const OString& s)
{
    const sal_Int32 n = s.getLength();
    const char* p = s.getStr();
    for (sal_Int32 i = 0; i < n; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        switch (c)
        {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (c < 0x20)
                {
                    char hex[8];
                    ::snprintf(hex, sizeof(hex), "\\u%04x",
                               static_cast<unsigned>(c));
                    out.append(hex);
                }
                else
                {
                    out.append(static_cast<char>(c));
                }
                break;
        }
    }
}

/// Skip whitespace starting at `i`; advance i past it.
inline void skipWs(const char* s, sal_Int32 n, sal_Int32& i)
{
    while (i < n
           && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
        ++i;
}

/// Lift the value of the `"key"` string field that begins at-or-after
/// `from` and ends before the closing `}` at `entryEnd`. Returns empty
/// OString if absent or malformed. Honors the same `\"`/`\\`/`\n`/
/// `\t`/`\r`/`\/`/`\b`/`\f` escapes used elsewhere in the project.
inline OString readStringField(const OString& body,
                               const char* key,
                               sal_Int32 from,
                               sal_Int32 entryEnd)
{
    sal_Int32 keyPos = body.indexOf(key, from);
    if (keyPos < 0 || keyPos >= entryEnd)
        return {};
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();
    sal_Int32 i = keyPos + static_cast<sal_Int32>(::strlen(key));
    skipWs(s, n, i);
    if (i >= n || s[i] != ':') return {};
    ++i;
    skipWs(s, n, i);
    if (i >= n || s[i] != '"') return {};
    ++i;

    OStringBuffer val;
    while (i < n && i < entryEnd)
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
    return val.makeStringAndClear();
}

/// Lift the integer value of `"key"` between `from` and `entryEnd`.
/// Returns 0 on absence/malformation; callers that need to distinguish
/// "absent" from "zero" should not use this helper for that field.
inline sal_Int32 readIntField(const OString& body,
                              const char* key,
                              sal_Int32 from,
                              sal_Int32 entryEnd)
{
    sal_Int32 keyPos = body.indexOf(key, from);
    if (keyPos < 0 || keyPos >= entryEnd)
        return 0;
    const sal_Int32 n = body.getLength();
    const char* s = body.getStr();
    sal_Int32 i = keyPos + static_cast<sal_Int32>(::strlen(key));
    skipWs(s, n, i);
    if (i >= n || s[i] != ':') return 0;
    ++i;
    skipWs(s, n, i);

    bool neg = false;
    if (i < n && s[i] == '-') { neg = true; ++i; }
    sal_Int32 val = 0;
    bool any = false;
    while (i < n && i < entryEnd && s[i] >= '0' && s[i] <= '9')
    {
        val = val * 10 + (s[i] - '0');
        any = true;
        ++i;
    }
    if (!any) return 0;
    return neg ? -val : val;
}
} // namespace detail

inline std::vector<RecentEntry> RecentStore::parseRecentJson(
    const OString& body)
{
    std::vector<RecentEntry> out;

    // Cheap version sniff — refuse anything that doesn't claim version 1.
    sal_Int32 verPos = body.indexOf("\"version\"");
    if (verPos < 0)
        return out;
    sal_Int32 verVal = detail::readIntField(body, "\"version\"", 0,
                                            body.getLength());
    if (verVal != 1)
        return out;

    sal_Int32 arrPos = body.indexOf("\"entries\"", verPos);
    if (arrPos < 0)
        return out;
    sal_Int32 arrStart = body.indexOf('[', arrPos);
    if (arrStart < 0)
        return out;

    // Walk objects inside the entries array. We do not validate that
    // arrEnd matches; truncated input simply trims the last entry.
    sal_Int32 i = arrStart + 1;
    const sal_Int32 n = body.getLength();
    while (i < n)
    {
        // Find the next object opening `{` before the array's closing `]`.
        sal_Int32 nextObj = body.indexOf('{', i);
        sal_Int32 nextArrClose = body.indexOf(']', i);
        if (nextObj < 0
            || (nextArrClose >= 0 && nextArrClose < nextObj))
        {
            break;
        }
        sal_Int32 entryEnd = body.indexOf('}', nextObj);
        if (entryEnd < 0)
            break;

        RecentEntry e;
        OString uno = detail::readStringField(body, "\"unoCommand\"",
                                              nextObj, entryEnd);
        OString last = detail::readStringField(body, "\"lastUsed\"",
                                               nextObj, entryEnd);
        sal_Int32 used = detail::readIntField(body, "\"useCount\"",
                                              nextObj, entryEnd);
        if (!uno.isEmpty())
        {
            e.unoCommand = OStringToOUString(uno, RTL_TEXTENCODING_UTF8);
            e.lastUsed = OStringToOUString(last, RTL_TEXTENCODING_UTF8);
            e.useCount = used;
            out.push_back(std::move(e));
        }
        i = entryEnd + 1;
    }
    return out;
}

inline OString RecentStore::serializeRecentJson(
    const std::vector<RecentEntry>& entries)
{
    OStringBuffer out;
    out.append("{\n  \"version\": 1,\n  \"entries\": [");
    for (std::size_t k = 0; k < entries.size(); ++k)
    {
        if (k > 0) out.append(",");
        out.append("\n    {\"unoCommand\": \"");
        detail::appendRecentJsonEscaped(out,
            OUStringToOString(entries[k].unoCommand,
                              RTL_TEXTENCODING_UTF8));
        out.append("\", \"lastUsed\": \"");
        detail::appendRecentJsonEscaped(out,
            OUStringToOString(entries[k].lastUsed,
                              RTL_TEXTENCODING_UTF8));
        out.append("\", \"useCount\": ");
        out.append(entries[k].useCount);
        out.append("}");
    }
    if (!entries.empty())
        out.append("\n  ");
    out.append("]\n}\n");
    return out.makeStringAndClear();
}

inline std::vector<RecentEntry> RecentStore::bump(
    std::vector<RecentEntry> entries,
    const OUString& unoCommand,
    const OUString& nowIso,
    std::size_t maxEntries)
{
    bool found = false;
    for (auto& e : entries)
    {
        if (e.unoCommand == unoCommand)
        {
            ++e.useCount;
            e.lastUsed = nowIso;
            found = true;
            break;
        }
    }
    if (!found)
    {
        RecentEntry fresh;
        fresh.unoCommand = unoCommand;
        fresh.lastUsed = nowIso;
        fresh.useCount = 1;
        entries.push_back(std::move(fresh));
    }
    std::sort(entries.begin(), entries.end(),
              [](const RecentEntry& a, const RecentEntry& b) {
                  if (a.useCount != b.useCount)
                      return a.useCount > b.useCount;
                  return a.lastUsed > b.lastUsed;
              });
    if (entries.size() > maxEntries)
        entries.resize(maxEntries);
    return entries;
}

inline void RecentStore::applyFrequencies(
    std::vector<CommandEntry>& corpus,
    const std::vector<RecentEntry>& recents)
{
    for (auto& c : corpus)
    {
        for (const auto& r : recents)
        {
            if (r.unoCommand == c.unoCommand)
            {
                // FuzzyMatcher's recency boost is `frequency / 10`.
                // Map useCount → frequency*10 so one use lifts the
                // score by exactly +1 (see W2 spec scoring table).
                c.frequency = r.useCount * 10;
                break;
            }
        }
    }
}

} // namespace cui::commandpalette

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
