/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace content registry).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>

#include <vector>

namespace sfx2::sidebar
{

struct AIChatContentRegistryEntry
{
    OUString ObjectId;
    OUString Type;
    OUString SourceSurface;
    OUString State;
    OUString EvidenceId;
    OUString HashReference;
    OUString OpenTarget;
    OUString PreviewMode;
};

class AIChatContentRegistry final
{
public:
    AIChatContentRegistry();

    const OUString& GetRegistryUrl() const { return m_sRegistryUrl; }

    bool RegisterObject(const AIChatContentRegistryEntry& rEntry) const;
    std::vector<AIChatContentRegistryEntry> LoadEntries() const;
    bool ArchiveObject(const OUString& rObjectId) const;

private:
    OUString m_sStorageRootUrl;
    OUString m_sRegistryUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
