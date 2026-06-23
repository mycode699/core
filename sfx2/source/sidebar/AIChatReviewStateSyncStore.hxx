/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: review state sync).
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

struct AIChatReviewStateSyncEntry
{
    OUString ReviewId;
    OUString State;
    OUString TransitionEvent;
    OUString SourceSurface;
    OUString EvidenceId;
    OUString HashReference;
    OUString OpenTarget;
    OUString PreviewMode;
    OUString VisibleState;
    bool Conflict = false;
};

struct AIChatReviewStateSyncResult
{
    bool Success = false;
    AIChatReviewStateSyncEntry Entry;
    OUString Message;
};

class AIChatReviewStateSyncStore final
{
public:
    AIChatReviewStateSyncStore();

    const OUString& GetStateUrl() const { return m_sStateUrl; }

    AIChatReviewStateSyncResult RecordTransition(const OUString& rReviewId,
                                                 const OUString& rTransitionEvent,
                                                 const OUString& rState,
                                                 const OUString& rSourceSurface,
                                                 const OUString& rEvidenceId,
                                                 const OUString& rHashReference,
                                                 const OUString& rOpenTarget,
                                                 const OUString& rPreviewMode) const;
    AIChatReviewStateSyncResult RecordFromRegistry(const AIChatContentRegistryEntry& rEntry,
                                                   const OUString& rTransitionEvent,
                                                   const OUString& rState,
                                                   const OUString& rSourceSurface) const;
    AIChatReviewStateSyncResult GetLatestState(const OUString& rReviewId) const;
    std::vector<AIChatReviewStateSyncEntry> LoadEntries() const;

    static bool IsValidState(const OUString& rState);
    static bool IsValidTransitionEvent(const OUString& rTransitionEvent);
    static bool IsSyncedSurface(const OUString& rSurface);
    static OUString NormalizeRegistryState(const OUString& rState);
    static OUString TransitionForState(const OUString& rState);
    static OUString BuildVisibleState(const AIChatReviewStateSyncEntry& rEntry);

private:
    OUString m_sStorageRootUrl;
    OUString m_sStateUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
