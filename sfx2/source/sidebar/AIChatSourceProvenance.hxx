/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace source provenance).
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

struct AIChatSourceProvenanceEntry
{
    OUString SourceId;
    OUString SourceType;
    OUString CitationId;
    OUString EvidenceId;
    OUString HashReference;
    OUString SourceSurface;
    OUString OpenTarget;
    OUString SpanReference;
    OUString ReviewId;
};

class AIChatSourceProvenance final
{
public:
    AIChatSourceProvenance();

    const OUString& GetProvenanceUrl() const { return m_sProvenanceUrl; }

    bool RegisterSource(const AIChatSourceProvenanceEntry& rEntry) const;
    std::vector<AIChatSourceProvenanceEntry> LoadEntries() const;

    static OUString MakeSourceId(const OUString& rObjectId);
    static OUString MakeCitationId(const OUString& rObjectId);
    static OUString MakeLocalEvidenceId(const OUString& rObjectId);

private:
    OUString m_sStorageRootUrl;
    OUString m_sProvenanceUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
