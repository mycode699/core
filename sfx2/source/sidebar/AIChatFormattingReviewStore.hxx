/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: formatting review runtime).
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

struct AIChatFormattingReviewEntry
{
    OUString ReviewId;
    OUString SourceObjectId;
    OUString FormattingScope;
    OUString State;
    OUString ReviewMode;
    OUString EvidenceId;
    OUString HashReference;
    OUString OpenTarget;
    OUString PreviewMode;
    bool RequiresHumanApproval = true;
    bool MainDocumentMutationAllowed = false;
};

struct AIChatFormattingReviewCreateResult
{
    bool Success = false;
    AIChatFormattingReviewEntry Review;
    AIChatContentRegistryEntry RegistryEntry;
    OUString Message;
};

class AIChatFormattingReviewStore final
{
public:
    AIChatFormattingReviewStore();

    const OUString& GetReviewStoreUrl() const { return m_sReviewStoreUrl; }

    AIChatFormattingReviewCreateResult
    CreateReviewFromSource(const AIChatContentRegistryEntry& rSource) const;
    std::vector<AIChatFormattingReviewEntry> LoadEntries() const;

    static bool IsSupportedFormattingScope(const OUString& rScope);
    static OUString MakeReviewId(const OUString& rSourceObjectId);

private:
    OUString m_sStorageRootUrl;
    OUString m_sReviewStoreUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
