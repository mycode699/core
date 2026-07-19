/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: AI workspace content objects).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

enum class AIChatContentObjectType
{
    PlainTextLarge,
    StructuredText,
};

struct AIChatMaterializedContent
{
    OUString ObjectId;
    OUString Reference;
    OUString SidecarUrl;
    AIChatContentObjectType Type = AIChatContentObjectType::PlainTextLarge;
};

class AIChatContentObjectStore final
{
public:
    AIChatContentObjectStore();

    const OUString& GetStorageRootUrl() const { return m_sStorageRootUrl; }

    bool ShouldMaterializeText(const OUString& rText) const;
    AIChatMaterializedContent MaterializeText(const OUString& rText) const;

    /// Wave D5: resolve sidecar URL / read UTF-8 body for sidebar text preview.
    OUString MakeSidecarUrl(const OUString& rObjectId) const;
    bool ReadObjectText(const OUString& rObjectId, OUString& rText) const;

    static OUString DetectTypeLabel(AIChatContentObjectType eType);

private:
    OUString m_sStorageRootUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
