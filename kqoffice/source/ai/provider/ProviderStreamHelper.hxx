/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Non-UNO streaming chat helper: same routing as Provider::call (Ollama /
 * OpenAI-compatible), but yields incremental text chunks for the AI sidebar.
 * Keeps XProvider sync (gate tests forbid streaming UNO schema).
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_PROVIDERSTREAMHELPER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_PROVIDERSTREAMHELPER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <functional>

namespace kqoffice::ai
{

struct StreamChatResult
{
    OUString status; ///< ok | provider-error | cancelled | policy-denied
    OUString content; ///< full assembled text
    OUString providerLabel;
    sal_Int32 durationMs = 0;
};

using StreamChunkFn = std::function<bool(const OUString& rDelta)>;
using StreamCancelFn = std::function<bool()>;

/// Resolve model routing and stream a chat completion.
/// rOnChunk may be empty (still assembles content). Return false from
/// rOnChunk / rShouldCancel to abort early (status=cancelled if any text,
/// provider-error if empty).
StreamChatResult streamChatCompletion(const OUString& rCapability,
                                      const OUString& rPrompt,
                                      const StreamChunkFn& rOnChunk,
                                      const StreamCancelFn& rShouldCancel = StreamCancelFn());

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
