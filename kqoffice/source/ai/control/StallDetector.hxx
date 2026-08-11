/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: stall / loop detector).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Pure: no clock/IO. Progress signals reset counters (AutoHarness BUILD 09).
 * Negative cases (TDD, research, long build) must not fire.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_STALLDETECTOR_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_STALLDETECTOR_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

enum class DetectorSignalKind : sal_uInt8
{
    Progress = 0, ///< token / tool progress / file write
    ToolFail, ///< same failing tool/command
    ToolOk,
    IdleTick, ///< no output for one interval unit
    ReadOnly, ///< read without edit
    Note, ///< other
};

struct DetectorEvent
{
    DetectorSignalKind kind = DetectorSignalKind::Note;
    OUString action; ///< tool name / command fingerprint seed
    OUString detail; ///< optional error text (will be normalized)
    /// Logical time units since run start (caller supplies; pure detector).
    sal_Int32 timeUnit = 0;
};

enum class StallVerdict : sal_uInt8
{
    None = 0,
    SoftStall, ///< suggest cancel UI
    LoopSuspect, ///< repeated identical failures
    ReadNoWrite, ///< many reads, no progress edits
};

struct StallDecision
{
    StallVerdict verdict = StallVerdict::None;
    OUString reasonCode;
    OUString reasonZh;
    bool shouldNudge = false;
    bool shouldStop = false;
};

class SAL_DLLPUBLIC_EXPORT StallDetector
{
public:
    /// Evaluate pure history (newest last). history size capped internally to 24.
    static StallDecision evaluate(const std::vector<DetectorEvent>& history);

    /// Normalize action for fingerprint: digits → #, keep last path component.
    static OUString normalizeFingerprint(const OUString& action, const OUString& detail);

    static OUString verdictId(StallVerdict v);
    static OUString policyHintZh();

    /// Defaults (logical units — map to seconds in UI).
    static sal_Int32 idleUnitsForSoftStall(); ///< default 4
    static sal_Int32 identicalFailsForLoop(); ///< default 2
    static sal_Int32 readOnlyBurstForSuspect(); ///< default 12
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
