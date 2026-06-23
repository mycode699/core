/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5: Async Cowork Task Manager UI).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-1 bridge: opens the weld dialog, lists TaskStore envelopes for the
 * current UTC month, and runs one deterministic worker task from「新建任务」
 * through TaskRunner -> awaiting-review -> diff-review-opened evidence.
 */

#include <sal/config.h>

#include <cowork/CoworkDialog.hxx>
#include <cowork/CoworkPanel.hxx>
#include <dispatch/CoworkPanelDispatcher.hxx>

#include "AsyncTask.hxx"
#include "CoworkUiBridge.hxx"
#include "TaskNativeOsNotificationBackend.hxx"
#include "TaskReviewBridge.hxx"
#include "TaskRunner.hxx"
#include "TaskStore.hxx"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <set>
#include <vector>

#include <osl/time.h>
#include <sal/log.hxx>
#include <sfx2/viewsh.hxx>
#include <svx/sidebar/DiffReviewPanel.hxx>
#include <vcl/window.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Dialog.hxx>
#include <vcl/weld/weldutils.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>

using namespace kqoffice::ai::cowork;

namespace
{
std::vector<OUString> collectTaskIdsForMonth(TaskStore& rStore,
                                             const OUString& rMonthDir)
{
    const TaskState states[] = {
        TaskState::Pending,
        TaskState::Running,
        TaskState::AwaitingReview,
        TaskState::Applied,
        TaskState::Failed,
        TaskState::Cancelled,
    };

    std::set<OUString> seen;
    std::vector<OUString> ids;
    for (TaskState st : states)
    {
        for (const OUString& id : rStore.listByState(rMonthDir, st))
        {
            if (seen.insert(id).second)
                ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

OUString formatTaskRow(const AsyncTaskEnvelope& env)
{
    OUString aTitle = env.title.isEmpty() ? env.taskId : env.title;
    return aTitle + " [" + taskStateToken(env.state) + "]";
}

std::vector<svx::sidebar::diff_review::DiffReviewPatchEntry> buildCoworkReviewEntries(
    const TaskReviewOpenResult& result)
{
    svx::sidebar::diff_review::DiffReviewPatchEntry entry;
    entry.maPatchId = result.evidenceId.isEmpty() ? result.taskId : result.evidenceId;
    entry.maKind = u"cowork-task"_ustr;
    entry.maStatus = u"awaiting-review"_ustr;
    entry.mbApplied = false;
    return { entry };
}

weld::Widget* currentDocumentDiffReviewParent()
{
    SfxViewShell* pViewShell = SfxViewShell::Current();
    if (!pViewShell)
        return nullptr;

    vcl::Window* pWin = pViewShell->GetWindow();
    if (!pWin)
        return nullptr;

    tools::Rectangle aRect;
    if (weld::Window* pPopupParent = weld::GetPopupParent(*pWin, aRect))
        return pPopupParent;
    return pWin->GetFrameWeld();
}

class CoworkDialogReviewOpenSink final : public TaskReviewOpenSink
{
public:
    explicit CoworkDialogReviewOpenSink(weld::Widget* pParent)
        : m_pParent(pParent)
    {
    }

    void openDiffReview(const TaskReviewOpenResult& result) override
    {
        m_lastResult = result;
        ++m_openCount;

        if (!result.opened || !m_pParent)
            return;

        svx::sidebar::diff_review::ShowDiffReviewPanel(
            m_pParent, result.resultPlanId, buildCoworkReviewEntries(result));

        SAL_INFO("cui.cowork", "CoworkDialogReviewOpenSink showed DiffReview task_id="
                                   << result.taskId << " plan=" << result.resultPlanId
                                   << " evidence=" << result.evidenceId);
    }

    const TaskReviewOpenResult& lastResult() const { return m_lastResult; }
    sal_Int32 openCount() const { return m_openCount; }

private:
    weld::Widget* m_pParent;
    TaskReviewOpenResult m_lastResult;
    sal_Int32 m_openCount = 0;
};

class CoworkDialogNativeClickSink final : public TaskNativeOsNotificationClickSink
{
public:
    explicit CoworkDialogNativeClickSink(weld::Widget* pParent)
        : m_pParent(pParent)
    {
    }

    void handleNativeNotificationClick(
        const TaskNativeOsNotificationClickPayload& payload) override
    {
        TaskStore store;
        CoworkDialogReviewOpenSink openSink(m_pParent);
            TaskReviewOpenResult result;
            const bool bOpened
                = openReviewFromNativeOsNotificationClick(payload, store, openSink, &result);
            recordNativeOsNotificationReviewOpenEvidence(payload, result, bOpened);
            if (!bOpened)
            {
                SAL_INFO("cui.cowork", "Native notification click ignored task_id="
                                          << payload.taskId << " reason="
                                      << result.failureReason);
            return;
        }

        SAL_INFO("cui.cowork", "Native notification click opened review task_id="
                                  << result.taskId << " plan=" << result.resultPlanId
                                  << " evidence=" << result.evidenceId);
    }

private:
    weld::Widget* m_pParent;
};
} // namespace

CoworkDialog::CoworkDialog(weld::Widget* pParent)
    : GenericDialogController(pParent, u"cui/ui/cowork-dialog.ui"_ustr, u"CoworkDialog"_ustr)
    , m_xTaskList(m_xBuilder->weld_tree_view(u"task_list_view"_ustr))
    , m_xStatusLabel(m_xBuilder->weld_label(u"status_label"_ustr))
    , m_xNewTaskButton(m_xBuilder->weld_button(u"btn_new_task"_ustr))
    , m_xAcceptTaskButton(m_xBuilder->weld_button(u"btn_accept_task"_ustr))
    , m_pDiffReviewParent(currentDocumentDiffReviewParent())
    , m_aTaskPollTimer("CoworkDialogTaskPoll")
    , m_aMonthDir(currentMonthDir())
{
    if (!m_pDiffReviewParent)
        m_pDiffReviewParent = pParent;
    m_xReviewOpenSink = std::make_unique<CoworkDialogReviewOpenSink>(m_pDiffReviewParent);
    m_xNewTaskButton->connect_clicked(LINK(this, CoworkDialog, OnNewTask));
    m_xAcceptTaskButton->connect_clicked(LINK(this, CoworkDialog, OnAcceptTask));
    m_xTaskList->connect_selection_changed(LINK(this, CoworkDialog, OnSelectionChanged));
    m_aTaskPollTimer.SetTimeout(100);
    m_aTaskPollTimer.SetInvokeHandler(LINK(this, CoworkDialog, OnTaskPoll));
    m_xNativeClickSink
        = std::make_shared<CoworkDialogNativeClickSink>(m_pDiffReviewParent);
    setTaskNativeOsNotificationClickSink(m_xNativeClickSink);
    refreshTaskList();
}

CoworkDialog::~CoworkDialog()
{
    m_aTaskPollTimer.Stop();
    if (m_xTaskJob)
        m_xTaskJob->join();
    clearTaskNativeOsNotificationClickSink(m_xNativeClickSink.get());
}

OUString CoworkDialog::currentMonthDir()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    std::time_t secs = static_cast<std::time_t>(tv.Seconds);
    std::tm utc{};
    gmtime_r(&secs, &utc);
    char ym[8];
    std::snprintf(ym, sizeof(ym), "%04d-%02d", utc.tm_year + 1900, utc.tm_mon + 1);
    return OUString::createFromAscii(ym);
}

OUString CoworkDialog::currentIsoUtcTimestamp()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    std::time_t secs = static_cast<std::time_t>(tv.Seconds);
    std::tm utc{};
    gmtime_r(&secs, &utc);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return OUString::createFromAscii(buf);
}

OUString CoworkDialog::nextStubTaskId(const OUString& /*rMonthDir*/,
                                      const std::vector<OUString>& rExistingIds)
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    std::time_t secs = static_cast<std::time_t>(tv.Seconds);
    std::tm utc{};
    gmtime_r(&secs, &utc);
    char dateToken[16];
    std::snprintf(dateToken, sizeof(dateToken), "%04d%02d%02d",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);

    sal_Int32 maxSeq = 0;
    const OUString prefix
        = OUString::createFromAscii("tk-") + OUString::createFromAscii(dateToken) + "-";
    for (const OUString& id : rExistingIds)
    {
        if (!id.startsWith(prefix))
            continue;
        const sal_Int32 seq = id.copy(prefix.getLength()).toInt32();
        if (seq > maxSeq)
            maxSeq = seq;
    }

    char taskId[32];
    std::snprintf(taskId, sizeof(taskId), "tk-%s-%03d", dateToken,
                  static_cast<int>(maxSeq + 1));
    return OUString::createFromAscii(taskId);
}

void CoworkDialog::updateStatusLabel(std::size_t nCount)
{
    m_xStatusLabel->set_label(
        "任务列表（" + m_aMonthDir + "）：" + OUString::number(nCount) + " 项");
}

void CoworkDialog::refreshTaskList(const OUString& rPreferredTaskId)
{
    TaskStore store;
    const std::vector<OUString> ids = collectTaskIdsForMonth(store, m_aMonthDir);
    const OUString aTaskIdToSelect
        = rPreferredTaskId.isEmpty() ? m_aSelectedTaskId : rPreferredTaskId;

    m_xTaskList->clear();
    std::size_t shown = 0;
    int nPreferredRow = -1;
    for (const OUString& id : ids)
    {
        AsyncTaskEnvelope env;
        if (!store.read(m_aMonthDir, id, env))
            continue;
        m_xTaskList->append(id, formatTaskRow(env));
        if (id == aTaskIdToSelect)
            nPreferredRow = static_cast<int>(shown);
        ++shown;
    }

    if (shown > 0)
    {
        const int nRow = nPreferredRow >= 0 ? nPreferredRow : 0;
        m_xTaskList->select(nRow);
        m_aSelectedTaskId = m_xTaskList->get_id(nRow);
    }
    else
        m_aSelectedTaskId.clear();

    updateStatusLabel(shown);
    updateActionButtons();
    SAL_INFO("cui.cowork", "refreshTaskList month=" << m_aMonthDir << " count=" << shown
                                                    << " selected=" << m_aSelectedTaskId
                                                    << " root=" << TaskStore::resolveRootDir());
}

OUString CoworkDialog::selectedTaskId() const
{
    const OUString aSelectedId = m_xTaskList->get_selected_id();
    return aSelectedId.isEmpty() ? m_aSelectedTaskId : aSelectedId;
}

void CoworkDialog::updateActionButtons()
{
    TaskStore store;
    AsyncTaskEnvelope env;
    const OUString taskId = selectedTaskId();
    const bool readOk = !taskId.isEmpty() && store.read(m_aMonthDir, taskId, env);
    const bool canAccept = readOk && env.state == TaskState::AwaitingReview
                           && !env.resultPlanId.isEmpty();
    m_xAcceptTaskButton->set_sensitive(canAccept);
    SAL_INFO("cui.cowork", "updateActionButtons task_id=" << taskId
                                                           << " read="
                                                           << (readOk ? "true" : "false")
                                                           << " state="
                                                           << (readOk ? taskStateToken(env.state)
                                                                      : OUString())
                                                           << " plan="
                                                           << (readOk ? env.resultPlanId
                                                                      : OUString())
                                                           << " can_accept="
                                                           << (canAccept ? "true" : "false"));
}

IMPL_LINK_NOARG(CoworkDialog, OnNewTask, weld::Button&, void)
{
    if (m_xTaskJob && !m_xTaskJob->isDone())
    {
        SAL_INFO("cui.cowork", "OnNewTask ignored while task is active task_id="
                                   << m_aActiveTaskId);
        return;
    }
    if (m_xTaskJob)
    {
        m_xTaskJob->join();
        m_xTaskJob.reset();
    }

    TaskStore store;
    const std::vector<OUString> ids = collectTaskIdsForMonth(store, m_aMonthDir);
    const OUString aNow = currentIsoUtcTimestamp();

    AsyncTaskEnvelope env;
    env.taskId = nextStubTaskId(m_aMonthDir, ids);
    env.kind = TaskKind::WeeklyReport;
    env.state = TaskState::Pending;
    env.title = u"新任务（等待审批）"_ustr;
    env.createdAt = aNow;
    env.updatedAt = aNow;
    env.serviceMode = u"offline"_ustr;
    env.userPrompt = u"从异步任务面板启动"_ustr;
    env.schemaVersion = 1;

    m_xTaskJob = std::make_unique<CoworkUiTaskBridgeJob>(m_aMonthDir, env,
                                                         *m_xReviewOpenSink);
    if (!m_xTaskJob->prepare())
    {
        SAL_INFO("cui.cowork", "OnNewTask prepare failed task_id=" << env.taskId);
        m_xTaskJob.reset();
        return;
    }

    m_aActiveTaskId = env.taskId;
    m_xNewTaskButton->set_sensitive(false);
    refreshTaskList(m_aActiveTaskId);
    m_aTaskPollTimer.Start();
    SAL_INFO("cui.cowork", "OnNewTask prepared pending task_id=" << m_aActiveTaskId);
}

