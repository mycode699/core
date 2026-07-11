/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <dispatch/WorkPendantDispatcher.hxx>
#include <dispatch/AIInputDispatcher.hxx>

#include <WorkTelemetryStore.hxx>

#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/system/SystemShellExecute.hpp>
#include <com/sun/star/system/SystemShellExecuteFlags.hpp>
#include <com/sun/star/util/URL.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
// mailto share uses SystemShellExecute
#include <comphelper/processfactory.hxx>
#include <osl/file.hxx>
#include <rtl/uri.hxx>
#include <rtl/ustrbuf.hxx>
#include <sfx2/viewfrm.hxx>
#include <vcl/svapp.hxx>
#include <vcl/timer.hxx>
#include <vcl/unohelp2.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/DialogController.hxx>
#include <vcl/weld/MessageDialog.hxx>
#include <vcl/weld/weld.hxx>

#include <memory>

using namespace kqoffice::ai::workbench;

namespace sfx2
{
namespace
{
bool openLocalPath(const OUString& rSysPath)
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

class WorkPendantController final : public weld::GenericDialogController
{
public:
    explicit WorkPendantController(weld::Widget* pParent)
        : GenericDialogController(pParent, u"sfx/ui/workpendant.ui"_ustr, u"WorkPendantDialog"_ustr)
        , m_xStats(m_xBuilder->weld_label(u"stats_label"_ustr))
        , m_xSpark(m_xBuilder->weld_label(u"spark_label"_ustr))
        , m_xShot(m_xBuilder->weld_button(u"btn_shot"_ustr))
        , m_xVoice(m_xBuilder->weld_button(u"btn_voice"_ustr))
        , m_xRefresh(m_xBuilder->weld_button(u"btn_refresh"_ustr))
        , m_xWorkbench(m_xBuilder->weld_button(u"btn_workbench"_ustr))
        , m_xNotebook(m_xBuilder->weld_button(u"btn_notebook"_ustr))
        , m_xAi(m_xBuilder->weld_button(u"btn_ai"_ustr))
        , m_xChart(m_xBuilder->weld_button(u"btn_chart"_ustr))
        , m_xReports(m_xBuilder->weld_button(u"btn_reports"_ustr))
        , m_xPack(m_xBuilder->weld_button(u"btn_pack"_ustr))
        , m_xMail(m_xBuilder->weld_button(u"btn_mail"_ustr))
        , m_xDoneWeek(m_xBuilder->weld_button(u"btn_done_week"_ustr))
        , m_xWow(m_xBuilder->weld_button(u"btn_wow"_ustr))
        , m_aRefresh("WorkPendantRefresh")
    {
        if (m_xShot)
            m_xShot->connect_clicked(LINK(this, WorkPendantController, OnShot));
        if (m_xVoice)
            m_xVoice->connect_clicked(LINK(this, WorkPendantController, OnVoice));
        if (m_xRefresh)
            m_xRefresh->connect_clicked(LINK(this, WorkPendantController, OnRefresh));
        if (m_xWorkbench)
            m_xWorkbench->connect_clicked(LINK(this, WorkPendantController, OnWorkbench));
        if (m_xNotebook)
            m_xNotebook->connect_clicked(LINK(this, WorkPendantController, OnNotebook));
        if (m_xAi)
            m_xAi->connect_clicked(LINK(this, WorkPendantController, OnAi));
        if (m_xChart)
            m_xChart->connect_clicked(LINK(this, WorkPendantController, OnChart));
        if (m_xReports)
            m_xReports->connect_clicked(LINK(this, WorkPendantController, OnReports));
        if (m_xPack)
            m_xPack->connect_clicked(LINK(this, WorkPendantController, OnPack));
        if (m_xMail)
            m_xMail->connect_clicked(LINK(this, WorkPendantController, OnMail));
        if (m_xDoneWeek)
            m_xDoneWeek->connect_clicked(LINK(this, WorkPendantController, OnDoneWeek));
        if (m_xWow)
            m_xWow->connect_clicked(LINK(this, WorkPendantController, OnWow));
        m_aRefresh.SetTimeout(30 * 1000);
        m_aRefresh.SetInvokeHandler(LINK(this, WorkPendantController, OnTick));
        m_aRefresh.Start();
        RefreshStats();
    }

