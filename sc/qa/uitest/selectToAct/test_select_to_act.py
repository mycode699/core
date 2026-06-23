# -*- tab-width: 4; indent-tabs-mode: nil; py-indent-offset: 4 -*-
#
# W4 Select-to-act UITest smoke for Calc.
# Cover the Calc document window through UITest.load_empty_file(), which
# avoids the Start Center creation path used by broader UI suites.

from uitest.framework import UITestCase


class SelectToAct(UITestCase):

    def test_office_connect_smoke(self):
        """Harness connected to soffice with LO_RUNNING_UI_TEST set."""

        self.assertIsNotNone(self.xContext)
        smgr = self.xContext.ServiceManager
        self.assertIsNotNone(smgr.createInstanceWithContext(
            "com.sun.star.frame.Desktop", self.xContext))

    def test_calc_uno_factory_smoke(self):
        with self.ui_test.load_empty_file("calc") as doc:
            self.assertTrue(doc.supportsService(
                "com.sun.star.sheet.SpreadsheetDocument"))

    def test_calc_grid_window_reachable_smoke(self):
        with self.ui_test.load_empty_file("calc"):
            self.assertIsNotNone(
                self.xUITest.getTopFocusWindow().getChild("grid_window"))


# vim: set shiftwidth=4 softtabstop=4 expandtab:
