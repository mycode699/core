/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W2 Day-1c — ASCII-first-letter pinyin hints for Cmd+K fuzzy match.
 *
 * No i18npool: derives pinyinFirst / pinyinFull from labelEn only.
 * Chinese-only labels (labelZh without labelEn) are left empty so real
 * pinyin from .xcu (W2 Day-2) can fill them later.
 */

#ifndef INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_PINYINHINT_HXX
#define INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_PINYINHINT_HXX

#include <commandpalette/FuzzyMatcher.hxx>

#include <rtl/ustrbuf.hxx>

namespace cui::commandpalette
{
namespace PinyinHint
{
namespace detail
{
inline bool isAsciiLetter(sal_Unicode c)
{
    return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z');
}

inline sal_Unicode toLowerAscii(sal_Unicode c)
{
    if (c >= u'A' && c <= u'Z')
        return c + (u'a' - u'A');
    return c;
}
} // namespace detail

/// Populate `entry.pinyinFirst` (first letter per word) and
/// `entry.pinyinFull` (concatenated lowercase letters) from labelEn.
inline void apply(CommandEntry& entry)
{
    if (entry.labelEn.isEmpty())
        return;

    OUString aFirst;
    OUStringBuffer aFull;
    bool bInWord = false;

    const sal_Int32 n = entry.labelEn.getLength();
    for (sal_Int32 i = 0; i < n; ++i)
    {
        sal_Unicode c = entry.labelEn[i];
        if (c == u'~')
            continue;
        if (detail::isAsciiLetter(c))
        {
            const sal_Unicode lower = detail::toLowerAscii(c);
            aFull.append(lower);
            if (!bInWord)
            {
                aFirst += OUStringChar(lower);
                bInWord = true;
            }
        }
        else
        {
            bInWord = false;
        }
    }
    entry.pinyinFirst = aFirst;
    entry.pinyinFull = aFull.makeStringAndClear();
}

} // namespace PinyinHint
} // namespace cui::commandpalette

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */