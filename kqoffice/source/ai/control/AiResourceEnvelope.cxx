/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "AiResourceEnvelope.hxx"
#include "AiPaths.hxx"

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/ustrbuf.hxx>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <string>

#if defined(MACOSX) || defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace kqoffice::ai::control
{
namespace
{
std::mutex& EnvMutex()
{
    static std::mutex m;
    return m;
}

std::set<std::string>& OnceSet()
{
    static std::set<std::string> s;
    return s;
}

std::map<std::string, sal_Int64>& LastAllowMs()
{
    static std::map<std::string, sal_Int64> m;
    return m;
}

std::atomic<int>& LlmStreamSlots()
{
    static std::atomic<int> n{ 0 };
    return n;
}

sal_Int64 NowMs()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
           + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

OUString InjectPath()
{
    if (const char* env = std::getenv("KQOFFICE_AI_PENDING_PROMPT_INJECT"); env && *env)
        return OUString::fromUtf8(env);
    const OUString cfg = kqofficeAiConfigDir();
    if (cfg.isEmpty())
        return {};
    return cfg + u"/pending-prompt-inject"_ustr;
}

// Idle membership poll stretches when no inject for a while.
sal_Int32 g_idleMembershipStreak = 0;

/// Process working set / max RSS in MB (best-effort). 0 = unknown.
sal_Int64 ProcessRssMb()
{
#if defined(MACOSX) || defined(__APPLE__)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    // macOS: ru_maxrss is bytes
    return static_cast<sal_Int64>(ru.ru_maxrss) / (1024 * 1024);
#elif defined(__linux__)
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    // Linux: ru_maxrss is kilobytes
    return static_cast<sal_Int64>(ru.ru_maxrss) / 1024;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!::GetProcessMemoryInfo(::GetCurrentProcess(),
                                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return 0;
    // Working set is the live RSS-like metric for desktop QoE.
    return static_cast<sal_Int64>(pmc.WorkingSetSize) / (1024 * 1024);
#else
    return 0;
#endif
}
} // namespace

sal_uInt64 AiResourceEnvelope::membershipInjectPollMs()
{
    // Adaptive: 1.5s when active inject likely; up to 5s when idle.
    if (pendingInjectLikelyPresent())
    {
        g_idleMembershipStreak = 0;
        return 1500;
    }
    if (g_idleMembershipStreak < 20)
        ++g_idleMembershipStreak;
    // 1.5s → ~5s
    const sal_uInt64 base = 1500;
    const sal_uInt64 extra = static_cast<sal_uInt64>(g_idleMembershipStreak) * 175;
    return std::min<sal_uInt64>(base + extra, 5000);
}

bool AiResourceEnvelope::pendingInjectLikelyPresent()
{
    const OUString path = InjectPath();
    if (path.isEmpty())
        return false;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(url, item) != osl::FileBase::E_None)
        return false;
    osl::FileStatus st(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileSize);
    if (item.getFileStatus(st) != osl::FileBase::E_None)
        return false;
    if (st.getFileType() != osl::FileStatus::Regular)
        return false;
    return st.getFileSize() > 0;
}

sal_uInt64 AiResourceEnvelope::panelInjectPollMs()
{
    // Panel open: slightly snappier than process-global membership poll, still soft.
    return pendingInjectLikelyPresent() ? 1000 : 2500;
}

sal_Int64 AiResourceEnvelope::membershipStatusCacheTtlMs()
{
    if (const char* e = std::getenv("KQOFFICE_MEMBERSHIP_CACHE_MS"); e && *e)
    {
        const long v = std::atol(e);
        if (v >= 5000 && v <= 300000)
            return static_cast<sal_Int64>(v);
    }
    return 45000; // 45s — chip/status without thrashing api.03122.com
}

sal_Int64 AiResourceEnvelope::routingDiagnoseCacheTtlMs()
{
    if (const char* e = std::getenv("KQOFFICE_ROUTING_DIAG_CACHE_MS"); e && *e)
    {
        const long v = std::atol(e);
        if (v >= 2000 && v <= 120000)
            return static_cast<sal_Int64>(v);
    }
    return 12000; // 12s — absorb double warm-up + chip refresh
}

sal_uInt64 AiResourceEnvelope::scheduleScanIntervalMs()
{
    return 90'000; // 90s while panel open; due tasks still run on open
}

sal_uInt64 AiResourceEnvelope::deferredWarmupMs()
{
    return 450;
}

sal_Int32 AiResourceEnvelope::maxVaultIndexFilesPerPass()
{
    if (const char* e = std::getenv("KQOFFICE_VAULT_INDEX_MAX_FILES"); e && *e)
    {
        const int v = std::atoi(e);
        if (v > 0 && v < 5000)
            return static_cast<sal_Int32>(v);
    }
    // Under soft pressure, smaller batches keep UI fluid.
    if (underSoftMemoryPressure())
        return 24;
    return 48; // enough for interactive; avoids IO storm on huge imports/
}

sal_Int32 AiResourceEnvelope::maxVaultIndexCharsPerFile()
{
    if (const char* e = std::getenv("KQOFFICE_VAULT_INDEX_MAX_CHARS"); e && *e)
    {
        const int v = std::atoi(e);
        if (v >= 2000 && v <= 200000)
            return static_cast<sal_Int32>(v);
    }
    return underSoftMemoryPressure() ? 16000 : 32000;
}

sal_Int32 AiResourceEnvelope::maxCompileExcerptChars()
{
    return underSoftMemoryPressure() ? 4000 : 6000;
}

sal_Int32 AiResourceEnvelope::maxCompileItemsPerPass()
{
    return underSoftMemoryPressure() ? 4 : 6;
}

sal_Int32 AiResourceEnvelope::maxFtsSearchTopK()
{
    return 8;
}

sal_Int32 AiResourceEnvelope::maxConcurrentLlmStreams()
{
    if (const char* e = std::getenv("KQOFFICE_AI_MAX_STREAMS"); e && *e)
    {
        const int v = std::atoi(e);
        if (v >= 1 && v <= 4)
            return static_cast<sal_Int32>(v);
    }
    return underSoftMemoryPressure() ? 1 : 2;
}

bool AiResourceEnvelope::claimLlmStream()
{
    const int max = maxConcurrentLlmStreams();
    int cur = LlmStreamSlots().load(std::memory_order_relaxed);
    while (cur < max)
    {
        if (LlmStreamSlots().compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel))
            return true;
    }
    return false;
}

