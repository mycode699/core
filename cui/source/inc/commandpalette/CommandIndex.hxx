/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2 Day-1a: Command Index).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V2 W2 Day-1a — XCU scanner that lifts (.uno:Foo, label) tuples out of
 * `officecfg/.../UI/{Star}Commands.xcu`. Pure logic, no UNO calls.
 *
 * NOTE: parseCommandsXcu is defined inline so the cppunit test can link
 * against it without duplicating .cxx objects already in libcui (the
 * fdo#47246 double-linkage trap). loadFromDirectory wraps the parser
 * with osl/file disk I/O and lives in CommandIndex.cxx.
 *
 * Spec: docs/product/v2/w2-cmd-palette-spec.md §"命令索引数据源".
 */

#ifndef INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_COMMANDINDEX_HXX
#define INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_COMMANDINDEX_HXX

#include <commandpalette/FuzzyMatcher.hxx>

#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <utility>
#include <vector>

namespace cui::commandpalette
{
/// Builds a `std::vector<CommandEntry>` corpus from `*Commands.xcu`.
///
/// Day-1a contract:
///   - Pure parser only — no recency/persistence, no localization.
///   - Only en-US `Label` is lifted (zh-CN comes from the
///     `.po`-derived translation overlay at runtime; W2 Day-1c will
///     inject pinyin via i18npool, leaving these fields empty for now).
///   - `loadFromDirectory()` reads every `*Commands.xcu` under the
///     supplied directory and concatenates parser output. Designed to
///     be called once at palette open time.
class CommandIndex
{
public:
    /// Parse a single XCU buffer. Linear scan looking for
    ///   `<node oor:name=".uno:..." oor:op="...">`
    /// followed (in any order, before the closing `</node>`) by
    ///   `<value xml:lang="en-US">Foo</value>`
    /// inside the first `Label` prop. Robust to whitespace and the
    /// optional ContextLabel that some commands also carry.
    /// Entries with no en-US Label are still emitted (label stays
    /// empty) so the corpus matches the universe of registered uno
    /// commands. Defensive: any malformed/truncated entry is skipped
    /// silently — caller treats the result as best-effort.
    static inline std::vector<CommandEntry> parseCommandsXcu(
        const OString& body);

    /// Walk a directory for `*Commands.xcu`, parse each, concat.
    /// Empty vector on any I/O failure (no such directory etc.).
    /// Implemented in CommandIndex.cxx to keep this header free of
    /// the osl/file include surface.
    static std::vector<CommandEntry> loadFromDirectory(const OUString& dir);
};

namespace detail
{
inline OString readOorName(const OString& body,
                           sal_Int32 nodeStart,
                           sal_Int32 nodeEnd)
{
    sal_Int32 attrPos = body.indexOf("oor:name=\"", nodeStart);
    if (attrPos < 0 || attrPos >= nodeEnd)
        return {};
    sal_Int32 valStart = attrPos + 10;
    sal_Int32 valEnd = body.indexOf('"', valStart);
    if (valEnd < 0 || valEnd >= nodeEnd)
        return {};
    return body.copy(valStart, valEnd - valStart);
}

inline OString readLabelEnUs(const OString& body,
                             sal_Int32 nodeStart,
                             sal_Int32 nodeEnd)
{
    // Locate `<prop oor:name="Label"`. The prop block can carry
    // multiple `<value xml:lang=...>` children; pick the first en-US.
    sal_Int32 propPos = body.indexOf("oor:name=\"Label\"", nodeStart);
    if (propPos < 0 || propPos >= nodeEnd)
        return {};
    sal_Int32 propClose = body.indexOf("</prop>", propPos);
    if (propClose < 0 || propClose > nodeEnd)
        return {};
    sal_Int32 langPos = body.indexOf("xml:lang=\"en-US\"", propPos);
    if (langPos < 0 || langPos >= propClose)
        return {};
    sal_Int32 gt = body.indexOf('>', langPos);
    if (gt < 0 || gt >= propClose)
        return {};
    sal_Int32 valStart = gt + 1;
    sal_Int32 valEnd = body.indexOf("</value>", valStart);
    if (valEnd < 0 || valEnd > propClose)
        return {};
    OString raw = body.copy(valStart, valEnd - valStart);

    // Drop the LO accelerator hint (`~`) that marks the underlined
    // letter. Keep XML entities readable for the matcher; full entity
    // expansion is overkill for Day-1a.
    OStringBuffer out(raw.getLength());
    const sal_Int32 n = raw.getLength();
    for (sal_Int32 i = 0; i < n; ++i)
    {
        const char c = raw[i];
        if (c == '~')
            continue;
        out.append(c);
    }
    return out.makeStringAndClear();
}
} // namespace detail

inline std::vector<CommandEntry> CommandIndex::parseCommandsXcu(
    const OString& body)
{
    std::vector<CommandEntry> out;
    const sal_Int32 n = body.getLength();
    sal_Int32 i = 0;
    while (i < n)
    {
        // Anchor on the literal `.uno:` so we ignore the outer
        // UserInterface/Commands wrapper nodes without a full XML parser.
        sal_Int32 nodeStart = body.indexOf("<node oor:name=\".uno:", i);
        if (nodeStart < 0)
            break;

        sal_Int32 cursor = body.indexOf('>', nodeStart);
        if (cursor < 0)
            break;
        ++cursor;
        int depth = 1;
        sal_Int32 nodeEnd = -1;
        while (cursor < n && depth > 0)
        {
            sal_Int32 nextOpen = body.indexOf("<node ", cursor);
            sal_Int32 nextClose = body.indexOf("</node>", cursor);
            if (nextClose < 0)
                break;
            if (nextOpen >= 0 && nextOpen < nextClose)
            {
                ++depth;
                cursor = nextOpen + 6;
            }
            else
            {
                --depth;
                if (depth == 0)
                {
                    nodeEnd = nextClose;
                    cursor = nextClose + 7;
                    break;
                }
                cursor = nextClose + 7;
            }
        }
        if (nodeEnd < 0)
            break; // truncated input — bail out cleanly

        OString unoName = detail::readOorName(body, nodeStart, nodeEnd);
        if (unoName.startsWith(".uno:"))
        {
            CommandEntry e;
            e.unoCommand = OStringToOUString(unoName,
                                             RTL_TEXTENCODING_UTF8);
            OString labelEn = detail::readLabelEnUs(body, nodeStart, nodeEnd);
            if (!labelEn.isEmpty())
            {
                e.labelEn = OStringToOUString(labelEn,
                                              RTL_TEXTENCODING_UTF8);
            }
            // labelZh / pinyin* stay empty in Day-1a; W2 Day-1c fills them.
            out.push_back(std::move(e));
        }

        i = cursor;
    }
    return out;
}

} // namespace cui::commandpalette

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
