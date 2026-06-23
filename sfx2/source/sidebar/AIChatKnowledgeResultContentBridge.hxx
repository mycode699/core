/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W3/M4: Knowledge result content bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatContentRegistry.hxx"
#include "AIChatKnowledgeRetrievalRuntime.hxx"

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatKnowledgeResultContentBridgeResult
{
    bool Success = false;
    AIChatContentRegistryEntry RegistryEntry;
    OUString SourceId;
    OUString CitationId;
    OUString EvidenceId;
    OUString HashReference;
    OUString Message;
};

class AIChatKnowledgeResultContentBridge final
{
public:
    AIChatKnowledgeResultContentBridgeResult
    RegisterResult(const AIChatKnowledgeRetrievalResult& rResult) const;

    static OUString MakeKnowledgeResultEvidenceId(const AIChatKnowledgeRetrievalResult& rResult);
    static OUString MakeKnowledgeResultHashReference(const AIChatKnowledgeRetrievalResult& rResult);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
