/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * AI surface resource envelope — keep memory/CPU/IO bounded while preserving
 * 可圈 AI / 资料盘 responsiveness (QoE over raw throughput).
 *
 * Philosophy (Thunder/Quark/WPS-class desktop):
 *   - Idle cheap, work bursts bounded
 *   - Prefer local FTS over heavy LLM
 *   - Never thrash disk/network on every timer tick
 *   - Soft pressure: defer background polish under high RSS
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_AIRESOURCEENVELOPE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_AIRESOURCEENVELOPE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::control
{
/// Process-wide soft budgets for AI / vault / membership background work.
class SAL_DLLPUBLIC_EXPORT AiResourceEnvelope
{
public:
    /// Membership inject poll interval (ms). Adaptive: longer when idle.
    static sal_uInt64 membershipInjectPollMs();

    /// True if inject path exists and is non-empty (cheap stat; avoids open).
    static bool pendingInjectLikelyPresent();

    /// AIChat inject poll when panel open (ms).
    static sal_uInt64 panelInjectPollMs();

    /// Status/quota HTTP cache TTL (ms). Mutations invalidate.
    static sal_Int64 membershipStatusCacheTtlMs();

    /// Full routing diagnose soft cache (ms) — cold-open may call twice.
    static sal_Int64 routingDiagnoseCacheTtlMs();

    /// Local schedule due-scan interval while AI panel open (ms).
    static sal_uInt64 scheduleScanIntervalMs();

    /// Deferred warmup after AI panel first paint (ms).
    static sal_uInt64 deferredWarmupMs();

    /// Max vault import files to FTS-index per reindex/pass.
    static sal_Int32 maxVaultIndexFilesPerPass();

    /// Max characters read from one vault file for FTS.
    static sal_Int32 maxVaultIndexCharsPerFile();

    /// Max bytes for LLM compile input excerpt.
    static sal_Int32 maxCompileExcerptChars();

    /// Max pending raw items compile per user trigger.
    static sal_Int32 maxCompileItemsPerPass();

    /// Default FTS search top-K (capped).
    static sal_Int32 maxFtsSearchTopK();

    /// Soft concurrent LLM streams (panel + agent). claim/release pair.
    static sal_Int32 maxConcurrentLlmStreams();
    static bool claimLlmStream();
    static void releaseLlmStream();

    /// Soft process pressure (RSS) — skip non-critical background polish.
    static bool underSoftMemoryPressure();

    /// True if related-materials / optional polish should run.
    static bool allowHeavyBackgroundWork();

    /// True once per process for key (e.g. "vault-install-defaults").
    static bool claimOnce(const char* key);

    /// Rate-limit: true if at least minIntervalMs elapsed since last allow for key.
    static bool allowEvery(const char* key, sal_Int64 minIntervalMs);

    /// Related-materials probe allowed (once per panel session window).
    static bool allowRelatedMaterialsProbe();

    /// Soft process RSS budget for AI-side guidance (MB). Env override.
    static sal_Int64 softRssBudgetMb();

    /// One-line status for diagnostics (/vault status can append).
    static OUString summaryLineZh();
};

} // namespace kqoffice::ai::control

#endif
