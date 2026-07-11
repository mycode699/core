/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Local workbench telemetry (iPhone-battery-style insights).
 * Events: ~/.config/kqoffice/workbench/telemetry/YYYY-MM-DD.jsonl
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_WORKBENCH_WORKTELEMETRYSTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_WORKBENCH_WORKTELEMETRYSTORE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <map>
#include <vector>

namespace kqoffice::ai::workbench
{

struct WorkTelemetryEvent
{
    OUString kind; ///< session_tick|doc_open|doc_save|doc_new|ai_call|ai_apply|screenshot|voice|test_pass|notebook_note
    OUString tsIso; ///< filled by store if empty
    OUString model;
    OUString capability;
    OUString docHash;
    OUString docTitle;
    OUString status;
    sal_Int32 tokensEst = 0; ///< estimated tokens (chars/4)
    sal_Int32 durationMs = 0;
    sal_Int32 count = 1;
};

struct ModelUsageRow
{
    OUString model;
    sal_Int32 calls = 0;
    sal_Int32 tokensEst = 0;
};

struct DocUsageRow
{
    OUString docHash;
    OUString docTitle;
    sal_Int32 opens = 0;
    sal_Int32 saves = 0;
    sal_Int32 aiTouches = 0;
};

struct WorkPeriodSummary
{
    OUString periodLabel; ///< e.g. 2026-07-11 | 2026-07 | 2026
    sal_Int32 activeMinutes = 0;
    sal_Int32 docOpens = 0;
    sal_Int32 docSaves = 0;
    sal_Int32 docNews = 0;
    sal_Int32 uniqueDocs = 0;
    sal_Int32 aiCalls = 0;
    sal_Int32 tokensEst = 0;
    sal_Int32 screenshots = 0;
    sal_Int32 voiceEvents = 0;
    sal_Int32 testsPassed = 0;
    sal_Int32 notebookNotes = 0;
    sal_Int32 applies = 0;
    std::vector<ModelUsageRow> models;
    std::vector<DocUsageRow> docs;
};

class SAL_DLLPUBLIC_EXPORT WorkTelemetryStore
{
public:
    static OUString rootDir();
    static OUString telemetryDir();
    static OUString todayKey(); ///< YYYY-MM-DD local
    /// Local calendar day key nDaysAgo (0 = today).
    static OUString dayKeyOffset(sal_Int32 nDaysAgo);
    /// True if rDayKey (YYYY-MM-DD) is within last nDays including today.
    static bool isDayWithinLastN(const OUString& rDayKey, sal_Int32 nDays);

    /// Append one event (never throws). Returns false on IO failure.
    static bool record(const WorkTelemetryEvent& rEvent);

    /// Convenience helpers
    static bool recordAiCall(const OUString& rModel, const OUString& rCapability,
                             sal_Int32 nRequestChars, sal_Int32 nResponseChars,
                             sal_Int32 nDurationMs, const OUString& rStatus,
                             const OUString& rDocTitle = OUString());
    static bool recordSessionTick(sal_Int32 nMinutes = 1);
    static bool recordDoc(const OUString& rKind /*open|save|new*/, const OUString& rTitle,
                          const OUString& rUrlOrId = OUString());
    static bool recordSimple(const OUString& rKind, sal_Int32 nCount = 1);

    /// Aggregate day (YYYY-MM-DD), month (YYYY-MM), or year (YYYY).
    static WorkPeriodSummary summarizeDay(const OUString& rDayKey);
    static WorkPeriodSummary summarizeMonth(const OUString& rMonthKey);
    static WorkPeriodSummary summarizeYear(const OUString& rYearKey);
    static WorkPeriodSummary summarizeToday();
    /// Rolling last N calendar days including today (default 7 = week).
    static WorkPeriodSummary summarizeLastDays(sal_Int32 nDays = 7);
    /// Rolling window starting nStartOffsetDays ago (0 = today), for nDays.
    /// e.g. (0,7)=本周滚动, (7,7)=上周滚动.
    static WorkPeriodSummary summarizeDaysOffset(sal_Int32 nStartOffsetDays, sal_Int32 nDays);

    /// Per-day bars for last N days (oldest → newest), for sparkline UI.
    /// Returns lines like "07-05 ████ 12m · AI 3".
    static OUString formatDaySparkline(sal_Int32 nDays = 7);

