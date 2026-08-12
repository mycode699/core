/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1 Day-1: Ollama adapter).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_OLLAMAADAPTER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_OLLAMAADAPTER_HXX

#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <functional>
#include <vector>

namespace kqoffice::ai
{
/// Thin HTTP probe against a locally-running Ollama daemon.
///
/// Pure logic — no libcurl, no TLS. One short-timeout GET against
/// `http://127.0.0.1:11434/api/tags` and we either learn the installed
/// model list or report the daemon as unreachable. Offline-first mode
/// has to survive a workstation that has never heard of Ollama.
///
/// Receive timeout is clamped to 100ms so cppunit invocations from a
/// developer laptop do not hang when nothing is listening.
class SAL_DLLPUBLIC_EXPORT OllamaAdapter
{
public:
    // Host + port are compile-time constants for Day-1. Day-2 will let
    // ServiceModePolicy inject an alternate endpoint for "private" mode.
    static constexpr const char* kHost = "127.0.0.1";
    static constexpr int kPort = 11434;
    static constexpr const char* kTagsPath = "/api/tags";
    static constexpr const char* kGeneratePath = "/api/generate";

    /// One TCP connect attempt with a 100ms timeout.
    /// Returns "reachable" on successful connect, "unreachable" otherwise.
    /// Never throws — a down daemon is the normal offline case.
    OUString probe();

    /// GET /api/tags and extract `models[].name`. Empty vector on any
    /// failure (unreachable, non-2xx, malformed JSON) — callers treat
    /// empty as "no models available" which is the same semantics as
    /// a fresh Ollama install.
    std::vector<OUString> listModels();

    /// Blocking POST /api/generate with `{"stream":false,"format":"json"}`.
    /// 30s send/recv
    /// timeout — long enough for a 7B local model to produce a short
    /// answer, short enough that a hung daemon never wedges the UI.
    /// Returns the generated text on success, empty OUString on any
    /// failure (unreachable, send/recv error, non-2xx, malformed JSON,
    /// empty `response` field). Caller maps empty → provider-error.
    /// Never throws.
    OUString generate(const OUString& model, const OUString& prompt);

    /// Streaming /api/generate (stream:true NDJSON). rOnChunk gets response deltas.
    using StreamChunkFn = std::function<bool(const OUString& rDelta)>;
    using StreamCancelFn = std::function<bool()>;
    OUString generateStream(const OUString& model, const OUString& prompt,
                            const StreamChunkFn& rOnChunk,
                            const StreamCancelFn& rShouldCancel = StreamCancelFn());

    /// Exposed for cppunit: build the exact non-stream /api/generate request body.
    /// The app-level Writer provider path expects runtime JSON, so the request
    /// pins Ollama JSON mode and temperature 0 for deterministic structure.
    static OString buildGenerateRequestJson(const OUString& model, const OUString& prompt,
                                            bool bStream = false);

    /// Exposed for cppunit: parse just the `models[].name` fields from
    /// a raw Ollama `/api/tags` JSON body. No generic JSON parser —
    /// linear scan for `"name"` keys, which matches the canonical
    /// Ollama response shape where `name` is the first field of each
    /// model object.
    static std::vector<OUString> parseModelsJson(const OString& body);

    /// Exposed for cppunit: parse the `response` string field from a
    /// raw Ollama `/api/generate` (non-stream) JSON body. Same linear
    /// scan style as parseModelsJson with the same `\"`/`\\`/`\n`/
    /// `\t`/`\r`/`\/` escape handling. Returns empty OUString if the
    /// `response` key is absent or the string is malformed.
    static OUString parseGenerateJson(const OString& body);
};

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
