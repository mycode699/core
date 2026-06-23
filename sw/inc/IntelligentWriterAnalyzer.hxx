/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "swdllapi.h"

#include <rtl/ustring.hxx>
#include <vector>

class SwDoc;
class SwDocShell;

namespace sw::intelligent
{
struct DiagnosticAction
{
    OUString maId;
    OUString maLabelZh;
    OUString maMode;
};

struct DiagnosticLocation
{
    OUString maKind;
    OUString maLabelZh;
    OUString maPath;
};

struct DiagnosticEvidence
{
    OUString maSource;
    OUString maPath;
    OUString maSummaryZh;
};

struct Diagnostic
{
    OUString maId;
    OUString maModule;
    OUString maSeverity;
    OUString maTitleZh;
    OUString maMessageZh;
    DiagnosticLocation maLocation;
    std::vector<DiagnosticAction> maActions;
    DiagnosticEvidence maEvidence;
};

SW_DLLPUBLIC std::vector<Diagnostic> AnalyzeWriterDocumentPreview(const SwDoc& rDoc);

// Forward-declared in IntelligentWriterApplyEngine.hxx; bridge helper exposed
// here so callers that already include IntelligentWriterAnalyzer.hxx can reach
// the apply engine without a second include in cold paths.
struct ApplyPlan;
struct ApplyResult;
SW_DLLPUBLIC ApplyResult runApply(SwDocShell& rDocShell, const ApplyPlan& rPlan);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
