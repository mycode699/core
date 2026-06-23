/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace content opener).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "AIChatContentRegistry.hxx"
#include "AIChatPreviewMatrix.hxx"

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

struct AIChatContentOpenResult
{
    bool Success = false;
    OUString ObjectId;
    OUString Target;
    OUString PreviewMode;
    OUString PreviewSummary;
    OUString Message;
};

class AIChatContentOpener final
{
public:
    AIChatContentOpenResult OpenReadOnlyPreview(const AIChatContentRegistryEntry& rEntry) const;

    static OUString ResolveOpenTarget(const AIChatContentRegistryEntry& rEntry);
    static bool IsSupportedTarget(const OUString& rTarget);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
