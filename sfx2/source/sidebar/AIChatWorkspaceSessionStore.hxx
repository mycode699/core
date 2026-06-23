/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: workspace session state).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatWorkspaceActivityEntry
{
    OUString Event;
    OUString Surface;
    OUString Actor;
    OUString Timestamp;
    OUString ArtifactId;
    OUString ReviewId;
    OUString EvidenceId;
    OUString HashReference;
    OUString OpenTarget;
};

struct AIChatSessionSnapshot
{
    OUString DocumentBinding;
    OUString Timestamp;
    OUString ActiveTaskId;
    OUString OpenArtifactId;
    OUString OpenReviewId;
    OUString ActiveEvidenceId;
    OUString PreviewMode;
    OUString ReviewState;
    OUString ActivityCursor;
    OUString FailureState;
    OUString HashReference;
};

class AIChatWorkspaceSessionStore final
{
public:
    explicit AIChatWorkspaceSessionStore(const OUString& rDocumentBinding);

    bool RecordActivity(const AIChatWorkspaceActivityEntry& rEntry) const;
    bool SaveSnapshot(const AIChatSessionSnapshot& rSnapshot) const;
    AIChatSessionSnapshot LoadSnapshot() const;

    const OUString& GetTimelineUrl() const { return m_sTimelineUrl; }
    const OUString& GetSnapshotUrl() const { return m_sSnapshotUrl; }
    const OUString& GetDocumentBinding() const { return m_sDocumentBinding; }

    static OUString MakeTimestamp();

private:
    OUString m_sDocumentBinding;
    OUString m_sStorageRootUrl;
    OUString m_sTimelineUrl;
    OUString m_sSnapshotUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
