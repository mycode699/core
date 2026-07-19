/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "WorkDashboardPanel.hxx"

#include <dispatch/WorkPendantDispatcher.hxx>
#include <WorkTelemetryStore.hxx>
#include <NotebookMaterialStore.hxx>

#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/system/SystemShellExecute.hpp>
#include <com/sun/star/system/SystemShellExecuteFlags.hpp>
#include <com/sun/star/util/URL.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <comphelper/processfactory.hxx>
#include <osl/file.hxx>
#include <algorithm>
#include <cstdlib>
#include <rtl/string.hxx>
#include <rtl/uri.hxx>
#include <rtl/ustrbuf.hxx>
// getenv + OString for pending-prompt inject
#include <sfx2/viewfrm.hxx>
#include <vcl/svapp.hxx>
#include <vcl/unohelp2.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/MessageDialog.hxx>
#include <vcl/weld/TextView.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>

using namespace kqoffice::ai::notebook;

using namespace kqoffice::ai::workbench;

namespace sfx2::sidebar
{
// note: notebook namespace already imported for requestFocusPinned
bool WorkDashboardPanel::openLocalPath(const OUString& rSysPath)
{
    if (rSysPath.isEmpty())
        return false;
    try
    {
        OUString url = rSysPath;
        if (!url.startsWith(u"file:"_ustr))
        {
            if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
                return false;
        }
        css::uno::Reference<css::system::XSystemShellExecute> xExec(
            css::system::SystemShellExecute::create(comphelper::getProcessComponentContext()));
        if (!xExec.is())
            return false;
        xExec->execute(url, OUString(), css::system::SystemShellExecuteFlags::URIS_ONLY);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

WorkDashboardPanel::WorkDashboardPanel(weld::Widget* pParent)
    : PanelLayout(pParent, u"WorkDashboardPanel"_ustr, u"sfx/ui/workdashboard.ui"_ustr)
    , m_xDay(m_xBuilder->weld_radio_button(u"period_day"_ustr))
    , m_xMonth(m_xBuilder->weld_radio_button(u"period_month"_ustr))
    , m_xYear(m_xBuilder->weld_radio_button(u"period_year"_ustr))
    , m_xRefresh(m_xBuilder->weld_button(u"refresh_btn"_ustr))
    , m_xExportDay(m_xBuilder->weld_button(u"export_day_btn"_ustr))
    , m_xExportWeek(m_xBuilder->weld_button(u"export_week_btn"_ustr))
    , m_xExportMonth(m_xBuilder->weld_button(u"export_month_btn"_ustr))
    , m_xPendant(m_xBuilder->weld_button(u"pendant_btn"_ustr))
    , m_xChart(m_xBuilder->weld_button(u"chart_btn"_ustr))
    , m_xReports(m_xBuilder->weld_button(u"reports_btn"_ustr))
    , m_xDesktop(m_xBuilder->weld_button(u"desktop_btn"_ustr))
    , m_xShare(m_xBuilder->weld_button(u"share_btn"_ustr))
    , m_xPackage(m_xBuilder->weld_button(u"package_btn"_ustr))
    , m_xCopySummary(m_xBuilder->weld_button(u"copy_summary_btn"_ustr))
    , m_xEmail(m_xBuilder->weld_button(u"email_btn"_ustr))
    , m_xOpenArchive(m_xBuilder->weld_button(u"open_archive_btn"_ustr))
    , m_xCompleteWeek(m_xBuilder->weld_button(u"complete_week_btn"_ustr))
    , m_xCopyArchive(m_xBuilder->weld_button(u"copy_archive_btn"_ustr))
    , m_xDeleteArchive(m_xBuilder->weld_button(u"delete_archive_btn"_ustr))
    , m_xClearArchive(m_xBuilder->weld_button(u"clear_archive_btn"_ustr))
    , m_xExportArchiveCsv(m_xBuilder->weld_button(u"export_archive_csv_btn"_ustr))
    , m_xWowChart(m_xBuilder->weld_button(u"wow_chart_btn"_ustr))
    , m_xOpenPinned(m_xBuilder->weld_button(u"open_pinned_btn"_ustr))
    , m_xWeekGoal(m_xBuilder->weld_entry(u"week_goal_entry"_ustr))
    , m_xSetWeekGoal(m_xBuilder->weld_button(u"set_week_goal_btn"_ustr))
    , m_xGlance(m_xBuilder->weld_label(u"glance_label"_ustr))
    , m_xCopyGlance(m_xBuilder->weld_button(u"copy_glance_btn"_ustr))
    , m_xGlanceToAi(m_xBuilder->weld_button(u"glance_to_ai_btn"_ustr))
    , m_xPeriodLabel(m_xBuilder->weld_label(u"period_label"_ustr))
    , m_xSummary(m_xBuilder->weld_text_view(u"summary_view"_ustr))
    , m_xArchiveHistory(m_xBuilder->weld_tree_view(u"archive_history_list"_ustr))
{
    if (m_xDay)
        m_xDay->connect_toggled(LINK(this, WorkDashboardPanel, OnPeriodToggled));
    if (m_xMonth)
        m_xMonth->connect_toggled(LINK(this, WorkDashboardPanel, OnPeriodToggled));
    if (m_xYear)
        m_xYear->connect_toggled(LINK(this, WorkDashboardPanel, OnPeriodToggled));
    if (m_xRefresh)
        m_xRefresh->connect_clicked(LINK(this, WorkDashboardPanel, OnRefreshClicked));
    if (m_xExportDay)
        m_xExportDay->connect_clicked(LINK(this, WorkDashboardPanel, OnExportDay));
    if (m_xExportWeek)
        m_xExportWeek->connect_clicked(LINK(this, WorkDashboardPanel, OnExportWeek));
    if (m_xExportMonth)
        m_xExportMonth->connect_clicked(LINK(this, WorkDashboardPanel, OnExportMonth));
    if (m_xPendant)
        m_xPendant->connect_clicked(LINK(this, WorkDashboardPanel, OnPendant));
    if (m_xChart)
        m_xChart->connect_clicked(LINK(this, WorkDashboardPanel, OnOpenChart));
    if (m_xReports)
        m_xReports->connect_clicked(LINK(this, WorkDashboardPanel, OnOpenReports));
    if (m_xDesktop)
        m_xDesktop->connect_clicked(LINK(this, WorkDashboardPanel, OnDesktopShortcuts));
    if (m_xShare)
        m_xShare->connect_clicked(LINK(this, WorkDashboardPanel, OnSharePath));
    if (m_xPackage)
        m_xPackage->connect_clicked(LINK(this, WorkDashboardPanel, OnPackageShare));
    if (m_xCopySummary)
        m_xCopySummary->connect_clicked(LINK(this, WorkDashboardPanel, OnCopySummary));
    if (m_xEmail)
        m_xEmail->connect_clicked(LINK(this, WorkDashboardPanel, OnEmailShare));
    if (m_xOpenArchive)
        m_xOpenArchive->connect_clicked(LINK(this, WorkDashboardPanel, OnOpenArchive));
    if (m_xCompleteWeek)
        m_xCompleteWeek->connect_clicked(LINK(this, WorkDashboardPanel, OnCompleteWeek));
    if (m_xArchiveHistory)
        m_xArchiveHistory->connect_row_activated(LINK(this, WorkDashboardPanel, OnArchiveHistoryActivated));
    if (m_xCopyArchive)
        m_xCopyArchive->connect_clicked(LINK(this, WorkDashboardPanel, OnCopyArchivePath));
    if (m_xDeleteArchive)
        m_xDeleteArchive->connect_clicked(LINK(this, WorkDashboardPanel, OnDeleteArchive));
    if (m_xClearArchive)
        m_xClearArchive->connect_clicked(LINK(this, WorkDashboardPanel, OnClearArchive));
    if (m_xWowChart)
        m_xWowChart->connect_clicked(LINK(this, WorkDashboardPanel, OnWowChart));
    if (m_xOpenPinned)
        m_xOpenPinned->connect_clicked(LINK(this, WorkDashboardPanel, OnOpenPinnedMaterials));
    if (m_xExportArchiveCsv)
        m_xExportArchiveCsv->connect_clicked(LINK(this, WorkDashboardPanel, OnExportArchiveCsv));
    if (m_xSetWeekGoal)
        m_xSetWeekGoal->connect_clicked(LINK(this, WorkDashboardPanel, OnSetWeekGoal));
    if (m_xCopyGlance)
        m_xCopyGlance->connect_clicked(LINK(this, WorkDashboardPanel, OnCopyGlance));
    if (m_xGlanceToAi)
        m_xGlanceToAi->connect_clicked(LINK(this, WorkDashboardPanel, OnGlanceToAi));
    if (m_xSummary)
        m_xSummary->set_editable(false);
    syncWeekGoalEntry();

    WorkTelemetryStore::recordSessionTick(1);
    Reload();
    showOnboardingIfNeeded();
}

WorkDashboardPanel::~WorkDashboardPanel() = default;

OUString WorkDashboardPanel::currentPeriodKey() const
{
    const OUString today = WorkTelemetryStore::todayKey();
    if (m_xYear && m_xYear->get_active())
        return today.getLength() >= 4 ? today.copy(0, 4) : today;
    if (m_xMonth && m_xMonth->get_active())
        return today.getLength() >= 7 ? today.copy(0, 7) : today;
    return today;
}

OUString WorkDashboardPanel::formatSummaryText(const WorkPeriodSummary& s)
{
    OUStringBuffer b;
    b.append(WorkTelemetryStore::formatTodayGlance());
    b.append(u"\n—— 概览 ——\n"_ustr);
    b.append(u"活跃时长（估算分钟）："_ustr);
    b.append(s.activeMinutes);
    // simple bar for minutes (max scale 480 min)
    {
        const sal_Int32 bar = std::min<sal_Int32>(20, std::max<sal_Int32>(0, s.activeMinutes / 24));
        b.append(u"  "_ustr);
        for (sal_Int32 i = 0; i < bar; ++i)
            b.append(u'█');
    }
    b.append(u"\n文稿打开 / 保存 / 新建："_ustr);
    b.append(s.docOpens);
    b.append(u" / "_ustr);
    b.append(s.docSaves);
    b.append(u" / "_ustr);
    b.append(s.docNews);
    b.append(u"\n独立文稿数："_ustr);
    b.append(s.uniqueDocs);
    b.append(u"\nAI 调用："_ustr);
    b.append(s.aiCalls);
    b.append(u"\n预估 Token："_ustr);
    b.append(s.tokensEst);
    b.append(u" （本地估算，非账单）"_ustr);
    b.append(u"\n写回批准："_ustr);
    b.append(s.applies);
    b.append(u"\n截图 / 语音："_ustr);
    b.append(s.screenshots);
    b.append(u" / "_ustr);
    b.append(s.voiceEvents);
    b.append(u"\n测试通过钩子："_ustr);
    b.append(s.testsPassed);
    b.append(u"\n记事本笔记事件："_ustr);
    b.append(s.notebookNotes);

    b.append(u"\n\n—— 模型使用 ——\n"_ustr);
    if (s.models.empty())
        b.append(u"（尚无 AI 调用记录）\n"_ustr);
    else
    {
        sal_Int32 maxCalls = 1;
        for (const auto& m : s.models)
            if (m.calls > maxCalls)
                maxCalls = m.calls;
        sal_Int32 n = 0;
        for (const auto& m : s.models)
        {
            if (n++ >= 8)
                break;
            const sal_Int32 barLen = std::max<sal_Int32>(1, (m.calls * 16) / maxCalls);
            b.append(m.model);
            b.append(u" "_ustr);
            for (sal_Int32 k = 0; k < barLen; ++k)
                b.append(u'█');
            b.append(u" "_ustr);
            b.append(m.calls);
            b.append(u"次 token≈"_ustr);
            b.append(m.tokensEst);
            b.append(u"\n"_ustr);
        }
    }

    b.append(u"\n"_ustr);
    b.append(WorkTelemetryStore::formatWeekGoalProgress());
    b.append(u"\n"_ustr);
    b.append(WorkTelemetryStore::formatDaySparkline(7));
    b.append(u"\n"_ustr);
    b.append(WorkTelemetryStore::formatWeekOverWeekSparkline());
    b.append(u"\n"_ustr);
    b.append(WorkTelemetryStore::formatWeekOverWeekCompare());

    b.append(u"\n—— 文稿触达 ——\n"_ustr);
    if (s.docs.empty())
        b.append(u"（尚无文稿事件；打开文档或使用 AI 后刷新）\n"_ustr);
    else
    {
        sal_Int32 n = 0;
        for (const auto& d : s.docs)
        {
            if (n++ >= 10)
                break;
            b.append(d.docTitle.isEmpty() ? d.docHash : d.docTitle);
            b.append(u" · 开"_ustr);
            b.append(d.opens);
            b.append(u"/存"_ustr);
            b.append(d.saves);
            b.append(u"/AI"_ustr);
            b.append(d.aiTouches);
            b.append(u"\n"_ustr);
        }
    }
    return b.makeStringAndClear();
}

void WorkDashboardPanel::Reload()
{
    const OUString key = currentPeriodKey();
    WorkPeriodSummary s;
    if (key.getLength() == 4)
        s = WorkTelemetryStore::summarizeYear(key);
    else if (key.getLength() == 7)
        s = WorkTelemetryStore::summarizeMonth(key);
    else
        s = WorkTelemetryStore::summarizeDay(key);

    if (m_xGlance)
        m_xGlance->set_label(WorkTelemetryStore::formatTodayGlance());

    if (m_xPeriodLabel)
    {
        OUStringBuffer lab;
        lab.append(u"周期："_ustr);
        lab.append(s.periodLabel);
        if (WorkTelemetryStore::isWeekArchived())
            lab.append(u"  · ✓ 本周已归档"_ustr);
        else
        {
            const OUString tip = WorkTelemetryStore::weeklyArchiveTip();
            if (!tip.isEmpty())
                lab.append(u"  · ⚠ "_ustr + tip);
            else
                lab.append(u"  · 可导出 Markdown 周/日/月报"_ustr);
        }
        m_xPeriodLabel->set_label(lab.makeStringAndClear());
    }

    if (m_xSummary)
    {
        OUString body = formatSummaryText(s);
        if (!WorkTelemetryStore::isWeekArchived())
        {
            body = u"【本周提醒】尚未打包 ZIP/邮件归档。点「打包ZIP」一键生成本地分享包。\n\n"_ustr
                   + body;
        }
        m_xSummary->set_text(body);
    }
    ReloadArchiveHistory();
    syncWeekGoalEntry();
}

void WorkDashboardPanel::syncWeekGoalEntry()
{
    if (!m_xWeekGoal)
        return;
    const sal_Int32 g = WorkTelemetryStore::getWeekGoalMinutes();
    if (g > 0)
        m_xWeekGoal->set_text(OUString::number(g));
    else if (m_xWeekGoal->get_text().trim().isEmpty())
        m_xWeekGoal->set_text(OUString());
}

void WorkDashboardPanel::ReloadArchiveHistory()
{
    if (!m_xArchiveHistory)
        return;
    m_xArchiveHistory->clear();
    const auto items = WorkTelemetryStore::listArchiveHistory(12);
    if (items.empty())
    {
        m_xArchiveHistory->append(OUString(), u"（尚无归档 · 点「完成本周」或「打包ZIP」）"_ustr);
        return;
    }
    sal_Int32 n = 0;
    for (const auto& p : items)
    {
        OUString label = p;
        // show basename if long
        const sal_Int32 slash = p.lastIndexOf(u'/');
        if (slash >= 0 && slash + 1 < p.getLength())
        {
            label = p.copy(slash + 1);
            if (slash > 0)
            {
                const OUString parent = p.copy(0, slash);
                const sal_Int32 slash2 = parent.lastIndexOf(u'/');
                if (slash2 >= 0)
                    label = parent.copy(slash2 + 1) + u"/"_ustr + label;
            }
        }
        if (n == 0)
            label = u"★ "_ustr + label;
        m_xArchiveHistory->append(p, label);
        ++n;
    }
}

OUString WorkDashboardPanel::selectedArchivePath() const
{
    if (!m_xArchiveHistory)
        return OUString();
    OUString sel = m_xArchiveHistory->get_selected_id();
    if (!sel.isEmpty())
        return sel;
    if (m_xArchiveHistory->n_children() > 0)
    {
        const OUString first = m_xArchiveHistory->get_id(0);
        if (!first.isEmpty())
            return first;
    }
    return WorkTelemetryStore::lastArchiveArtifactPath();
}

void WorkDashboardPanel::openArchivePath(const OUString& rPathIn)
{
    OUString art = rPathIn;
    if (art.isEmpty())
        art = WorkTelemetryStore::lastArchiveArtifactPath();
    if (art.isEmpty())
    {
        art = WorkTelemetryStore::rootDir() + u"/reports/work-report-week-"_ustr
              + WorkTelemetryStore::todayKey() + u".html"_ustr;
    }
    auto tryOpen = [&](const OUString& p) -> bool {
        if (p.isEmpty())
            return false;
        return openLocalPath(p);
    };
    if (tryOpen(art))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"已打开归档："_ustr + art);
        return;
    }
    if (art.endsWith(u".zip"_ustr))
    {
        const sal_Int32 slash = art.lastIndexOf(u'/');
        if (slash > 0 && tryOpen(art.copy(0, slash)))
        {
            if (m_xPeriodLabel)
                m_xPeriodLabel->set_label(u"已打开归档目录"_ustr);
            return;
        }
    }
    if (art.endsWith(u".html"_ustr) || art.endsWith(u".md"_ustr))
    {
        OUString base = art;
        if (base.endsWith(u".html"_ustr))
            base = base.copy(0, base.getLength() - 5);
        else if (base.endsWith(u".md"_ustr))
            base = base.copy(0, base.getLength() - 3);
        if (tryOpen(base + u".html"_ustr) || tryOpen(base + u".md"_ustr))
        {
            if (m_xPeriodLabel)
                m_xPeriodLabel->set_label(u"已打开报告文件"_ustr);
            return;
        }
    }
    const OUString dir = WorkTelemetryStore::rootDir() + u"/share"_ustr;
    if (openLocalPath(dir))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"未找到归档文件，已打开 share 目录"_ustr);
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"尚无归档，请先「打包ZIP」或「导出周」"_ustr);
}

