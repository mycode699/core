/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * OpenAI-compatible HTTP adapter for private/local gateways
 * (vLLM, LocalAI, One-API, LiteLLM, cloud OpenAI-compatible endpoints).
 * No TLS yet — HTTP only; TLS private/cloud remains a later gated step.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_OPENAICOMPATIBLEADAPTER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_OPENAICOMPATIBLEADAPTER_HXX

#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <functional>
#include <vector>

namespace kqoffice::ai
{
/// Thin HTTP(S) client for OpenAI Chat Completions API shape:
///   POST {base}/v1/chat/completions
///   GET  {base}/v1/models
///
/// HTTP: pure BSD sockets. HTTPS: system `curl` transport (macOS/Linux).
/// Never throws.
class SAL_DLLPUBLIC_EXPORT OpenAICompatibleAdapter
{
public:
    struct Endpoint
    {
        OUString host; // default 127.0.0.1
        int port = 8080;
        OUString pathPrefix; // e.g. empty or "/v1" stripped — internal uses /v1/...
        bool useTls = false; ///< https://
        bool valid = false;
    };

    /// Parse baseUrl like "http://127.0.0.1:8080", "https://ttqq.inping.com", or ".../v1".
    /// Empty hosts are rejected.
    static Endpoint parseBaseUrl(const OUString& rBaseUrl);

    /// True when routing.backend is openai / openai-compatible / openai-compat / openai_compatible.
    static bool isOpenAICompatibleBackend(const OUString& rBackend);

    /// API key from env KQOFFICE_AI_API_KEY, else ~/.config/kqoffice/api-key (optional).
    static OUString apiKeyFromEnv();

    explicit OpenAICompatibleAdapter(const OUString& rBaseUrl, const OUString& rApiKey = OUString());

    /// Connect probe. "reachable" / "unreachable".
    OUString probe();

    /// GET /v1/models → data[].id list. Empty on any failure.
    std::vector<OUString> listModels();

    /// POST /v1/chat/completions non-stream. Empty on failure.
    /// On failure, lastErrorZh() carries a short Chinese reason (auth/timeout/…).
    OUString chat(const OUString& model, const OUString& prompt);

    /// POST /v1/chat/completions with stream:true (SSE). Invokes rOnChunk for each
    /// content delta; return false from rOnChunk or true from rShouldCancel to abort.
    /// Returns assembled full text; empty on hard failure (see lastErrorZh).
    using StreamChunkFn = std::function<bool(const OUString& rDelta)>;
    using StreamCancelFn = std::function<bool()>;
    OUString chatStream(const OUString& model, const OUString& prompt,
                        const StreamChunkFn& rOnChunk,
                        const StreamCancelFn& rShouldCancel = StreamCancelFn());

    /// Last failure detail from chat()/listModels()/probe path (zh-CN, never secrets).
    OUString lastErrorZh() const { return m_lastErrorZh; }
    int lastHttpCode() const { return m_lastHttpCode; }

    /// Request JSON builder (cppunit / evidence).
    static OString buildChatRequestJson(const OUString& model, const OUString& prompt,
                                        bool bStream = false);

    /// Parse choices[0].message.content from chat completion body.
    static OUString parseChatCompletionJson(const OString& body);

    /// Parse data[].id from models list body.
    static std::vector<OUString> parseModelsJson(const OString& body);

private:
    Endpoint m_ep;
    OUString m_apiKey;
    mutable int m_lastHttpCode = 0;
    mutable OUString m_lastErrorZh;

    OUString absoluteUrl(const char* path) const;
    /// HTTPS via curl; returns body. Non-2xx still returns body when available
    /// so callers can surface 401 message text; sets m_lastHttpCode.
    OString httpsRequest(const char* method, const char* path, const OString& rJsonBody,
                         int timeoutSec) const;
};

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
