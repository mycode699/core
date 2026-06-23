/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1/M3: evidence inspector runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatContentRegistry.hxx"

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatEvidenceInspectionResult
{
    bool Success = false;
    OUString SourceId;
    OUString SourceType;
    OUString CitationId;
    OUString EvidenceId;
    OUString HashReference;
    OUString OpenTarget;
    OUString AuditTrail;
    OUString Summary;
};

class AIChatEvidenceInspector final
{
public:
    AIChatEvidenceInspectionResult Inspect(const AIChatContentRegistryEntry& rEntry) const;

    static bool IsSupportedSourceType(const OUString& rSourceType);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