void WorkDashboardPanel::DoExport(const OUString& rPeriod, bool bOpenChart)
{
    const OUString path = WorkTelemetryStore::exportReport(rPeriod);
    if (path.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"导出失败"_ustr);
        return;
    }
    // Sibling SVG chart path
    OUString svgPath = path;
    if (svgPath.endsWith(u".md"_ustr))
        svgPath = svgPath.copy(0, svgPath.getLength() - 3) + u".svg"_ustr;

    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"已导出："_ustr + path + u" + SVG"_ustr);

    WorkPeriodSummary s;
    if (rPeriod == u"week"_ustr)
        s = WorkTelemetryStore::summarizeLastDays(7);
    else if (rPeriod == u"month"_ustr)
    {
        const OUString t = WorkTelemetryStore::todayKey();
        s = WorkTelemetryStore::summarizeMonth(t.getLength() >= 7 ? t.copy(0, 7) : t);
    }
    else
        s = WorkTelemetryStore::summarizeToday();

    const OUString md = WorkTelemetryStore::formatReportMarkdown(s);
    if (m_xSummary)
        m_xSummary->set_text(md + u"\n\n（Markdown："_ustr + path + u"\n SVG 图表："_ustr
                             + svgPath + u"）\n\n"_ustr
                             + WorkTelemetryStore::formatDaySparkline(
                                   rPeriod == u"month"_ustr ? 30 : 7));

    if (bOpenChart)
    {
        const OUString htmlPath = WorkTelemetryStore::htmlPathForReport(path);
        const OUString openPath = htmlPath; // prefer HTML with embedded SVG
        if (openLocalPath(openPath) || openLocalPath(svgPath))
        {
            if (m_xPeriodLabel)
                m_xPeriodLabel->set_label(u"已打开图表："_ustr + openPath);
        }
        else if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"图表已导出，但无法打开："_ustr + openPath);
    }
}

void WorkDashboardPanel::showOnboardingIfNeeded()
{
    const OUString tip = WorkTelemetryStore::maybeOnboardingTip(false);
    if (tip.isEmpty())
        return;
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(tip);
    // dismiss after first show so it does not reappear every open
    WorkTelemetryStore::maybeOnboardingTip(true);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnPeriodToggled, weld::Toggleable&, void)
{
    if ((m_xDay && m_xDay->get_active()) || (m_xMonth && m_xMonth->get_active())
        || (m_xYear && m_xYear->get_active()))
        Reload();
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnRefreshClicked, weld::Button&, void) { Reload(); }

IMPL_LINK_NOARG(WorkDashboardPanel, OnExportDay, weld::Button&, void)
{
    DoExport(u"today"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnExportWeek, weld::Button&, void)
{
    DoExport(u"week"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnExportMonth, weld::Button&, void)
{
    DoExport(u"month"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnPendant, weld::Button&, void)
{
    sfx2::WorkPendantDispatcher::Get().Show(GetFrameWeld());
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnOpenChart, weld::Button&, void)
{
    // Default: week chart (most useful overview)
    DoExport(u"week"_ustr, true);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnOpenReports, weld::Button&, void)
{
    const OUString dir = WorkTelemetryStore::rootDir() + u"/reports"_ustr;
    // ensure exists by exporting a tiny week report if missing
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) == osl::FileBase::E_None)
        osl::Directory::createPath(url);
    if (openLocalPath(dir))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"已打开报告目录："_ustr + dir);
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"无法打开："_ustr + dir);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnDesktopShortcuts, weld::Button&, void)
{
    OUString msg;
    const bool ok = WorkTelemetryStore::createDesktopShortcuts(&msg);
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(msg);
    if (m_xSummary && ok)
        m_xSummary->set_text(msg + u"\n\n"_ustr + m_xSummary->get_text());
}

bool WorkDashboardPanel::copyTextToClipboard(const OUString& rText)
{
    if (rText.isEmpty())
        return false;
    try
    {
        weld::Widget* p = m_xSummary ? static_cast<weld::Widget*>(m_xSummary.get())
                                     : static_cast<weld::Widget*>(m_xPeriodLabel.get());
        if (!p)
            return false;
        auto xClip = p->get_clipboard();
        if (!xClip.is())
            return false;
        vcl::unohelper::TextDataObject::CopyStringTo(rText, xClip);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnSharePath, weld::Button&, void)
{
    // Export week report then copy HTML + markdown paths for sharing (local path only).
    const OUString path = WorkTelemetryStore::exportReport(u"week"_ustr);
    if (path.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"导出失败，无法分享"_ustr);
        return;
    }
    const OUString html = WorkTelemetryStore::htmlPathForReport(path);
    OUString svg = path;
    if (svg.endsWith(u".md"_ustr))
        svg = svg.copy(0, svg.getLength() - 3) + u".svg"_ustr;

    OUStringBuffer clip;
    clip.append(u"可圈 工作中台周报（本地文件，可自行发送）\n"_ustr);
    clip.append(u"HTML: "_ustr);
    clip.append(html);
    clip.append(u"\nMarkdown: "_ustr);
    clip.append(path);
    clip.append(u"\nSVG: "_ustr);
    clip.append(svg);
    clip.append(u"\n"_ustr);
    const OUString text = clip.makeStringAndClear();
    if (copyTextToClipboard(text))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"已复制报告路径到剪贴板（可粘贴到微信/邮件）"_ustr);
        if (m_xSummary)
            m_xSummary->set_text(text + u"\n\n"_ustr + m_xSummary->get_text());
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"已导出但复制剪贴板失败：\n"_ustr + html);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnPackageShare, weld::Button&, void)
{
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"正在生成 ZIP…"_ustr);
    const OUString path = WorkTelemetryStore::exportShareZip(u"week"_ustr);
    if (path.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"打包失败"_ustr);
        return;
    }
    const bool isZip = path.endsWith(u".zip"_ustr);
    OUStringBuffer clip;
    clip.append(isZip ? u"可圈 工作中台分享包（ZIP，可直接发送）\n"_ustr
                      : u"可圈 工作中台分享包（文件夹；系统无 zip 时回退）\n"_ustr);
    clip.append(path);
    clip.append(u"\n"_ustr);
    const OUString text = clip.makeStringAndClear();
    copyTextToClipboard(text);
    // Open zip's parent or the folder itself
    if (isZip)
    {
        const sal_Int32 slash = path.lastIndexOf(u'/');
        if (slash > 0)
            openLocalPath(path.copy(0, slash));
        openLocalPath(path); // also try open zip (Archive Utility / Finder)
    }
    else
        openLocalPath(path);
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label((isZip ? u"已生成 ZIP："_ustr : u"已打包文件夹："_ustr) + path);
    if (m_xSummary)
        m_xSummary->set_text(text + u"\n\n"_ustr + m_xSummary->get_text());
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnCopySummary, weld::Button&, void)
{
    const OUString text = m_xSummary ? m_xSummary->get_text() : OUString();
    if (text.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"摘要为空"_ustr);
        return;
    }
    if (copyTextToClipboard(text))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"已复制当前摘要到剪贴板"_ustr);
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"复制失败"_ustr);
}

bool WorkDashboardPanel::openMailto(const OUString& rSubject, const OUString& rBody)
{
    try
    {
        const OUString sub = rtl::Uri::encode(rSubject, rtl_UriCharClassUnoParamValue,
                                              rtl_UriEncodeIgnoreEscapes, RTL_TEXTENCODING_UTF8);
        const OUString body = rtl::Uri::encode(rBody, rtl_UriCharClassUnoParamValue,
                                               rtl_UriEncodeIgnoreEscapes, RTL_TEXTENCODING_UTF8);
        const OUString url = u"mailto:?subject="_ustr + sub + u"&body="_ustr + body;
        css::uno::Reference<css::system::XSystemShellExecute> xExec(
            css::system::SystemShellExecute::create(comphelper::getProcessComponentContext()));
        if (!xExec.is())
            return false;
        // mailto: is a URI; URIS_ONLY is appropriate
        xExec->execute(url, OUString(), css::system::SystemShellExecuteFlags::URIS_ONLY);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void WorkDashboardPanel::doEmailShare()
{
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"正在准备邮件分享…"_ustr);
    const OUString zipOrFolder = WorkTelemetryStore::exportShareZip(u"week"_ustr);
    if (zipOrFolder.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"导出失败"_ustr);
        return;
    }

    const OUString bodyStr = WorkTelemetryStore::formatWeekEmailBody(zipOrFolder);
    copyTextToClipboard(u"请附加文件：\n"_ustr + zipOrFolder + u"\n\n"_ustr + bodyStr);

    // Open folder/zip so user can drag into mail; then open mail draft
    if (zipOrFolder.endsWith(u".zip"_ustr))
    {
        const sal_Int32 slash = zipOrFolder.lastIndexOf(u'/');
        if (slash > 0)
            openLocalPath(zipOrFolder.copy(0, slash));
        openLocalPath(zipOrFolder);
    }
    else
        openLocalPath(zipOrFolder);

    const bool ok = openMailto(u"可圈 工作中台周报"_ustr, bodyStr);
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(
            ok ? (u"已打开邮件草稿 · 请附加："_ustr + zipOrFolder)
               : (u"邮件打开失败，路径已复制："_ustr + zipOrFolder));
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnEmailShare, weld::Button&, void) { doEmailShare(); }

IMPL_LINK_NOARG(WorkDashboardPanel, OnOpenArchive, weld::Button&, void)
{
    openArchivePath(selectedArchivePath());
}

IMPL_LINK(WorkDashboardPanel, OnArchiveHistoryActivated, weld::TreeView&, rTree, bool)
{
    const OUString id = rTree.get_selected_id();
    if (!id.isEmpty())
        openArchivePath(id);
    return true;
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnCopyArchivePath, weld::Button&, void)
{
    const OUString path = selectedArchivePath();
    if (path.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"无归档路径可复制"_ustr);
        return;
    }
    if (copyTextToClipboard(path))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"已复制归档路径："_ustr + path);
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"复制归档路径失败"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnDeleteArchive, weld::Button&, void)
{
    const OUString path = selectedArchivePath();
    if (path.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"请先选择归档记录"_ustr);
        return;
    }
    if (WorkTelemetryStore::removeArchiveHistoryEntry(path))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"已从历史删除（文件仍保留）："_ustr + path);
        ReloadArchiveHistory();
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"删除失败（可能已不在历史中）"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnClearArchive, weld::Button&, void)
{
    if (WorkTelemetryStore::clearArchiveHistory())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"已清空归档历史（本地 ZIP/HTML 未删）"_ustr);
        ReloadArchiveHistory();
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"清空归档历史失败"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnWowChart, weld::Button&, void)
{
    const OUString path = WorkTelemetryStore::exportWeekOverWeekSvg();
    if (path.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"环比图导出失败"_ustr);
        return;
    }
    openLocalPath(path);
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"已打开环比图："_ustr + path);
    if (m_xSummary)
        m_xSummary->set_text(u"环比 SVG："_ustr + path + u"\n\n"_ustr
                             + WorkTelemetryStore::formatWeekOverWeekCompare() + u"\n"_ustr
                             + m_xSummary->get_text());
}

