/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: review queue runtime).
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

struct AIChatReviewQueueEntry
{
    OUString ReviewId;
    OUString ItemType;
    OUString State;
    OUString SourceSurface;
    OUString EvidenceId;
    OUString HashReference;
    OUString OpenTarget;
    OUString PreviewMode;
};

struct AIChatReviewQueueFilter
{
    OUString State;
    OUString ItemType;
    OUString Surface;
};

class AIChatReviewQueueStore final
{
public:
    AIChatReviewQueueStore();

    const OUString& GetQueueUrl() const { return m_sQueueUrl; }

    bool EnqueueFromRegistry(const AIChatContentRegistryEntry& rEntry) const;
    bool TransitionState(const OUString& rReviewId, const OUString& rNewState) const;
    bool TransitionState(const AIChatReviewQueueEntry& rEntry, const OUString& rNewState,
                         const OUString& rTransitionEvent) const;
    std::vector<AIChatReviewQueueEntry> LoadEntries() const;
    std::vector<AIChatReviewQueueEntry> FilterEntries(const AIChatReviewQueueFilter& rFilter) const;

    static bool IsReviewQueueEntry(const AIChatContentRegistryEntry& rEntry);
    static bool IsValidState(const OUString& rState);
    static bool IsBulkActionAllowed(const OUString& rAction);
    static OUString ResolveItemType(const AIChatContentRegistryEntry& rEntry);

private:
    OUString m_sStorageRootUrl;
    OUString m_sQueueUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
