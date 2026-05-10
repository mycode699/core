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
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
