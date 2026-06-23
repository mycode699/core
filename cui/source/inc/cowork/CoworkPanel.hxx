/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5: Async Cowork Task Manager UI).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-1 entry point for the cowork task manager dialog. Open via
 * `.uno:CoworkTaskManager` (Help menu / Cmd+Shift+T) or ShowCoworkDialog.
 * Spec: docs/product/v2/w5-async-cowork-spec.md §"Desktop Task Manager".
 */

#ifndef INCLUDED_CUI_SOURCE_INC_COWORK_COWORKPANEL_HXX
#define INCLUDED_CUI_SOURCE_INC_COWORK_COWORKPANEL_HXX

#include <vcl/weld/weld.hxx>

/// Open the async task manager dialog (modal). Day-1 stub: lists tasks
/// from kqoffice::ai::cowork::TaskStore for the current UTC month and
/// supports creating a pending stub envelope via「新建任务」.
SAL_DLLPUBLIC_EXPORT void ShowCoworkDialog(weld::Widget* pParent);

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */