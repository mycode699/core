/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Local-first document RAG: capture current Writer/Calc/Impress text,
 * keyword-rank chunks, inject into Provider prompts. No cloud egress.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAILOCALRAG_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAILOCALRAG_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

struct LocalRagChunk
{
    OUString position; ///< e.g. para:3 / cell:A1 / slide:2
    OUString text;
    sal_Int32 score = 0;
};

/// Local document Q&A without vector DB (keyword + structure ranking).
class SAL_DLLPUBLIC_EXPORT DocumentAILocalRag
{
public:
    /// True when user intent is "ask this document".
    static bool wantsDocumentRag(const OUString& rUserInput);

    /// Capture full-ish document text (clipped) for offline context.
    static OUString captureDocumentText(sal_Int32 nMaxChars = 12000);

    /// Split document into chunks for ranking.
    static std::vector<LocalRagChunk> chunkDocument(sal_Int32 nMaxChunks = 48,
                                                    sal_Int32 nMaxChunkChars = 600);

    /// Top-K chunks relevant to rQuery (local keyword score).
    static std::vector<LocalRagChunk> retrieve(const OUString& rQuery, sal_Int32 nTopK = 6,
                                               sal_Int32 nMaxChunkChars = 600);

    /// Ready-to-inject prompt block; empty if no document.
    static OUString buildContextBlock(const OUString& rQuery, sal_Int32 nTopK = 6,
                                      sal_Int32 nMaxChars = 4500);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
