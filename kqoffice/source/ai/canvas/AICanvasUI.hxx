/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AICanvasUI — display formatting helpers for the canvas workflow.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CANVAS_AICANVASUI_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CANVAS_AICANVASUI_HXX

#include "AICanvasMode.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::canvas
{

/// Formatted display data for the canvas UI.
struct CanvasUIDisplay
{
    OUString stepLabel;         // "Step 3/7"
    OUString progressBar;       // "[████░░░░░░░░░░░░░░░░] 43%"
    OUString statusText;        // "请确认"
    OUString contentPreview;    // First 200 chars of generated content
    OUString nextActionHint;    // "确认以继续，或描述修改内容"
};

class SAL_DLLPUBLIC_EXPORT AICanvasUI
{
public:
    /// Build the display data for the current canvas session.
    static CanvasUIDisplay buildDisplay(const CanvasSession& session);

    /// Format a single step as a summary line.
    static OUString formatStepSummary(const CanvasStep& step);

    /// Format a progress bar: "[████░░░░░░░░░░░░░░░░] 43%"
    /// @param fraction 0.0 - 1.0
    /// @param width Bar width in characters (default 20)
    static OUString formatProgressBar(double fraction, sal_Int32 width = 20);
};

} // namespace kqoffice::ai::canvas

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
