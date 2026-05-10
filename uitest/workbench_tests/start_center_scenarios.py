# -*- tab-width: 4; indent-tabs-mode: nil; py-indent-offset: 4 -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

import time
from contextlib import contextmanager

from uitest.framework import UITestCase
from uitest.uihelper.common import get_state_as_dict


class StartCenterScenarioSmokeTest(UITestCase):
    SCENARIO_BUTTONS = (
        "scenario_report",
        "scenario_minutes",
        "scenario_notice",
        "scenario_plan",
        "scenario_outline",
        "scenario_budget",
        "scenario_sales",
        "scenario_schedule",
        "scenario_pitch",
        "scenario_project_report",
        "scenario_courseware",
        "scenario_compat_open",
    )

    CRITICAL_START_CENTER_CONTROLS = SCENARIO_BUTTONS + (
        "open_all",
        "open_recent",
        "templates_all",
        "writer_all",
        "calc_all",
        "impress_all",
        "scrolllocal",
        "local_view",
        "help",
    )

    def dispatch_scenario(self, button_id, editor_child_id):
        xStartCenter = self.xUITest.getTopFocusWindow()
        xScenarioButton = xStartCenter.getChild(button_id)
        xScenarioButton.executeAction("CLICK", tuple())

        for _ in range(400):
            frames = self.ui_test.get_frames()
            if frames:
                self.ui_test.get_desktop().setActiveFrame(frames[0])

            xComponent = self.ui_test.get_component()
            xDocumentWindow = self.xUITest.getTopFocusWindow()
            if xComponent is not None and editor_child_id in xDocumentWindow.getChildren():
                return xComponent, xDocumentWindow
            time.sleep(self.ui_test.get_default_sleep())

        self.fail("Scenario button '%s' did not open editor '%s'" % (button_id, editor_child_id))

    @contextmanager
    def open_scenario(self, button_id, editor_child_id):
        xComponent, xDocumentWindow = self.dispatch_scenario(button_id, editor_child_id)
        try:
            yield xComponent, xDocumentWindow
        finally:
            self.ui_test.close_doc()

    def assert_scenario_opens(self, button_id, service_name, editor_child_id, template_title):
        with self.open_scenario(button_id, editor_child_id) as (xComponent, xDocumentWindow):
            self.assertTrue(xComponent.supportsService(service_name))
            self.assertEqual(xComponent.DocumentProperties.Title, template_title)
            self.assertFalse(xComponent.URL.lower().endswith((".ott", ".ots", ".otp")))
            self.assertFalse(xComponent.isModified())
            xDocumentWindow.getChild(editor_child_id)

    def test_start_center_critical_controls_are_visible_and_enabled(self):
        xStartCenter = self.xUITest.getTopFocusWindow()
        for control_id in self.CRITICAL_START_CENTER_CONTROLS:
            state = get_state_as_dict(xStartCenter.getChild(control_id))
            self.assertEqual("true", state["Visible"], "%s should be visible" % control_id)
            self.assertEqual("true", state["Enabled"], "%s should be enabled" % control_id)

    def test_open_scenario_report(self):
        self.assert_scenario_opens(
            "scenario_report", "com.sun.star.text.TextDocument", "writer_edit", "工作汇报")

    def test_open_scenario_minutes(self):
        self.assert_scenario_opens(
            "scenario_minutes", "com.sun.star.text.TextDocument", "writer_edit", "会议纪要")

    def test_open_scenario_notice(self):
        self.assert_scenario_opens(
            "scenario_notice", "com.sun.star.text.TextDocument", "writer_edit", "通知")

    def test_open_scenario_plan(self):
        self.assert_scenario_opens(
            "scenario_plan", "com.sun.star.text.TextDocument", "writer_edit", "项目方案")

    def test_open_scenario_outline(self):
        self.assert_scenario_opens(
            "scenario_outline", "com.sun.star.text.TextDocument", "writer_edit", "PPT 提纲初稿")

    def test_open_scenario_budget(self):
        self.assert_scenario_opens(
            "scenario_budget", "com.sun.star.sheet.SpreadsheetDocument", "grid_window", "预算总览")

    def test_open_scenario_sales(self):
        self.assert_scenario_opens(
            "scenario_sales", "com.sun.star.sheet.SpreadsheetDocument", "grid_window", "销售跟进")

    def test_open_scenario_schedule(self):
        self.assert_scenario_opens(
            "scenario_schedule", "com.sun.star.sheet.SpreadsheetDocument", "grid_window", "项目排期")

    def test_open_scenario_pitch(self):
        self.assert_scenario_opens(
            "scenario_pitch", "com.sun.star.presentation.PresentationDocument", "impress_win", "商务路演")

    def test_open_scenario_project_report(self):
        self.assert_scenario_opens(
            "scenario_project_report", "com.sun.star.presentation.PresentationDocument", "impress_win", "项目汇报")

    def test_open_scenario_courseware(self):
        self.assert_scenario_opens(
            "scenario_courseware", "com.sun.star.presentation.PresentationDocument", "impress_win", "教学课件")

    def test_compatibility_open_keeps_start_center_alive(self):
        xStartCenter = self.xUITest.getTopFocusWindow()
        xCompatButton = xStartCenter.getChild("scenario_compat_open")
        with self.ui_test.execute_blocking_action(xCompatButton.executeAction, args=("CLICK", tuple()), close_button="cancel"):
            pass
        self.xUITest.getTopFocusWindow().getChild("scenario_compat_open")


# vim: set shiftwidth=4 softtabstop=4 expandtab:
