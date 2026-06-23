/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge extraction guards).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatKnowledgeIndexStore.hxx"

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatKnowledgeExtractionRequest
{
    OUString WorkspaceIdentity;
    OUString SourceKind;
    OUString SourceUriHash;
    OUString SourceId;
    OUString SnapshotId;
    OUString ContentHash;
    OUString TextHash;
    OUString DocumentFamily;
    OUString InputFormat;
    OUString Granularity;
    sal_Int32 Ordinal = 0;
    sal_Int32 TokenCount = 0;
    OUString Language;
    OUString EvidenceId;
    OUString HashReference;
    bool UsesLibreOfficeImportFilter = false;
    bool UsesDocumentModel = false;
    bool PreservesSlideElementRefs = false;
    bool StandalonePptParserAllowed = false;
};

struct AIChatKnowledgeExtractionResult
{
    bool Success = false;
    AIChatKnowledgeIndexChunk Chunk;
    OUString TextExtractionPath;
    OUString ExtractionPolicy;
    OUString Message;
};

class AIChatKnowledgeExtractionRuntime final
{
public:
    AIChatKnowledgeExtractionResult
    CreateChunkMetadata(const AIChatKnowledgeExtractionRequest& rRequest) const;

    static bool IsSupportedDocumentFamily(const OUString& rDocumentFamily);
    static bool IsLibreOfficeDocumentModelExtraction(
        const AIChatKnowledgeExtractionRequest& rRequest);
    static bool IsConnectorExtraction(const AIChatKnowledgeExtractionRequest& rRequest);
    static bool IsPptxExtractionPolicyAllowed(const AIChatKnowledgeExtractionRequest& rRequest);
    static OUString ResolveTextExtractionPath(const AIChatKnowledgeExtractionRequest& rRequest);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