IMPL_LINK_NOARG(CoworkDialog, OnTaskPoll, Timer*, void)
{
    if (!m_xTaskJob)
    {
        m_aTaskPollTimer.Stop();
        return;
    }

    if (!m_xTaskJob->isStarted())
    {
        if (!m_xTaskJob->start())
        {
            SAL_INFO("cui.cowork", "OnTaskPoll failed to start task_id="
                                       << m_aActiveTaskId);
            m_xNewTaskButton->set_sensitive(true);
            m_aTaskPollTimer.Stop();
            m_xTaskJob.reset();
            m_aActiveTaskId.clear();
            return;
        }
        SAL_INFO("cui.cowork", "OnTaskPoll started worker task_id=" << m_aActiveTaskId);
    }

    refreshTaskList(m_aActiveTaskId);
    if (!m_xTaskJob->isDone())
        return;

    m_xTaskJob->join();
    const CoworkUiBridgeResult aResult = m_xTaskJob->result();
    SAL_INFO("cui.cowork", "OnTaskPoll completed task_id=" << aResult.taskId
                                                             << " state="
                                                             << taskStateToken(aResult.finalState)
                                                             << " plan="
                                                             << aResult.resultPlanId
                                                             << " evidence="
                                                             << aResult.evidenceId
                                                             << " notifications="
                                                             << aResult.notificationCount
                                                             << " os_notifications="
                                                             << aResult.osNotificationPostedCount);
    refreshTaskList(m_aActiveTaskId);
    m_xNewTaskButton->set_sensitive(true);
    m_aTaskPollTimer.Stop();
    m_xTaskJob.reset();
    m_aActiveTaskId.clear();
}

IMPL_LINK_NOARG(CoworkDialog, OnAcceptTask, weld::Button&, void)
{
    TaskStore store;
    AsyncTaskEnvelope env;
    const OUString taskId = selectedTaskId();
    if (taskId.isEmpty() || !store.read(m_aMonthDir, taskId, env))
    {
        SAL_INFO("cui.cowork", "OnAcceptTask skipped: no selected task");
        updateActionButtons();
        return;
    }

    TaskReviewOpenResult openResult;
    openResult.opened = true;
    openResult.actionToken = taskReviewOpenedToken();
    openResult.sourceToken = u"cowork-dialog-selected-review"_ustr;
    openResult.monthDir = m_aMonthDir;
    openResult.taskId = taskId;
    openResult.resultPlanId = env.resultPlanId;
    openResult.evidenceId = env.evidenceIds.empty() ? OUString() : env.evidenceIds.back();

    TaskReviewAcceptResult acceptResult;
    if (!acceptReviewResult(openResult, store, &acceptResult))
    {
        SAL_INFO("cui.cowork", "OnAcceptTask failed task_id=" << taskId
                                                              << " reason="
                                                              << acceptResult.failureReason);
        updateActionButtons();
        return;
    }

    SAL_INFO("cui.cowork", "OnAcceptTask applied task_id=" << acceptResult.taskId
                                                           << " plan="
                                                           << acceptResult.resultPlanId
                                                           << " evidence="
                                                           << acceptResult.evidenceId);
    refreshTaskList(taskId);
}

IMPL_LINK_NOARG(CoworkDialog, OnSelectionChanged, weld::TreeView&, void)
{
    const OUString aSelectedTaskId = m_xTaskList->get_selected_id();
    if (!aSelectedTaskId.isEmpty() || m_xTaskList->n_children() == 0)
        m_aSelectedTaskId = aSelectedTaskId;
    updateActionButtons();
}

void ShowCoworkDialog(weld::Widget* pParent)
{
    CoworkDialog aDlg(pParent);
    aDlg.run();
}

namespace
{
struct CoworkPanelHookRegistrar
{
    CoworkPanelHookRegistrar()
    {
        sfx2::CoworkPanelDispatcher::RegisterShowPanelHook(&ShowCoworkDialog);
    }
};
const CoworkPanelHookRegistrar g_aCoworkPanelHookRegistrar;
} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