void WorkDashboardPanel::openSidebarDeck(const OUString& rUno)
{
    SfxViewFrame* pFrame = SfxViewFrame::Current();
    if (!pFrame)
        return;
    try
    {
        css::util::URL aUrl;
        aUrl.Complete = rUno;
        auto xTrans = css::util::URLTransformer::create(comphelper::getProcessComponentContext());
        if (xTrans.is())
            xTrans->parseStrict(aUrl);
        css::uno::Reference<css::frame::XDispatchProvider> xProv(
            pFrame->GetFrame().GetFrameInterface(), css::uno::UNO_QUERY);
        if (!xProv.is())
            return;
        auto xDisp = xProv->queryDispatch(aUrl, u"_self"_ustr, 0);
        if (xDisp.is())
            xDisp->dispatch(aUrl, {});
    }
    catch (...)
    {
    }
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnOpenPinnedMaterials, weld::Button&, void)
{
    NotebookMaterialStore::requestFocusPinned();
    openSidebarDeck(u".uno:SidebarDeck.LocalNotebookDeck"_ustr);
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"已打开记事本 · 置顶材料筛选"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnExportArchiveCsv, weld::Button&, void)
{
    const OUString path = WorkTelemetryStore::exportArchiveHistoryCsv();
    if (path.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"导出 CSV 失败"_ustr);
        return;
    }
    openLocalPath(path);
    if (copyTextToClipboard(path) && m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"已导出 CSV 并复制路径："_ustr + path);
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"已导出 CSV："_ustr + path);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnSetWeekGoal, weld::Button&, void)
{
    sal_Int32 mins = 0;
    if (m_xWeekGoal)
    {
        const OUString t = m_xWeekGoal->get_text().trim();
        if (!t.isEmpty())
            mins = t.toInt32();
    }
    if (WorkTelemetryStore::setWeekGoalMinutes(mins))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(mins > 0 ? (u"本周目标已设为 "_ustr + OUString::number(mins)
                                                  + u" 分"_ustr)
                                               : u"已清除本周目标"_ustr);
        Reload();
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"保存目标失败"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnCopyGlance, weld::Button&, void)
{
    const OUString glance = WorkTelemetryStore::formatTodayGlance();
    if (copyTextToClipboard(glance))
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"已复制今日速览到剪贴板"_ustr);
    }
    else if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"复制速览失败"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnGlanceToAi, weld::Button&, void)
{
    // Reuse same pending-prompt path as notebook / voice / screenshot inject
    const OUString brief = WorkTelemetryStore::formatAiContextBrief();
    const char* home = std::getenv("HOME");
    if (!home || !*home || brief.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"无法写入 AI 注入队列"_ustr);
        return;
    }
    const OUString path
        = OUString::fromUtf8(home) + u"/.config/kqoffice/pending-prompt-inject"_ustr;
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(path.copy(0, slash), dirUrl)
            == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"AI 注入队列写入失败"_ustr);
        return;
    }
    f.setSize(0);
    const OString utf8 = OUStringToOString(brief, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
    openSidebarDeck(u".uno:SidebarDeck.AIChatDeck"_ustr);
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"已注入工作中台上下文 → 可圈 AI"_ustr);
}

