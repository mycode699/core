/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: evolution guard).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Dials the system may propose vs bars it may never touch (AutoHarness BUILD 10).
 * Candidates are never auto-promoted.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_POLICYEVOLUTIONGUARD_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_POLICYEVOLUTIONGUARD_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

struct PolicyCandidateCheck
{
    bool allowed = false;
    OUString rejectedField;
    OUString reasonZh;
    OUString reasonCode;
};

class SAL_DLLPUBLIC_EXPORT PolicyEvolutionGuard
{
public:
    /// Fields a candidate may change (exact match, case-insensitive).
    static std::vector<OUString> allowedDialFields();

    /// Substrings that may never appear in candidate keys (deny list).
    static std::vector<OUString> forbiddenMarkers();

    /// Exact allow wins first; then marker deny (nested keys as "a.b").
    static PolicyCandidateCheck validateFieldName(const OUString& fieldName);

    /// Validate a flat list of field names from a candidate patch.
    static PolicyCandidateCheck validateCandidateFields(const std::vector<OUString>& fields);

    /// Ceilings may fall, never rise (e.g. max concurrent streams).
    static bool ceilingMayOnlyFall(sal_Int32 oldValue, sal_Int32 newValue);

    static OUString policyHintZh();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
