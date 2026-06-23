# -*- tab-width: 4; indent-tabs-mode: nil; py-indent-offset: 4 -*-
#
# W4 Day-6 — UITest smoke (headless svp / 可圈office product build).
# LO_RUNNING_UI_TEST disables Select-to-Act hooks in DrawSelChanged + controller.
# Cover both the direct UNO factory path and Start Center button path, since
# the latter exercises UITest CLICK dispatch while a new Writer frame is built.

from uitest.framework import UITestCase


class SelectToAct(UITestCase):

    def test_office_connect_smoke(self):
        """Harness connected to soffice with LO_RUNNING_UI_TEST set."""

        self.assertIsNotNone(self.xContext)
        smgr = self.xContext.ServiceManager
        self.assertIsNotNone(smgr.createInstanceWithContext(
            "com.sun.star.frame.Desktop", self.xContext))

    def test_writer_uno_factory_smoke(self):
        with self.ui_test.load_empty_file("writer") as doc:
            self.assertTrue(doc.supportsService("com.sun.star.text.TextDocument"))

    def test_writer_edit_reachable_smoke(self):
        with self.ui_test.load_empty_file("writer"):
            self.assertIsNotNone(
                self.xUITest.getTopFocusWindow().getChild("writer_edit"))

    def test_writer_start_center_create_smoke(self):
        with self.ui_test.create_doc_in_start_center("writer") as doc:
            self.assertTrue(doc.supportsService("com.sun.star.text.TextDocument"))
            self.assertIsNotNone(
                self.xUITest.getTopFocusWindow().getChild("writer_edit"))


# vim: set shiftwidth=4 softtabstop=4 expandtab:
