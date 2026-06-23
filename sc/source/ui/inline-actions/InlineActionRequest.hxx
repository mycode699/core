/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "CellActions.hxx"
#include <rtl/ustring.hxx>
#include <scdllapi.h>

// W4 Day-2: inline-action-request envelope (docs/schemas/inline-action-request.schema.json).

namespace sc::inline_actions {

/// Builds a one-line JSON envelope for calc-cell dispatch.
SC_DLLPUBLIC OUString buildCalcCellRequest(const OUString& rActionToken, const OUString& rSheet,
                                           const OUString& rRange,
                                           const OUString& rServiceMode = u"offline"_ustr);

/// Splits ScRangeStringConverter OOO output (e.g. "Sheet1.B2:D5") into sheet + range.
SC_DLLPUBLIC bool splitCalcCellRangeA1(const OUString& rCellRangeA1, OUString& rSheet,
                                       OUString& rRange);

/// True for actions that route through Diff (not popup-only explain-data).
SC_DLLPUBLIC bool actionRoutesToDiff(CellAction eAction);

} // namespace sc::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */