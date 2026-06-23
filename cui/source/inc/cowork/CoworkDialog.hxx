/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5: Async Cowork Task Manager UI).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>
#include <vcl/timer.hxx>
#include <vcl/weld/DialogController.hxx>
#include <vcl/weld/weld.hxx>

#include <memory>
#include <vector>

namespace kqoffice::ai::cowork
{
class CoworkUiTaskBridgeJob;
class TaskReviewOpenSink;
class TaskNativeOsNotificationClickSink;
}

class CoworkDialog final : public weld::GenericDialogController
{
private:
    std::unique_ptr<weld::TreeView> m_xTaskList;
    std::unique_ptr<weld::Label> m_xStatusLabel;
    std::unique_ptr<weld::Button> m_xNewTaskButton;
    std::unique_ptr<weld::Button> m_xAcceptTaskButton;
    weld::Widget* m_pDiffReviewParent;
    std::shared_ptr<kqoffice::ai::cowork::TaskNativeOsNotificationClickSink> m_xNativeClickSink;
    std::unique_ptr<kqoffice::ai::cowork::TaskReviewOpenSink> m_xReviewOpenSink;
    std::unique_ptr<kqoffice::ai::cowork::CoworkUiTaskBridgeJob> m_xTaskJob;
    AutoTimer m_aTaskPollTimer;

    OUString m_aMonthDir;
    OUString m_aSelectedTaskId;
    OUString m_aActiveTaskId;

    DECL_LINK(OnNewTask, weld::Button&, void);
    DECL_LINK(OnAcceptTask, weld::Button&, void);
    DECL_LINK(OnSelectionChanged, weld::TreeView&, void);
    DECL_LINK(OnTaskPoll, Timer*, void);

    void refreshTaskList(const OUString& rPreferredTaskId = OUString());
    void updateActionButtons();
    void updateStatusLabel(std::size_t nCount);
    OUString selectedTaskId() const;
    static OUString currentMonthDir();
    static OUString currentIsoUtcTimestamp();
    static OUString nextStubTaskId(const OUString& rMonthDir,
                                   const std::vector<OUString>& rExistingIds);

public:
    explicit CoworkDialog(weld::Widget* pParent);
    ~CoworkDialog() override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
