/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace preview matrix).
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

struct AIChatPreviewResult
{
    bool Success = false;
    OUString ObjectId;
    OUString ContentType;
    OUString Target;
    OUString Mode;
    OUString EvidenceBadge;
    OUString SourceMetadata;
    OUString Summary;
};

class AIChatPreviewMatrix final
{
public:
    AIChatPreviewResult BuildPreview(const AIChatContentRegistryEntry& rEntry) const;

    static OUString ResolvePreviewTarget(const AIChatContentRegistryEntry& rEntry);
    static OUString ResolvePreviewMode(const AIChatContentRegistryEntry& rEntry);
    static bool IsSupportedPreviewTarget(const OUString& rTarget);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
