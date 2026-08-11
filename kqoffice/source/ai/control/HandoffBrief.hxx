/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: ledger-based handoff).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Free + deterministic brief from EventLedger — not a model self-summary.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_HANDOFFBRIEF_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_HANDOFFBRIEF_HXX

#include "EventLedger.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

struct HandoffBrief
{
    OUString markdownZh;
    sal_Int32 eventCount = 0;
    sal_Int64 lastSequence = 0;
    bool fromLedger = true;
};

class SAL_DLLPUBLIC_EXPORT HandoffBriefBuilder
{
public:
    /// Build from already-fetched events (pure).
    static HandoffBrief fromEvents(const std::vector<LedgerEvent>& events,
                                   const OUString& titleZh = OUString());

    /// Load ledger tail and build (IO).
    static HandoffBrief fromLedgerTail(sal_Int32 maxEvents = 30);
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
