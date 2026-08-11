/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: context usage chip).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Approximate context budget for UI chips — not a billing meter.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_CONTEXTUSAGE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_CONTEXTUSAGE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::control
{

struct ContextUsageSample
{
    sal_Int32 userChars = 0;
    sal_Int32 selectionChars = 0;
    sal_Int32 documentContextChars = 0;
    sal_Int32 historyChars = 0;
    sal_Int32 vaultChars = 0;
    sal_Int32 systemChars = 0;
    /// Soft budget in approximate tokens (default 32k-class sidebar budget).
    sal_Int32 budgetTokens = 24000;
};

struct ContextUsageEstimate
{
    sal_Int32 totalChars = 0;
    /// Rough tokens ≈ chars * 0.6 for CJK-heavy text (honest approximation).
    sal_Int32 approxTokens = 0;
    sal_Int32 budgetTokens = 24000;
    /// 0–100 capped.
    sal_Int32 percentUsed = 0;
    bool nearLimit = false; ///< >= 80%
    bool overLimit = false; ///< >= 100%
    OUString chipZh; ///< e.g. 上下文约 3.2k/24k
    OUString detailZh;
};

class SAL_DLLPUBLIC_EXPORT ContextUsage
{
public:
    static ContextUsageEstimate estimate(const ContextUsageSample& sample);

    /// chars → approx tokens (min 1 if chars>0).
    static sal_Int32 charsToApproxTokens(sal_Int32 chars);

    /// Format k-suffix number for chip.
    static OUString formatK(sal_Int32 n);
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