    ~WorkPendantController() override { m_aRefresh.Stop(); }

    void RefreshStats()
    {
        const WorkPeriodSummary s = WorkTelemetryStore::summarizeToday();
        OUStringBuffer b;
        b.append(u"⏱ "_ustr);
        b.append(s.activeMinutes);
        b.append(u" 分 · 📄 "_ustr);
        b.append(s.uniqueDocs);
        b.append(u" 文稿\nAI "_ustr);
        b.append(s.aiCalls);
        b.append(u" 次 · token≈"_ustr);
        b.append(s.tokensEst);
        b.append(u"\n截图 "_ustr);
        b.append(s.screenshots);
        b.append(u" · 语音 "_ustr);
        b.append(s.voiceEvents);
        b.append(u" · 测试 "_ustr);
        b.append(s.testsPassed);
        if (m_xStats)
            m_xStats->set_label(b.makeStringAndClear());

        // Compact: today glance + archive tip + goal + WoW + 7-day spark
        if (m_xSpark)
        {
            OUStringBuffer sb;
            OUString glance = WorkTelemetryStore::formatTodayGlance();
            // keep glance short for pendant
            if (glance.getLength() > 160)
                glance = glance.copy(0, 160) + u"…"_ustr;
            sb.append(glance);
            if (!glance.endsWith(u"\n"_ustr))
                sb.append(u'\n');
            const OUString tip = WorkTelemetryStore::weeklyArchiveTip();
            if (!tip.isEmpty())
                sb.append(u"⚠ "_ustr + tip + u"\n"_ustr);
            else
                sb.append(u"✓ 本周已归档\n"_ustr);
            sb.append(WorkTelemetryStore::formatWeekGoalProgress());
            sb.append(u"\n"_ustr);
            sb.append(WorkTelemetryStore::formatWeekOverWeekSparkline());
            sb.append(u"\n"_ustr);
            OUString spark = WorkTelemetryStore::formatDaySparkline(7);
            if (spark.getLength() > 220)
                spark = spark.copy(0, 220) + u"…"_ustr;
            sb.append(spark);
            OUString out = sb.makeStringAndClear();
            if (out.getLength() > 560)
                out = out.copy(0, 560) + u"…"_ustr;
            m_xSpark->set_label(out);
        }
    }

private:
    DECL_LINK(OnShot, weld::Button&, void);
    DECL_LINK(OnVoice, weld::Button&, void);
    DECL_LINK(OnRefresh, weld::Button&, void);
    DECL_LINK(OnWorkbench, weld::Button&, void);
    DECL_LINK(OnNotebook, weld::Button&, void);
    DECL_LINK(OnAi, weld::Button&, void);
    DECL_LINK(OnChart, weld::Button&, void);
    DECL_LINK(OnReports, weld::Button&, void);
    DECL_LINK(OnPack, weld::Button&, void);
    DECL_LINK(OnMail, weld::Button&, void);
    DECL_LINK(OnDoneWeek, weld::Button&, void);
    DECL_LINK(OnWow, weld::Button&, void);
    DECL_LINK(OnTick, Timer*, void);

