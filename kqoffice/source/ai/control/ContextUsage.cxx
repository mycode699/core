/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: context usage chip).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ContextUsage.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::control
{

sal_Int32 ContextUsage::charsToApproxTokens(sal_Int32 chars)
{
    if (chars <= 0)
        return 0;
    // Mixed CJK/EN heuristic used for UI only.
    const sal_Int32 t = (chars * 3 + 4) / 5; // *0.6
    return t < 1 ? 1 : t;
}

OUString ContextUsage::formatK(sal_Int32 n)
{
    if (n < 1000)
        return OUString::number(n);
    const sal_Int32 whole = n / 1000;
    const sal_Int32 frac = (n % 1000) / 100;
    if (frac == 0)
        return OUString::number(whole) + u"k"_ustr;
    return OUString::number(whole) + u"."_ustr + OUString::number(frac) + u"k"_ustr;
}

ContextUsageEstimate ContextUsage::estimate(const ContextUsageSample& sample)
{
    ContextUsageEstimate out;
    out.budgetTokens = sample.budgetTokens > 0 ? sample.budgetTokens : 24000;
    out.totalChars = sample.userChars + sample.selectionChars + sample.documentContextChars
                     + sample.historyChars + sample.vaultChars + sample.systemChars;
    if (out.totalChars < 0)
        out.totalChars = 0;
    out.approxTokens = charsToApproxTokens(out.totalChars);
    if (out.budgetTokens > 0)
    {
        const sal_Int64 pct
            = (static_cast<sal_Int64>(out.approxTokens) * 100) / out.budgetTokens;
        out.percentUsed = pct > 100 ? 100 : static_cast<sal_Int32>(pct);
    }
    out.nearLimit = out.percentUsed >= 80;
    out.overLimit = out.percentUsed >= 100;

    out.chipZh = u"上下文约 "_ustr + formatK(out.approxTokens) + u"/"_ustr
                 + formatK(out.budgetTokens);
    if (out.overLimit)
        out.chipZh += u" · 超限"_ustr;
    else if (out.nearLimit)
        out.chipZh += u" · 将满"_ustr;

    OUStringBuffer d;
    d.append(u"用户 "_ustr + OUString::number(sample.userChars));
    d.append(u" · 选区 "_ustr + OUString::number(sample.selectionChars));
    d.append(u" · 文档 "_ustr + OUString::number(sample.documentContextChars));
    d.append(u" · 历史 "_ustr + OUString::number(sample.historyChars));
    d.append(u" · 资料 "_ustr + OUString::number(sample.vaultChars));
    d.append(u" · 系统 "_ustr + OUString::number(sample.systemChars));
    d.append(u" · 约 "_ustr + OUString::number(out.approxTokens) + u" token（估算）"_ustr);
    out.detailZh = d.makeStringAndClear();
    return out;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
