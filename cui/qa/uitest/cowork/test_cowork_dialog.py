# -*- tab-width: 4; indent-tabs-mode: nil; py-indent-offset: 4 -*-
#
# V2 W5 — Cowork dialog UITest (headless svp / 可圈office product build).
# Proves users can open the task manager, see the task list, create a task,
# and reach the accept affordance after the stub worker completes.

import time

from com.sun.star.awt import Toolkit

from uitest.framework import UITestCase
from uitest.uihelper.common import get_state_as_dict


class CoworkDialog(UITestCase):

    def _open_cowork_dialog(self):
        # svp/headless: cancel via UITest does not always emit DialogClosed;
        # close manually instead of execute_dialog_through_command's waiter.
        return self.ui_test.execute_dialog_through_command(
            ".uno:CoworkTaskManager", close_button="")

    def _close_cowork_dialog(self, xDialog):
        xDialog.getChild("cancel").executeAction("CLICK", tuple())
        Toolkit.create(self.xContext).waitUntilAllIdlesDispatched()

    def test_a_cowork_dialog_controls_smoke(self):
        with self.ui_test.create_doc_in_start_center("writer"), self._open_cowork_dialog() as xDialog:
            self.assertIsNotNone(xDialog.getChild("btn_new_task"))
            self.assertIsNotNone(xDialog.getChild("btn_accept_task"))
            self.assertIsNotNone(xDialog.getChild("task_list_view"))
            self.assertIsNotNone(xDialog.getChild("status_label"))
            self._close_cowork_dialog(xDialog)

    def test_b_cowork_new_task_visible_in_list(self):
        sleep_s = self.ui_test.get_default_sleep()
        with self.ui_test.create_doc_in_start_center("writer"), self._open_cowork_dialog() as xDialog:
            xNew = xDialog.getChild("btn_new_task")
            xList = xDialog.getChild("task_list_view")
            xNew.executeAction("CLICK", tuple())

            for _ in range(40):
                state = get_state_as_dict(xList)
                if state.get("Children", "0") != "0":
                    break
                time.sleep(sleep_s)
            self.assertNotEqual(get_state_as_dict(xList).get("Children", "0"), "0")
            self._close_cowork_dialog(xDialog)

    def test_c_cowork_accept_task_enabled_after_review(self):
        sleep_s = self.ui_test.get_default_sleep()
        with self.ui_test.create_doc_in_start_center("writer"), self._open_cowork_dialog() as xDialog:
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
            self._close_cowork_dialog(xDialog)


# vim: set shiftwidth=4 softtabstop=4 expandtab: