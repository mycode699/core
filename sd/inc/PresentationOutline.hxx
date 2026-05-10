/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sddllapi.h>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

class SdDrawDocument;

namespace sd::intelligent
{
struct PresentationOutlineSourceRef
{
    OUString maId;
    OUString maKind;
};

struct PresentationOutlineBullet
{
    OUString maText;
    sal_Int32 mnLevel = 1;
    OUString maSourceRef;
};

enum class PresentationOutlinePlaceholderIntent
{
    Title,
    Body,
    TwoColumnLeft,
    TwoColumnRight,
    Table,
    ImageLater,
    SpeakerNotes,
};

struct PresentationOutlinePlaceholder
{
    PresentationOutlinePlaceholderIntent meIntent = PresentationOutlinePlaceholderIntent::Body;
    bool mbEditable = true;
    OUString maSourceRef;
};

enum class PresentationOutlineLayout
{
    Title,
    TitleBody,
    TwoColumn,
    Table,
    ImageLater,
    Closing,
    Unsupported,
};

struct PresentationOutlineSlide
{
    OUString maId;
    OUString maSectionZh;
    OUString maTitleZh;
    PresentationOutlineLayout meLayout = PresentationOutlineLayout::TitleBody;
    std::vector<PresentationOutlineBullet> maBullets;
    OUString maNotesZh;
    std::vector<PresentationOutlinePlaceholder> maPlaceholders;
};

struct PresentationOutline
{
    OUString maId;
    OUString maTitleZh;
    OUString maLanguage;
    OUString maSourceModule;
    std::vector<PresentationOutlineSourceRef> maSourceRefs;
    std::vector<PresentationOutlineSlide> maSlides;
};

enum class PresentationOutlineDiagnostic
{
    ZeroSlides,
    UnknownLayout,
    MissingTitlePlaceholder,
    NonEditablePlaceholder,
    UnsupportedPlaceholder,
    BulletLevelClamped,
    SpeakerNotesUnsupported,
};

struct PresentationOutlineBuildResult
{
    bool mbSuccess = false;
    sal_uInt16 mnSlideCountCreated = 0;
    bool mbPlaceholdersMaterialized = false;
    bool mbSpeakerNotesMaterialized = false;
    std::vector<PresentationOutlineDiagnostic> maDiagnostics;
};

SD_DLLPUBLIC PresentationOutlineBuildResult
BuildPresentationFromOutline(SdDrawDocument& rDocument, const PresentationOutline& rOutline);

SD_DLLPUBLIC OUString GetPresentationOutlineDiagnosticName(PresentationOutlineDiagnostic eDiagnostic);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