IMPL_LINK_NOARG(WorkDashboardPanel, OnCompleteWeek, weld::Button&, void)
{
    {
        std::unique_ptr<weld::MessageDialog> xDlg(Application::CreateMessageDialog(
            GetFrameWeld(), VclMessageType::Question, VclButtonsType::YesNo,
            u"将导出本周 ZIP/HTML、打开报告并生成邮件草稿。\n确认完成本周归档？"_ustr));
        if (xDlg->run() != RET_YES)
        {
            if (m_xPeriodLabel)
                m_xPeriodLabel->set_label(u"已取消完成本周"_ustr);
            return;
        }
    }
    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"正在完成本周归档…"_ustr);
    // Full package: md/svg/html + zip when possible
    const OUString zipOrFolder = WorkTelemetryStore::exportShareZip(u"week"_ustr);
    if (zipOrFolder.isEmpty())
    {
        if (m_xPeriodLabel)
            m_xPeriodLabel->set_label(u"完成本周失败"_ustr);
        return;
    }
    // Prefer known week html path (exportShareZip already archived the week)
    const OUString weekHtml = WorkTelemetryStore::rootDir() + u"/reports/work-report-week-"_ustr
                              + WorkTelemetryStore::todayKey() + u".html"_ustr;

    const OUString mailBody = WorkTelemetryStore::formatWeekEmailBody(zipOrFolder);
    OUStringBuffer clip;
    clip.append(u"可圈 本周工作归档完成\n"_ustr);
    clip.append(u"ZIP/目录: "_ustr);
    clip.append(zipOrFolder);
    clip.append(u"\nHTML: "_ustr);
    clip.append(weekHtml);
    clip.append(u"\n\n"_ustr);
    clip.append(mailBody);
    const OUString text = clip.makeStringAndClear();
    copyTextToClipboard(text);

    if (!openLocalPath(weekHtml))
        openLocalPath(zipOrFolder);
    // Open mail draft with rich week body (user still attaches ZIP manually)
    openMailto(u"可圈 本周工作归档"_ustr, mailBody);

    if (m_xPeriodLabel)
        m_xPeriodLabel->set_label(u"✓ 本周已完成 · 邮件草稿已打开 · "_ustr + zipOrFolder);
    Reload();
    if (m_xSummary)
        m_xSummary->set_text(text + u"\n\n"_ustr + m_xSummary->get_text());
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
