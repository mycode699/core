/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "ProviderStreamHelper.hxx"

#include "ModelRoles.hxx"
#include "ModelRoutingConfig.hxx"
#include "OllamaAdapter.hxx"
#include "OpenAICompatibleAdapter.hxx"
#include "ServiceModePolicy.hxx"

#include <AiResourceEnvelope.hxx>

#include <cstdlib>
#include <osl/time.h>

namespace kqoffice::ai
{
namespace
{
sal_Int32 elapsedMs(const TimeValue& aStart)
{
    TimeValue aEnd{};
    osl_getSystemTime(&aEnd);
    return static_cast<sal_Int32>((aEnd.Seconds - aStart.Seconds) * 1000
                                  + static_cast<sal_Int64>(aEnd.Nanosec - aStart.Nanosec)
                                        / 1000000);
}

struct LlmStreamSlotGuard
{
    bool held = false;
    LlmStreamSlotGuard()
    {
        held = kqoffice::ai::control::AiResourceEnvelope::claimLlmStream();
    }
    ~LlmStreamSlotGuard()
    {
        if (held)
            kqoffice::ai::control::AiResourceEnvelope::releaseLlmStream();
    }
    LlmStreamSlotGuard(const LlmStreamSlotGuard&) = delete;
    LlmStreamSlotGuard& operator=(const LlmStreamSlotGuard&) = delete;
};
} // namespace

StreamChatResult streamChatCompletion(const OUString& rCapability, const OUString& rPrompt,
                                      const StreamChunkFn& rOnChunk,
                                      const StreamCancelFn& rShouldCancel)
{
    StreamChatResult out;
    TimeValue aStart{};
    osl_getSystemTime(&aStart);

    if (rCapability.isEmpty())
    {
        out.status = u"provider-error"_ustr;
        out.content = u"capability 不能为空"_ustr;
        out.durationMs = elapsedMs(aStart);
        return out;
    }

    ServiceModePolicy policy;
    if (!policy.allows(rCapability))
    {
        out.status = u"policy-denied"_ustr;
        out.content = u"当前服务模式不允许此能力"_ustr;
        out.durationMs = elapsedMs(aStart);
        return out;
    }

    if (std::getenv("KQOFFICE_AI_DISABLE_PROBE") != nullptr)
    {
        out.status = u"provider-error"_ustr;
        out.content = u"no provider backend registered (W1 Day-0 stub)"_ustr;
        out.durationMs = elapsedMs(aStart);
        return out;
    }

    // Soft concurrency ceiling (default 2; 1 under memory pressure).
    LlmStreamSlotGuard streamSlot;
    if (!streamSlot.held)
    {
        out.status = u"provider-error"_ustr;
        out.content = u"当前 AI 请求过多，请稍候再试（资源信封并发上限）"_ustr;
        out.durationMs = elapsedMs(aStart);
        return out;
    }

    auto cancelled = [&]() { return rShouldCancel && rShouldCancel(); };

    ensureDefaultModelRoutingTemplate();
    const ModelRoutingSnapshot routing = loadModelRoutingSnapshot();
    const OUString backend
        = routing.backend.isEmpty() ? u"ollama"_ustr : routing.backend.toAsciiLowerCase();

    if (OpenAICompatibleAdapter::isOpenAICompatibleBackend(backend))
    {
        const OUString baseUrl
            = routing.baseUrl.isEmpty() ? u"http://127.0.0.1:8080"_ustr : routing.baseUrl;
        OpenAICompatibleAdapter adapter(baseUrl, OpenAICompatibleAdapter::apiKeyFromEnv());
        if (adapter.probe() != u"reachable"_ustr)
        {
            out.status = u"provider-error"_ustr;
            out.content = u"openai-compatible gateway unreachable at "_ustr + baseUrl;
            out.durationMs = elapsedMs(aStart);
            return out;
        }
        auto models = adapter.listModels();
        const ModelRoleResolution resolved
            = resolveModelForCapability(rCapability, routing, models);
        const OUString modelId
            = resolved.model.isEmpty() ? routing.primaryModel : resolved.model;
        out.providerLabel = u"openai-compatible: "_ustr
                            + (modelId.isEmpty() ? u"?"_ustr : modelId);
        if (modelId.isEmpty())
        {
            out.status = u"provider-error"_ustr;
            out.content = u"openai-compatible: no model resolved"_ustr;
            out.durationMs = elapsedMs(aStart);
            return out;
        }
        if (cancelled())
        {
            out.status = u"cancelled"_ustr;
            out.durationMs = elapsedMs(aStart);
            return out;
        }
        const OUString text = adapter.chatStream(modelId, rPrompt, rOnChunk, rShouldCancel);
        if (cancelled())
        {
            out.status = u"cancelled"_ustr;
            out.content = text;
            out.durationMs = elapsedMs(aStart);
            return out;
        }
        if (text.isEmpty())
        {
            // Fall back to non-stream chat once — some gateways reject stream.
            const OUString fallback = adapter.chat(modelId, rPrompt);
            if (fallback.isEmpty())
            {
                out.status = u"provider-error"_ustr;
                const OUString err = adapter.lastErrorZh();
                out.content = err.isEmpty()
                                  ? (u"openai-compatible chat/stream failed for model "_ustr
                                     + modelId)
                                  : err;
            }
            else
            {
                out.status = u"ok"_ustr;
                out.content = fallback;
                if (rOnChunk)
                    (void)rOnChunk(fallback);
            }
        }
        else
        {
            out.status = u"ok"_ustr;
            out.content = text;
        }
        out.durationMs = elapsedMs(aStart);
        return out;
    }

    // Default: Ollama
    OllamaAdapter adapter;
    if (adapter.probe() != u"reachable"_ustr)
    {
        out.status = u"provider-error"_ustr;
        out.content = u"ollama unreachable at 127.0.0.1:11434"_ustr;
        out.durationMs = elapsedMs(aStart);
        return out;
    }
    auto models = adapter.listModels();
    const ModelRoleResolution resolved = resolveModelForCapability(rCapability, routing, models);
    const OUString modelId = resolved.model;
    out.providerLabel = u"ollama: "_ustr + (modelId.isEmpty() ? u"?"_ustr : modelId);
    if (modelId.isEmpty())
    {
        out.status = u"provider-error"_ustr;
        out.content = u"no model resolved for capability "_ustr + rCapability;
        out.durationMs = elapsedMs(aStart);
        return out;
    }
    if (cancelled())
    {
        out.status = u"cancelled"_ustr;
        out.durationMs = elapsedMs(aStart);
        return out;
    }
    const OUString text = adapter.generateStream(modelId, rPrompt, rOnChunk, rShouldCancel);
    if (cancelled())
    {
        out.status = u"cancelled"_ustr;
        out.content = text;
        out.durationMs = elapsedMs(aStart);
        return out;
    }
    if (text.isEmpty())
    {
        // Fall back to non-stream generate (JSON mode) — keep apply-friendly path.
        const OUString fallback = adapter.generate(modelId, rPrompt);
        if (fallback.isEmpty())
        {
            out.status = u"provider-error"_ustr;
            out.content = u"ollama generate/stream failed for model "_ustr + modelId;
        }
        else
        {
            out.status = u"ok"_ustr;
            out.content = fallback;
            if (rOnChunk)
                (void)rOnChunk(fallback);
        }
    }
    else
    {
        out.status = u"ok"_ustr;
        out.content = text;
    }
    out.durationMs = elapsedMs(aStart);
    return out;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
