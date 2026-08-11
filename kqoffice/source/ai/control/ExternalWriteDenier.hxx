/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: external-write denier).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Pure function over program + args. Local recoverable edits pass;
 * force-push / publish / deploy are refused (AutoHarness BUILD 06).
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_EXTERNALWRITEDENIER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_EXTERNALWRITEDENIER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

struct DenyDecision
{
    bool allowed = true;
    OUString reasonCode; ///< empty when allowed
    OUString reasonZh;
    OUString matchedRule;
};

class SAL_DLLPUBLIC_EXPORT ExternalWriteDenier
{
public:
    /// program: basename or path; args: argv without program.
    static DenyDecision evaluate(const OUString& program, const std::vector<OUString>& args);

    /// Convenience: single shell-ish line "git push origin main".
    static DenyDecision evaluateCommandLine(const OUString& commandLine);

    /// MCP / connector action names (publish, push_branch, …).
    static DenyDecision evaluateAction(const OUString& actionName);

    static OUString policyHintZh();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