    /// Compact 本周 vs 上周 compare block (minutes / AI / tokens).
    static OUString formatWeekOverWeekCompare();
    /// One-line mini bar chart for pendant (e.g. ⏱ ████ 120 vs ███ 80 · AI …).
    static OUString formatWeekOverWeekSparkline();
    /// Compact standalone SVG for 本周 vs 上周 (side-by-side bars).
    static OUString formatWeekOverWeekSvg();
    /// Write wow-YYYY-MM-DD.svg under reports/ and return system path.
    static OUString exportWeekOverWeekSvg();

    /// Vector SVG chart (near-term activity + model usage). Local file / embeddable.
    static OUString formatChartSvg(const WorkPeriodSummary& rSummary, sal_Int32 nSparkDays = 7);

    /// Markdown report body for a summary (local, human-readable).
    static OUString formatReportMarkdown(const WorkPeriodSummary& rSummary,
                                         const OUString& rTitleExtra = OUString());

    /// Single-file HTML report with embedded SVG (browser-friendly).
    static OUString formatReportHtml(const WorkPeriodSummary& rSummary, const OUString& rSvg,
                                     const OUString& rTitleExtra = OUString());

    /// Write report under ~/.config/kqoffice/workbench/reports/ and return .md path.
    /// Also writes sibling .svg + .html. period: "today" | "week" | "month" | ...
    static OUString exportReport(const OUString& rPeriod);

    /// Export then copy md/svg/html into a share bundle folder (easy to zip/send).
    /// Returns system path of the bundle directory, or empty on failure.
    static OUString exportShareBundle(const OUString& rPeriod);

    /// Same as exportShareBundle, then create a .zip beside the folder (if zip available).
    /// Returns zip path on success; falls back to folder path if zip fails.
    static OUString exportShareZip(const OUString& rPeriod);

    /// Path of last HTML sibling for a markdown report path (…md → …html).
    static OUString htmlPathForReport(const OUString& rMarkdownPath);

    /// Create Desktop launcher helpers (macOS .command / Linux .desktop). Local only.
    static bool createDesktopShortcuts(OUString* pMessage = nullptr);

    /// First-open tip; returns non-empty once until dismissed (flag file written).
    static OUString maybeOnboardingTip(bool bDismiss = false);

    /// Current week archive key e.g. week-2026-07-11 (rolling week ending today).
    static OUString weekArchiveKey();
    /// Whether user already exported share/zip for this week key.
    static bool isWeekArchived();
    /// Mark current week as archived (called after successful share zip).
    static void markWeekArchived(const OUString& rArtifactPath = OUString());
    /// Non-empty tip when this week has not been archived yet.
    static OUString weeklyArchiveTip();
    /// Last archived artifact path from last-week-archive.txt (line 2), if any.
    static OUString lastArchiveArtifactPath();
    /// Recent archive artifact paths (newest first), from archive-history.jsonl.
    static std::vector<OUString> listArchiveHistory(sal_Int32 nMax = 12);
    /// Multi-line tip of recent archives for dashboard/pendant.
    static OUString formatArchiveHistoryTip(sal_Int32 nMax = 5);
    /// Remove one history entry by exact path; returns true if removed.
    static bool removeArchiveHistoryEntry(const OUString& rPath);
    /// Clear all archive history (local file only; does not delete zip/html).
    static bool clearArchiveHistory();
    /// Export archive history as CSV under reports/; returns path or empty.
    static OUString exportArchiveHistoryCsv();

    /// Local week goal (active minutes target for rolling 7d). 0 = unset.
    static sal_Int32 getWeekGoalMinutes();
    static bool setWeekGoalMinutes(sal_Int32 nMinutes);
    /// Progress line: 目标 480 分 · 本周 120 · 进度 25% ████░░░░
    static OUString formatWeekGoalProgress();
    /// Rich email body for week share (summary + goal + wow + artifact path).
    static OUString formatWeekEmailBody(const OUString& rArtifactPath = OUString());
    /// Compact multi-line "今日速览" cards for dashboard top.
    static OUString formatTodayGlance();
    /// Prompt-ready local workbench brief for AI chat inject (no network).
    static OUString formatAiContextBrief();

    /// Once per local day: if weekend-ish (Fri–Sun) and week not archived,
    /// fire a local system notification (macOS) / write tip file. Never throws.
    static bool maybeWeeklySystemReminder();

    /// Once per local day: if week goal set and rolling-7d minutes reached,
    /// fire a local system notification. Never throws.
    static bool maybeWeekGoalReachedReminder();

    /// Estimate tokens from character counts.
    static sal_Int32 estimateTokens(sal_Int32 nChars);

    static OUString makeDocHash(const OUString& rIdentity);
};

} // namespace kqoffice::ai::workbench

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
