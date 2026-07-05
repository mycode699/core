# -*- tab-width: 4; indent-tabs-mode: nil; py-indent-offset: 4 -*-
#
# V2 W5 — Cowork dialog UITest (headless svp / 可圈office product build).
# Proves users can open the task manager, see the task list, create a task,
# and reach the accept affordance after the stub worker completes.

import time

from uitest.framework import UITestCase
from uitest.uihelper.common import get_state_as_dict


class CoworkDialog(UITestCase):

    def _open_cowork_dialog(self):
        return self.ui_test.execute_dialog_through_command(
            ".uno:CoworkTaskManager", close_button="cancel")

    def test_a_cowork_dialog_controls_smoke(self):
        with self.ui_test.load_empty_file("writer"), self._open_cowork_dialog() as xDialog:
            self.assertIsNotNone(xDialog.getChild("btn_new_task"))
            self.assertIsNotNone(xDialog.getChild("btn_accept_task"))
            self.assertIsNotNone(xDialog.getChild("task_list_view"))
            self.assertIsNotNone(xDialog.getChild("status_label"))

    def test_b_cowork_new_task_visible_in_list(self):
        sleep_s = self.ui_test.get_default_sleep()
        with self.ui_test.load_empty_file("writer"), self._open_cowork_dialog() as xDialog:
            xNew = xDialog.getChild("btn_new_task")
            xList = xDialog.getChild("task_list_view")
            xNew.executeAction("CLICK", tuple())

            for _ in range(40):
                state = get_state_as_dict(xList)
                if state.get("Children", "0") != "0":
                    break
                time.sleep(sleep_s)
            self.assertNotEqual(get_state_as_dict(xList).get("Children", "0"), "0")

    def test_c_cowork_accept_task_enabled_after_review(self):
        sleep_s = self.ui_test.get_default_sleep()
        with self.ui_test.load_empty_file("writer"), self._open_cowork_dialog() as xDialog:
            xNew = xDialog.getChild("btn_new_task")
            xAccept = xDialog.getChild("btn_accept_task")

            xNew.executeAction("CLICK", tuple())

            for _ in range(40):
                if get_state_as_dict(xAccept).get("Enabled", "false") == "true":
                    break
                time.sleep(sleep_s)

            self.assertEqual(
                get_state_as_dict(xAccept).get("Enabled", "false"), "true")
            xAccept.executeAction("CLICK", tuple())


# vim: set shiftwidth=4 softtabstop=4 expandtab: