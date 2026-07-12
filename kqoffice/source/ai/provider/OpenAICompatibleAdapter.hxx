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

#include <vector>

namespace kqoffice::ai
{
/// Thin HTTP client for OpenAI Chat Completions API shape:
///   POST {base}/v1/chat/completions
///   GET  {base}/v1/models
///
/// Pure BSD sockets (same discipline as OllamaAdapter). Never throws.
class SAL_DLLPUBLIC_EXPORT OpenAICompatibleAdapter
{
public:
    struct Endpoint
    {
        OUString host; // default 127.0.0.1
        int port = 8080;
        OUString pathPrefix; // e.g. empty or "/v1" stripped — internal uses /v1/...
        bool valid = false;
    };

    /// Parse baseUrl like "http://127.0.0.1:8080" or "http://host:8000/v1".
    /// Rejects https:// (TLS not wired) and empty hosts.
    static Endpoint parseBaseUrl(const OUString& rBaseUrl);

    /// True when routing.backend is openai / openai-compatible / openai-compat / openai_compatible.
    static bool isOpenAICompatibleBackend(const OUString& rBackend);

    /// API key from env KQOFFICE_AI_API_KEY (optional for local gateways).
    static OUString apiKeyFromEnv();

    explicit OpenAICompatibleAdapter(const OUString& rBaseUrl, const OUString& rApiKey = OUString());

    /// Connect probe (100ms). "reachable" / "unreachable".
    OUString probe();

    /// GET /v1/models → data[].id list. Empty on any failure.
    std::vector<OUString> listModels();

    /// POST /v1/chat/completions non-stream. Empty on failure.
    OUString chat(const OUString& model, const OUString& prompt);

    /// Request JSON builder (cppunit / evidence).
    static OString buildChatRequestJson(const OUString& model, const OUString& prompt);

    /// Parse choices[0].message.content from chat completion body.
    static OUString parseChatCompletionJson(const OString& body);

    /// Parse data[].id from models list body.
    static std::vector<OUString> parseModelsJson(const OString& body);

private:
    Endpoint m_ep;
    OUString m_apiKey;
};

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
