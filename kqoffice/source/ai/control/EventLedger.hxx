/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: append-only ledger).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Persist before broadcast (AutoHarness BUILD 02). Reconnect = replay since N
 * then live — one code path.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_EVENTLEDGER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_EVENTLEDGER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <functional>
#include <vector>

namespace kqoffice::ai::control
{

struct LedgerEvent
{
    sal_Int64 sequence = 0; ///< global monotonic (from committed write)
    sal_Int64 runSequence = 0; ///< per-runSequence when runId set
    OUString runId;
    OUString type; ///< route | apply | permission | stall | system | …
    OUString payload; ///< redacted-friendly short text (no secrets)
    sal_Int64 atMs = 0;
};

/// Append-only JSONL ledger under config/ledger/events.jsonl
class SAL_DLLPUBLIC_EXPORT EventLedger
{
public:
    EventLedger();
    explicit EventLedger(const OUString& rootDir);

    /// Persist then notify subscribers. Returns sequence (0 = failed).
    sal_Int64 append(const OUString& type, const OUString& payload,
                     const OUString& runId = OUString());

    /// Events with sequence > sinceSequence (inclusive lower bound exclusive).
    std::vector<LedgerEvent> replaySince(sal_Int64 sinceSequence,
                                         sal_Int32 maxEvents = 500) const;

    sal_Int64 lastSequence() const;

    /// Process-local subscribers (tests / future UI). Called only after persist.
    using Subscriber = std::function<void(const LedgerEvent&)>;
    static void subscribe(Subscriber fn);
    static void clearSubscribersForTests();

    OUString rootDir() const { return m_rootDir; }
    static OUString resolveRootDir();
    static OUString eventsPathFor(const OUString& rootDir);

private:
    bool ensureDir() const;
    bool appendLine(const OUString& line) const;
    sal_Int64 nextSequenceFromFile() const;

    OUString m_rootDir;
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
