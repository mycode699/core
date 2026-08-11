/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: first-run onboarding gate).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Skippable first-open welcome for AI sidebar. Never blocks document editing.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_AIFIRSTRUNGATE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_AIFIRSTRUNGATE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::control
{

enum class FirstRunState : sal_uInt8
{
    Unknown = 0,
    Pending, ///< should show welcome once
    Skipped,
    Completed,
};

struct FirstRunSnapshot
{
    FirstRunState state = FirstRunState::Unknown;
    bool shouldShowWelcome = false;
    OUString welcomeMarkdownZh;
    OUString statusLineZh;
};

/// Persist first-run AI sidebar prefs under kqoffice config dir.
class SAL_DLLPUBLIC_EXPORT AiFirstRunGate
{
public:
    static FirstRunState state();
    static FirstRunSnapshot snapshot();

    static bool shouldShowWelcome();
    static bool markSkipped();
    static bool markCompleted();
    static bool resetForTests();

    static OUString welcomeMarkdownZh();
    static OUString stateLabelZh(FirstRunState s);

    /// Store path for diagnostics (no secrets).
    static OUString storePathForDisplay();

    /// Override root for unit tests (empty = default config dir).
    static void setRootDirForTests(const OUString& rootDir);
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
