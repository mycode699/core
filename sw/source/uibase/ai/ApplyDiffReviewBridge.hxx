/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>

#include <vector>

class SwViewShell;

namespace sw::intelligent
{
struct ApplyResult;
struct Patch;
}

/** W4.E Day-2: open Diff Review after a successful ApplyEngine run. */
void ShowApplyPlanDiffReview(SwViewShell& rShell, const sw::intelligent::ApplyResult& rResult,
                             const rtl::OUString& rPlanId,
                             const std::vector<sw::intelligent::Patch>* pPlanPatches = nullptr);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */