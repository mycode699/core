/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <config_version.h>

#include <IconHelper.hxx>
#include <svdata.hxx>

#include <vcl/svapp.hxx>
#include <vcl/bitmaps.hlst>

namespace
{
OUString lclExtractLastPathSegment(const OUString& rPath)
{
    const sal_Int32 nSlash = rPath.lastIndexOf('/');
    return nSlash >= 0 ? rPath.copy(nSlash + 1) : rPath;
}

bool lclIsKnownDesktopPrefix(const OUString& rPrefix)
{
    return rPrefix.startsWith(u"kequanoffice")
           || rPrefix.startsWith(u"libreoffice")
           || rPrefix.startsWith(u"libreofficedev");
}

OUString lclGetFallbackDesktopPrefix()
{
    return u"kequanoffice"_ustr + OUString::createFromAscii(LIBO_VERSION_DOTTED_2);
}

OUString lclBuildAppIconName(std::u16string_view rModuleName)
{
    return IconHelper::GetDesktopIntegrationPrefix() + u"-"_ustr + OUString(rModuleName);
}
}

OUString IconHelper::GetDesktopIntegrationPrefix()
{
    static const OUString aDesktopPrefix = []() {
        const OUString aAppFileName = Application::GetAppFileName();
        const OUString aExecutableName = lclExtractLastPathSegment(aAppFileName);
        if (lclIsKnownDesktopPrefix(aExecutableName))
            return aExecutableName;

        const sal_Int32 nProgramPos = aAppFileName.lastIndexOf(u"/program/"_ustr);
        if (nProgramPos > 0)
        {
            const OUString aInstallRoot = lclExtractLastPathSegment(aAppFileName.copy(0, nProgramPos));
            if (lclIsKnownDesktopPrefix(aInstallRoot))
                return aInstallRoot;
        }

        return lclGetFallbackDesktopPrefix();
    }();

    return aDesktopPrefix;
}

OUString IconHelper::GetAppIconName(sal_uInt16 nIcon)
{
    switch (nIcon)
    {
        case SV_ICON_ID_TEXT:
            return lclBuildAppIconName(u"writer");
        case SV_ICON_ID_SPREADSHEET:
            return lclBuildAppIconName(u"calc");
        case SV_ICON_ID_DRAWING:
            return lclBuildAppIconName(u"draw");
        case SV_ICON_ID_PRESENTATION:
            return lclBuildAppIconName(u"impress");
        case SV_ICON_ID_DATABASE:
            return lclBuildAppIconName(u"base");
        case SV_ICON_ID_FORMULA:
            return lclBuildAppIconName(u"math");
        default:
            return GetStartCenterAppIconName();
    }
}

OUString IconHelper::GetStartCenterAppIconName() { return lclBuildAppIconName(u"startcenter"); }

OUString IconHelper::GetInternalAppIconName(sal_uInt16 nIcon)
{
    switch (nIcon)
    {
        case SV_ICON_ID_TEXT:
            return RID_FILE_THUMBNAIL_TEXT;
        case SV_ICON_ID_SPREADSHEET:
            return RID_FILE_THUMBNAIL_SHEET;
        case SV_ICON_ID_DRAWING:
            return RID_FILE_THUMBNAIL_DRAWING;
        case SV_ICON_ID_PRESENTATION:
            return RID_FILE_THUMBNAIL_PRESENTATION;
        case SV_ICON_ID_DATABASE:
            return RID_FILE_THUMBNAIL_DATABASE;
        case SV_ICON_ID_FORMULA:
            return RID_FILE_THUMBNAIL_MATH;
        default:
            return RID_FILE_THUMBNAIL_DEFAULT;
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
