/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: cage canaries).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Prove confinement at "startup" of an AI turn — fail closed (AutoHarness BUILD 05).
 * Not a full OS sandbox: product-level canaries for path policy, denier, and writable roots.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_SANDBOXCAGE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_SANDBOXCAGE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

enum class CanaryResult : sal_uInt8
{
    Pass = 0,
    Fail, ///< breach or policy hole
    Skip, ///< not applicable on this platform / env
};

struct CanaryCheck
{
    OUString id; ///< e.g. ssh-not-writable, home-not-auto-auth
    OUString titleZh;
    CanaryResult result = CanaryResult::Pass;
    OUString detailZh;
};

struct CageReport
{
    bool mayStartAiRun = true; ///< false if any Fail (fail-closed)
    std::vector<CanaryCheck> checks;
    OUString summaryZh;
    OUString failedCanaryId; ///< first failure id
};

class SAL_DLLPUBLIC_EXPORT SandboxCage
{
public:
    /// Run all canaries. Pure-ish: may touch temp files under kqoffice paths only.
    static CageReport runCanaries();

    /// Single named canary (tests).
    static CanaryCheck runOne(const OUString& canaryId);

    static OUString resultId(CanaryResult r);
    static OUString policyHintZh();

    /// Env KQOFFICE_AI_CAGE_BYPASS=1 allows run despite Fail (tests only).
    static bool bypassEnabled();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