void AiResourceEnvelope::releaseLlmStream()
{
    int cur = LlmStreamSlots().load(std::memory_order_relaxed);
    while (cur > 0)
    {
        if (LlmStreamSlots().compare_exchange_weak(cur, cur - 1, std::memory_order_acq_rel))
            return;
    }
}

sal_Int64 AiResourceEnvelope::softRssBudgetMb()
{
    if (const char* e = std::getenv("KQOFFICE_AI_SOFT_RSS_MB"); e && *e)
    {
        const long v = std::atol(e);
        if (v >= 512 && v <= 32768)
            return static_cast<sal_Int64>(v);
    }
    // Office baselines can be multi-GB; only soft-defer polish above this.
    return 5120; // 5 GiB process maxrss soft line
}

bool AiResourceEnvelope::underSoftMemoryPressure()
{
    const sal_Int64 rss = ProcessRssMb();
    if (rss <= 0)
        return false;
    return rss >= softRssBudgetMb();
}

bool AiResourceEnvelope::allowHeavyBackgroundWork()
{
    // Soft gate only — callers still apply allowEvery for their own cadence.
    return !underSoftMemoryPressure();
}

bool AiResourceEnvelope::claimOnce(const char* key)
{
    if (!key || !*key)
        return false;
    std::lock_guard<std::mutex> g(EnvMutex());
    auto& s = OnceSet();
    if (s.find(key) != s.end())
        return false;
    s.insert(key);
    return true;
}

bool AiResourceEnvelope::allowEvery(const char* key, sal_Int64 minIntervalMs)
{
    if (!key || !*key || minIntervalMs <= 0)
        return true;
    std::lock_guard<std::mutex> g(EnvMutex());
    const sal_Int64 now = NowMs();
    auto& m = LastAllowMs();
    const auto it = m.find(key);
    if (it != m.end() && (now - it->second) < minIntervalMs)
        return false;
    m[key] = now;
    return true;
}

bool AiResourceEnvelope::allowRelatedMaterialsProbe()
{
    if (!allowHeavyBackgroundWork())
        return false;
    // At most once per 8s process-wide (multiple panels / rapid open).
    return allowEvery("related-materials", 8000);
}

OUString AiResourceEnvelope::summaryLineZh()
{
    OUStringBuffer b;
    b.append(u"资源信封 · 索引≤"_ustr);
    b.append(maxVaultIndexFilesPerPass());
    b.append(u"文件/轮 · 单文件≤"_ustr);
    b.append(maxVaultIndexCharsPerFile() / 1000);
    b.append(u"k字 · 轮询"_ustr);
    b.append(static_cast<sal_Int32>(membershipInjectPollMs()));
    b.append(u"ms · 会员缓存"_ustr);
    b.append(static_cast<sal_Int32>(membershipStatusCacheTtlMs() / 1000));
    b.append(u"s · 流并发≤"_ustr);
    b.append(maxConcurrentLlmStreams());
    const sal_Int64 rss = ProcessRssMb();
    if (rss > 0)
    {
        b.append(u" · RSS≈"_ustr);
        b.append(rss);
        b.append(u"MB"_ustr);
        if (underSoftMemoryPressure())
            b.append(u" · 软压力"_ustr);
    }
    return b.makeStringAndClear();
}

} // namespace kqoffice::ai::control
