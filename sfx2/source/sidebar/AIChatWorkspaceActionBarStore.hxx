/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: workspace action bar).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatContentRegistry.hxx"

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatWorkspaceActionBarDispatchResult
{
    bool Success = false;
    OUString Command;
    OUString TargetId;
    OUString TargetType;
    OUString EvidenceId;
    OUString HashReference;
    OUString OpenTarget;
    OUString PreviewMode;
    OUString ReviewState;
    OUString Reference;
    OUString Message;
};

class AIChatWorkspaceActionBarStore final
{
public:
    AIChatWorkspaceActionBarDispatchResult
    DispatchCommand(const OUString& rCommand, const AIChatContentRegistryEntry* pEntry,
                    bool bRequestBusy, bool bHasRetryPrompt) const;

    static std::vector<OUString> GetCommandRoster();
    static bool IsSupportedCommand(const OUString& rCommand);
    static bool IsSupportedTargetType(const OUString& rTargetType);
    static bool RequiresSelectedTarget(const OUString& rCommand);
    static bool RequiresEvidenceLink(const OUString& rCommand);
    static bool IsCommandEnabled(const OUString& rCommand,
                                 const AIChatContentRegistryEntry* pEntry,
                                 bool bRequestBusy, bool bHasRetryPrompt);
    static OUString MakeReference(const AIChatContentRegistryEntry& rEntry);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
