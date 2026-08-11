/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: status dashboard).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * One markdown surface for /工作台状态 — phase, queue, writeback, resource, first-run.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_WORKBENCHSTATUS_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_WORKBENCHSTATUS_HXX

#include "WorkbenchPhase.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::control
{

struct WorkbenchStatusInput
{
    WorkbenchPhase phase = WorkbenchPhase::Idle;
    OUString workPlanId;
    OUString applyPlanId;
    sal_Int32 queueSize = 0;
    OUString queueStatusZh;
    bool streamOpen = false;
    bool toolsOpen = false;
    OUString surface;
    OUString membershipChipZh;
    OUString contextChipZh;
};

struct WorkbenchStatusReport
{
    OUString markdownZh;
    OUString chipZh; ///< one-line status bar
    bool readyForPrimaryInput = true;
};

class SAL_DLLPUBLIC_EXPORT WorkbenchStatus
{
public:
    static WorkbenchStatusReport build(const WorkbenchStatusInput& in);

    /// Resource envelope + writeback + first-run block (no UI state needed).
    static OUString staticPoliciesMarkdownZh();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