    bool copyToClipboard(const OUString& rText)
    {
        if (rText.isEmpty() || !m_xStats)
            return false;
        try
        {
            auto xClip = m_xStats->get_clipboard();
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

    static bool openMailto(const OUString& rSubject, const OUString& rBody)
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
            xExec->execute(url, OUString(), css::system::SystemShellExecuteFlags::URIS_ONLY);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static void openDeck(const OUString& rUno)
    {
        SfxViewFrame* pFrame = SfxViewFrame::Current();
        if (!pFrame)
            return;
        try
        {
            css::util::URL aUrl;
            aUrl.Complete = rUno;
            auto xTrans
                = css::util::URLTransformer::create(comphelper::getProcessComponentContext());
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

    std::unique_ptr<weld::Label> m_xStats;
    std::unique_ptr<weld::Label> m_xSpark;
    std::unique_ptr<weld::Button> m_xShot;
    std::unique_ptr<weld::Button> m_xVoice;
    std::unique_ptr<weld::Button> m_xRefresh;
    std::unique_ptr<weld::Button> m_xWorkbench;
    std::unique_ptr<weld::Button> m_xNotebook;
    std::unique_ptr<weld::Button> m_xAi;
    std::unique_ptr<weld::Button> m_xChart;
    std::unique_ptr<weld::Button> m_xReports;
    std::unique_ptr<weld::Button> m_xPack;
    std::unique_ptr<weld::Button> m_xMail;
    std::unique_ptr<weld::Button> m_xDoneWeek;
    std::unique_ptr<weld::Button> m_xWow;
    Timer m_aRefresh;
};

std::shared_ptr<WorkPendantController> g_pPendant;

IMPL_LINK_NOARG(WorkPendantController, OnShot, weld::Button&, void)
{
    AIInputDispatcher::Get().TriggerScreenshotRegion(SfxViewFrame::Current());
    RefreshStats();
}

IMPL_LINK_NOARG(WorkPendantController, OnVoice, weld::Button&, void)
{
    AIInputDispatcher::Get().TriggerVoice(SfxViewFrame::Current());
    RefreshStats();
}

IMPL_LINK_NOARG(WorkPendantController, OnRefresh, weld::Button&, void) { RefreshStats(); }

IMPL_LINK_NOARG(WorkPendantController, OnWorkbench, weld::Button&, void)
{
    openDeck(u".uno:SidebarDeck.WorkDashboardDeck"_ustr);
}

IMPL_LINK_NOARG(WorkPendantController, OnNotebook, weld::Button&, void)
{
    openDeck(u".uno:SidebarDeck.LocalNotebookDeck"_ustr);
}

IMPL_LINK_NOARG(WorkPendantController, OnAi, weld::Button&, void)
{
    // Preload workbench local context into AI prompt inject queue
    const OUString brief = WorkTelemetryStore::formatAiContextBrief();
    const char* home = std::getenv("HOME");
    if (home && *home && !brief.isEmpty())
    {
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
        if (osl::FileBase::getFileURLFromSystemPath(path, url) == osl::FileBase::E_None)
        {
            osl::File f(url);
            auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
            if (e != osl::FileBase::E_None)
                e = f.open(osl_File_OpenFlag_Write);
            if (e == osl::FileBase::E_None)
            {
                f.setSize(0);
                const OString utf8 = OUStringToOString(brief, RTL_TEXTENCODING_UTF8);
                sal_uInt64 n = 0;
                f.write(utf8.getStr(), utf8.getLength(), n);
                f.close();
            }
        }
    }
    openDeck(u".uno:SidebarDeck.AIChatDeck"_ustr);
    if (m_xStats)
        m_xStats->set_label(u"已打开 AI · 中台上下文已注入"_ustr);
}

IMPL_LINK_NOARG(WorkPendantController, OnChart, weld::Button&, void)
{
    const OUString path = WorkTelemetryStore::exportReport(u"week"_ustr);
    if (path.isEmpty())
        return;
    // Prefer HTML (embedded SVG) for browser view
    const OUString html = WorkTelemetryStore::htmlPathForReport(path);
    if (!openLocalPath(html))
    {
        OUString svg = path;
        if (svg.endsWith(u".md"_ustr))
            svg = svg.copy(0, svg.getLength() - 3) + u".svg"_ustr;
        openLocalPath(svg);
    }
    RefreshStats();
}

IMPL_LINK_NOARG(WorkPendantController, OnReports, weld::Button&, void)
{
    const OUString dir = WorkTelemetryStore::rootDir() + u"/reports"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) == osl::FileBase::E_None)
        osl::Directory::createPath(url);
    openLocalPath(dir);
}

IMPL_LINK_NOARG(WorkPendantController, OnPack, weld::Button&, void)
{
    const OUString path = WorkTelemetryStore::exportShareZip(u"week"_ustr);
    if (path.isEmpty())
        return;
    if (path.endsWith(u".zip"_ustr))
    {
        const sal_Int32 slash = path.lastIndexOf(u'/');
        if (slash > 0)
            openLocalPath(path.copy(0, slash));
        openLocalPath(path);
    }
    else
        openLocalPath(path);
    RefreshStats();
}

IMPL_LINK_NOARG(WorkPendantController, OnMail, weld::Button&, void)
{
    const OUString path = WorkTelemetryStore::exportShareZip(u"week"_ustr);
    if (path.isEmpty())
        return;
    const OUString body = WorkTelemetryStore::formatWeekEmailBody(path);
    if (path.endsWith(u".zip"_ustr))
    {
        const sal_Int32 slash = path.lastIndexOf(u'/');
        if (slash > 0)
            openLocalPath(path.copy(0, slash));
        openLocalPath(path);
    }
    else
        openLocalPath(path);
    openMailto(u"可圈 工作中台周报"_ustr, body);
    RefreshStats();
}

IMPL_LINK_NOARG(WorkPendantController, OnDoneWeek, weld::Button&, void)
{
    {
        weld::Widget* pParent = m_xDialog ? static_cast<weld::Widget*>(m_xDialog.get()) : nullptr;
        std::unique_ptr<weld::MessageDialog> xDlg(Application::CreateMessageDialog(
            pParent, VclMessageType::Question, VclButtonsType::YesNo,
            u"将导出本周 ZIP/HTML 并打开邮件草稿。确认完成本周？"_ustr));
        if (xDlg->run() != RET_YES)
        {
            if (m_xStats)
                m_xStats->set_label(u"已取消完成本周"_ustr);
            return;
        }
    }
    if (m_xStats)
        m_xStats->set_label(u"正在完成本周归档…"_ustr);
    const OUString zipOrFolder = WorkTelemetryStore::exportShareZip(u"week"_ustr);
    if (zipOrFolder.isEmpty())
    {
        if (m_xStats)
            m_xStats->set_label(u"完成本周失败"_ustr);
        return;
    }
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
    copyToClipboard(text);
    if (!openLocalPath(weekHtml))
        openLocalPath(zipOrFolder);
    openMailto(u"可圈 本周工作归档"_ustr, mailBody);
    if (m_xStats)
        m_xStats->set_label(u"✓ 本周已完成 · 邮件草稿\n"_ustr + zipOrFolder);
    RefreshStats();
}

IMPL_LINK_NOARG(WorkPendantController, OnWow, weld::Button&, void)
{
    const OUString path = WorkTelemetryStore::exportWeekOverWeekSvg();
    if (path.isEmpty())
    {
        if (m_xStats)
            m_xStats->set_label(u"环比图导出失败"_ustr);
        return;
    }
    openLocalPath(path);
    if (m_xStats)
        m_xStats->set_label(u"环比图\n"_ustr + path);
    RefreshStats();
}

IMPL_LINK_NOARG(WorkPendantController, OnTick, Timer*, void) { RefreshStats(); }

} // namespace

WorkPendantDispatcher& WorkPendantDispatcher::Get()
{
    static WorkPendantDispatcher a;
    return a;
}

void WorkPendantDispatcher::Show(weld::Widget* pParent)
{
    if (g_pPendant)
    {
        g_pPendant->RefreshStats();
        return;
    }
    if (!pParent)
        pParent = Application::GetDefDialogParent();
    g_pPendant = std::make_shared<WorkPendantController>(pParent);
    weld::DialogController::runAsync(g_pPendant, [](sal_Int32) { g_pPendant.reset(); });
}

void WorkPendantDispatcher::Hide()
{
    if (g_pPendant)
    {
        g_pPendant->response(RET_CANCEL);
        g_pPendant.reset();
    }
}

bool WorkPendantDispatcher::IsVisible() const { return static_cast<bool>(g_pPendant); }

} // namespace sfx2

extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_work_show_pendant()
{
    sfx2::WorkPendantDispatcher::Get().Show(Application::GetDefDialogParent());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
