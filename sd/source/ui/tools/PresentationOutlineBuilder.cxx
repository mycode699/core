/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <PresentationOutline.hxx>

#include <rtl/ustrbuf.hxx>

#include <drawdoc.hxx>
#include <sdpage.hxx>
#include <pres.hxx>

#include <svx/svdotext.hxx>
#include <xmloff/autolayout.hxx>

#include <algorithm>

namespace sd::intelligent
{
namespace
{
void AddDiagnosticOnce(std::vector<PresentationOutlineDiagnostic>& rDiagnostics,
                       PresentationOutlineDiagnostic eDiagnostic)
{
    if (std::find(rDiagnostics.begin(), rDiagnostics.end(), eDiagnostic) == rDiagnostics.end())
        rDiagnostics.push_back(eDiagnostic);
}

bool HasPlaceholder(const PresentationOutlineSlide& rSlide,
                    PresentationOutlinePlaceholderIntent eIntent)
{
    return std::any_of(rSlide.maPlaceholders.begin(), rSlide.maPlaceholders.end(),
                       [eIntent](const PresentationOutlinePlaceholder& rPlaceholder) {
                           return rPlaceholder.meIntent == eIntent;
                       });
}

bool HasTitlePlaceholder(const PresentationOutlineSlide& rSlide)
{
    return HasPlaceholder(rSlide, PresentationOutlinePlaceholderIntent::Title);
}

AutoLayout GetAutoLayoutForSlide(const PresentationOutlineSlide& rSlide)
{
    switch (rSlide.meLayout)
    {
        case PresentationOutlineLayout::Title:
        case PresentationOutlineLayout::Closing:
            return AUTOLAYOUT_TITLE_ONLY;
        case PresentationOutlineLayout::TwoColumn:
            return AUTOLAYOUT_TITLE_2CONTENT;
        case PresentationOutlineLayout::TitleBody:
        case PresentationOutlineLayout::Table:
        case PresentationOutlineLayout::ImageLater:
            return AUTOLAYOUT_TITLE_CONTENT;
        case PresentationOutlineLayout::Unsupported:
            break;
    }
    return AUTOLAYOUT_NONE;
}

OUString GetIntentPlaceholderText(PresentationOutlinePlaceholderIntent eIntent)
{
    switch (eIntent)
    {
        case PresentationOutlinePlaceholderIntent::Table:
            return u"表格占位：后续编辑"_ustr;
        case PresentationOutlinePlaceholderIntent::ImageLater:
            return u"图片占位：后续插入"_ustr;
        case PresentationOutlinePlaceholderIntent::SpeakerNotes:
            return u"演讲备注占位：后续编辑"_ustr;
        case PresentationOutlinePlaceholderIntent::TwoColumnLeft:
        case PresentationOutlinePlaceholderIntent::TwoColumnRight:
        case PresentationOutlinePlaceholderIntent::Body:
        case PresentationOutlinePlaceholderIntent::Title:
            break;
    }
    return OUString();
}

OUString JoinBulletText(const std::vector<PresentationOutlineBullet>& rBullets, size_t nBegin,
                        size_t nEnd,
                        std::vector<PresentationOutlineDiagnostic>& rDiagnostics)
{
    OUStringBuffer aBuffer;
    for (size_t nIndex = nBegin; nIndex < nEnd; ++nIndex)
    {
        if (nIndex > nBegin)
            aBuffer.append('\n');

        sal_Int32 nLevel = rBullets[nIndex].mnLevel;
        if (nLevel < 1)
        {
            nLevel = 1;
            AddDiagnosticOnce(rDiagnostics, PresentationOutlineDiagnostic::BulletLevelClamped);
        }
        else if (nLevel > 2)
        {
            nLevel = 2;
            AddDiagnosticOnce(rDiagnostics, PresentationOutlineDiagnostic::BulletLevelClamped);
        }

        if (nLevel > 1)
            aBuffer.append(u"  "_ustr);
        aBuffer.append(rBullets[nIndex].maText);
    }
    return aBuffer.makeStringAndClear();
}

SdrTextObj* GetTextPresObj(SdPage& rPage, PresObjKind eKind, int nIndex = 1)
{
    return DynCastSdrTextObj(rPage.GetPresObj(eKind, nIndex));
}

void SetPresentationText(SdPage& rPage, PresObjKind eKind, const OUString& rText, int nIndex = 1)
{
    SdrTextObj* pTextObject = GetTextPresObj(rPage, eKind, nIndex);
    if (!pTextObject)
        return;

    rPage.SetObjText(pTextObject, nullptr, eKind, rText);
}

void MaterializeBody(SdPage& rPage, const PresentationOutlineSlide& rSlide,
                     PresentationOutlineBuildResult& rResult)
{
    switch (rSlide.meLayout)
    {
        case PresentationOutlineLayout::TitleBody:
        case PresentationOutlineLayout::Closing:
            if (!rSlide.maBullets.empty() || HasPlaceholder(rSlide, PresentationOutlinePlaceholderIntent::Body))
            {
                OUString aText = JoinBulletText(rSlide.maBullets, 0, rSlide.maBullets.size(),
                                                rResult.maDiagnostics);
                SetPresentationText(rPage, PresObjKind::Outline, aText);
            }
            break;
        case PresentationOutlineLayout::TwoColumn:
        {
            const size_t nSplit = (rSlide.maBullets.size() + 1) / 2;
            SetPresentationText(rPage, PresObjKind::Outline,
                                JoinBulletText(rSlide.maBullets, 0, nSplit, rResult.maDiagnostics), 1);
            SetPresentationText(rPage, PresObjKind::Outline,
                                JoinBulletText(rSlide.maBullets, nSplit, rSlide.maBullets.size(),
                                               rResult.maDiagnostics),
                                2);
            break;
        }
        case PresentationOutlineLayout::Table:
            SetPresentationText(rPage, PresObjKind::Outline,
                                GetIntentPlaceholderText(PresentationOutlinePlaceholderIntent::Table));
            break;
        case PresentationOutlineLayout::ImageLater:
            SetPresentationText(rPage, PresObjKind::Outline,
                                GetIntentPlaceholderText(PresentationOutlinePlaceholderIntent::ImageLater));
            break;
        case PresentationOutlineLayout::Title:
        case PresentationOutlineLayout::Unsupported:
            break;
    }
}

bool ValidateSlide(const PresentationOutlineSlide& rSlide, PresentationOutlineBuildResult& rResult)
{
    bool bValid = true;

    if (rSlide.meLayout == PresentationOutlineLayout::Unsupported)
    {
        AddDiagnosticOnce(rResult.maDiagnostics, PresentationOutlineDiagnostic::UnknownLayout);
        bValid = false;
    }

    if (!HasTitlePlaceholder(rSlide))
    {
        AddDiagnosticOnce(rResult.maDiagnostics, PresentationOutlineDiagnostic::MissingTitlePlaceholder);
        bValid = false;
    }

    for (const PresentationOutlinePlaceholder& rPlaceholder : rSlide.maPlaceholders)
    {
        if (!rPlaceholder.mbEditable)
        {
            AddDiagnosticOnce(rResult.maDiagnostics, PresentationOutlineDiagnostic::NonEditablePlaceholder);
            bValid = false;
        }

        switch (rPlaceholder.meIntent)
        {
            case PresentationOutlinePlaceholderIntent::Title:
            case PresentationOutlinePlaceholderIntent::Body:
            case PresentationOutlinePlaceholderIntent::TwoColumnLeft:
            case PresentationOutlinePlaceholderIntent::TwoColumnRight:
            case PresentationOutlinePlaceholderIntent::Table:
            case PresentationOutlinePlaceholderIntent::ImageLater:
            case PresentationOutlinePlaceholderIntent::SpeakerNotes:
                break;
        }
    }

    if (!rSlide.maNotesZh.isEmpty()
        || HasPlaceholder(rSlide, PresentationOutlinePlaceholderIntent::SpeakerNotes))
    {
        AddDiagnosticOnce(rResult.maDiagnostics,
                          PresentationOutlineDiagnostic::SpeakerNotesUnsupported);
    }

    return bValid;
}

SdPage* EnsureSlide(SdDrawDocument& rDocument, sal_uInt16 nSlideIndex, AutoLayout eLayout)
{
    SdPage* pPage = nullptr;

    while (rDocument.GetSdPageCount(PageKind::Standard) <= nSlideIndex)
    {
        SdPage* pLastPage
            = rDocument.GetSdPage(rDocument.GetSdPageCount(PageKind::Standard) - 1, PageKind::Standard);
        if (!pLastPage)
            return nullptr;

        const sal_uInt16 nCreatedIndex = rDocument.CreatePage(pLastPage, PageKind::Standard, OUString(),
                                                              OUString(), eLayout, AUTOLAYOUT_NOTES,
                                                              true, true, -1);
        if (nCreatedIndex == 0xffff)
            return nullptr;

        pPage = rDocument.GetSdPage(nSlideIndex, PageKind::Standard);
        if (!pPage)
            return nullptr;
    }

    pPage = rDocument.GetSdPage(nSlideIndex, PageKind::Standard);
    if (pPage)
        pPage->SetAutoLayout(eLayout, true, true);
    return pPage;
}
}

PresentationOutlineBuildResult BuildPresentationFromOutline(SdDrawDocument& rDocument,
                                                            const PresentationOutline& rOutline)
{
    PresentationOutlineBuildResult aResult;

    if (rOutline.maSlides.empty())
    {
        AddDiagnosticOnce(aResult.maDiagnostics, PresentationOutlineDiagnostic::ZeroSlides);
        return aResult;
    }

    if (!rDocument.GetSdPage(0, PageKind::Standard))
    {
        AddDiagnosticOnce(aResult.maDiagnostics, PresentationOutlineDiagnostic::UnknownLayout);
        return aResult;
    }

    bool bCanBuild = true;
    for (const PresentationOutlineSlide& rSlide : rOutline.maSlides)
        bCanBuild = ValidateSlide(rSlide, aResult) && bCanBuild;

    if (!bCanBuild)
        return aResult;

    for (sal_uInt16 nSlideIndex = 0; nSlideIndex < rOutline.maSlides.size(); ++nSlideIndex)
    {
        const PresentationOutlineSlide& rSlide = rOutline.maSlides[nSlideIndex];
        SdPage* pPage = EnsureSlide(rDocument, nSlideIndex, GetAutoLayoutForSlide(rSlide));
        if (!pPage)
        {
            AddDiagnosticOnce(aResult.maDiagnostics, PresentationOutlineDiagnostic::UnknownLayout);
            return aResult;
        }

        SetPresentationText(*pPage, PresObjKind::Title, rSlide.maTitleZh);
        MaterializeBody(*pPage, rSlide, aResult);
        ++aResult.mnSlideCountCreated;
    }

    aResult.mbSuccess = aResult.mnSlideCountCreated == rOutline.maSlides.size();
    aResult.mbPlaceholdersMaterialized = aResult.mbSuccess;
    aResult.mbSpeakerNotesMaterialized = false;
    return aResult;
}

OUString GetPresentationOutlineDiagnosticName(PresentationOutlineDiagnostic eDiagnostic)
{
    switch (eDiagnostic)
    {
        case PresentationOutlineDiagnostic::ZeroSlides:
            return u"zero-slides"_ustr;
        case PresentationOutlineDiagnostic::UnknownLayout:
            return u"unknown-layout"_ustr;
        case PresentationOutlineDiagnostic::MissingTitlePlaceholder:
            return u"missing-title-placeholder"_ustr;
        case PresentationOutlineDiagnostic::NonEditablePlaceholder:
            return u"non-editable-placeholder"_ustr;
        case PresentationOutlineDiagnostic::UnsupportedPlaceholder:
            return u"unsupported-placeholder"_ustr;
        case PresentationOutlineDiagnostic::BulletLevelClamped:
            return u"bullet-level-clamped"_ustr;
        case PresentationOutlineDiagnostic::SpeakerNotesUnsupported:
            return u"speaker-notes-unsupported"_ustr;
    }
    return u"unknown-diagnostic"_ustr;
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
