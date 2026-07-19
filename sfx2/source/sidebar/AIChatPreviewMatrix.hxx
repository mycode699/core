/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1 / Wave D5: AI workspace preview matrix).
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

/** Wave D5 file-kind for DuMate-style 任务文件 / 产物 preview. */
enum class AIChatPreviewFileKind
{
    Metadata = 0, ///< no resolvable file path — registry metadata only
    Text, ///< plain text / csv / json / …
    Markdown, ///< .md / .markdown
    OfficeDocument, ///< pdf / docx / odt (open via LO filters)
    Image, ///< png / jpg / gif / webp — open via system / LO when possible
    Unsupported, ///< path present but no embedded preview
};

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
    /// D5: file classification + resolved path (system path when available).
    AIChatPreviewFileKind FileKind = AIChatPreviewFileKind::Metadata;
    OUString FileKindLabel; ///< text | markdown | office-document | unsupported | metadata
    OUString FilePath;
    OUString FileUrl;
    /// Optional short body snippet for sidebar details (text/markdown only).
    OUString PreviewBody;
    /// Chinese user-visible one-liner (status / details).
    OUString UserMessage;
};

class AIChatPreviewMatrix final
{
public:
    AIChatPreviewResult BuildPreview(const AIChatContentRegistryEntry& rEntry) const;

    static OUString ResolvePreviewTarget(const AIChatContentRegistryEntry& rEntry);
    static OUString ResolvePreviewMode(const AIChatContentRegistryEntry& rEntry);
    static bool IsSupportedPreviewTarget(const OUString& rTarget);

    /// Resolve a local file path from registry fields (SourceSurface / HashReference / ObjectId).
    static OUString ResolveArtifactFilePath(const AIChatContentRegistryEntry& rEntry);
    /// Classify by extension + entry type / preview mode.
    static AIChatPreviewFileKind DetectFileKind(const OUString& rPathOrName,
                                                const AIChatContentRegistryEntry& rEntry);
    /// Machine token: text | markdown | office-document | image | unsupported | metadata
    static OUString FileKindToLabel(AIChatPreviewFileKind eKind);
    /// Chinese UI label for content tab / status.
    static OUString FileKindToLabelZh(AIChatPreviewFileKind eKind);
    /// file:// URL for LO / shell open; empty if path cannot be converted.
    static OUString ToFileUrl(const OUString& rSystemPathOrUrl);
    /// Content-object sidecar id when HashReference is @artifact:<id>.
    static OUString ResolveContentObjectId(const AIChatContentRegistryEntry& rEntry);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
