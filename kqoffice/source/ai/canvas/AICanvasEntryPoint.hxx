/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AICanvasEntryPoint — UI entry for launching canvas mode.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CANVAS_AICANVASENTRYPOINT_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CANVAS_AICANVASENTRYPOINT_HXX

#include "AICanvasMode.hxx"

#include <rtl/ustring.hxx>

namespace kqoffice::ai::canvas
{

/// Entry point for launching canvas mode from the UI.
class SAL_DLLPUBLIC_EXPORT AICanvasEntryPoint
{
public:
    /// Launch canvas mode for creating a new document.
    /// Opens the AI chat panel in canvas workflow mode.
    static bool launchCanvasMode(CanvasDocType docType);

    /// Launch canvas mode for enhancing an existing document.
    static bool launchCanvasModeForExisting(const OUString& documentUrl);

    /// Check if canvas mode is available for the given document type.
    static bool isAvailable(CanvasDocType docType);

    /// Get the localized display name for canvas mode.
    static OUString getDisplayName(CanvasDocType docType);

    /// Get a short description for the new document dialog.
    static OUString getDescription(CanvasDocType docType);
};

} // namespace kqoffice::ai::canvas

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
