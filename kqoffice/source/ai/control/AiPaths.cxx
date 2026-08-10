/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "AiPaths.hxx"

#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace kqoffice::ai
{
namespace
{
OUString fromEnv(const char* name)
{
    if (!name)
        return {};
    const char* v = std::getenv(name);
    if (!v || !*v)
        return {};
    return OUString::fromUtf8(v);
}

OUString stripTrailingSeps(OUString p)
{
    while (p.getLength() > 1 && (p.endsWith(u"/") || p.endsWith(u"\\")))
        p = p.copy(0, p.getLength() - 1);
    return p;
}
} // namespace

OUString kqofficeUserHomeDir()
{
#if defined(_WIN32)
    OUString home = fromEnv("USERPROFILE");
    if (!home.isEmpty())
        return stripTrailingSeps(home);
    const OUString drive = fromEnv("HOMEDRIVE");
    const OUString path = fromEnv("HOMEPATH");
    if (!drive.isEmpty() && !path.isEmpty())
        return stripTrailingSeps(drive + path);
    // Last resort: profile via APPDATA parent
    OUString app = fromEnv("APPDATA");
    if (!app.isEmpty())
    {
        // typically C:\Users\<name>\AppData\Roaming → up two
        const OUString parent = kqofficeParentDir(kqofficeParentDir(app));
        if (!parent.isEmpty())
            return parent;
    }
#else
    OUString home = fromEnv("HOME");
    if (!home.isEmpty())
        return stripTrailingSeps(home);
#endif
    return {};
}

OUString kqofficeAiConfigDir()
{
    if (const char* o = std::getenv("KQOFFICE_CONFIG_DIR"); o && *o)
        return stripTrailingSeps(OUString::fromUtf8(o));

#if defined(_WIN32)
    // Product-class desktop apps live under %APPDATA% (WPS/迅雷 style).
    OUString app = fromEnv("APPDATA");
    if (!app.isEmpty())
        return stripTrailingSeps(app) + u"/kqoffice"_ustr;
    const OUString home = kqofficeUserHomeDir();
    if (!home.isEmpty())
        return home + u"/.config/kqoffice"_ustr;
    const OUString tmp = kqofficeTempDir();
    return tmp + u"/kqoffice-config"_ustr;
#else
    const OUString home = kqofficeUserHomeDir();
    if (!home.isEmpty())
        return home + u"/.config/kqoffice"_ustr;
    return u"/tmp/kqoffice-config"_ustr;
#endif
}

OUString kqofficeUserDocumentsDir()
{
    if (const char* o = std::getenv("KQOFFICE_DOCUMENTS_DIR"); o && *o)
        return stripTrailingSeps(OUString::fromUtf8(o));

    const OUString home = kqofficeUserHomeDir();
#if defined(_WIN32)
    // Prefer OneDrive\Documents when that is the user's real Documents (common).
    if (!home.isEmpty())
    {
        const OUString od = home + u"/OneDrive/Documents"_ustr;
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(od, url) == osl::FileBase::E_None)
        {
            osl::DirectoryItem item;
            if (osl::DirectoryItem::get(url, item) == osl::FileBase::E_None)
            {
                osl::FileStatus st(osl_FileStatus_Mask_Type);
                if (item.getFileStatus(st) == osl::FileBase::E_None
                    && st.getFileType() == osl::FileStatus::Directory)
                    return od;
            }
        }
        return home + u"/Documents"_ustr;
    }
    return kqofficeAiConfigDir();
#else
    if (!home.isEmpty())
        return home + u"/Documents"_ustr;
    return u"/tmp"_ustr;
#endif
}

OUString kqofficeTempDir()
{
    OUString t = fromEnv("TMPDIR");
    if (t.isEmpty())
        t = fromEnv("TMP");
    if (t.isEmpty())
        t = fromEnv("TEMP");
    if (!t.isEmpty())
        return stripTrailingSeps(t);
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetTempPathW(MAX_PATH, buf);
    if (n > 0 && n < MAX_PATH)
        return stripTrailingSeps(OUString(buf));
    return u"C:/Windows/Temp"_ustr;
#else
    return u"/tmp"_ustr;
#endif
}

OUString kqofficePathJoin(const OUString& rA, const OUString& rB)
{
    if (rA.isEmpty())
        return rB;
    if (rB.isEmpty())
        return rA;
    if (rA.endsWith(u"/") || rA.endsWith(u"\\"))
        return rA + rB;
    return rA + u"/"_ustr + rB;
}

OUString kqofficeParentDir(const OUString& rPath)
{
    if (rPath.isEmpty())
        return {};
    sal_Int32 slash = rPath.lastIndexOf(u'/');
    const sal_Int32 bslash = rPath.lastIndexOf(u'\\');
    if (bslash > slash)
        slash = bslash;
    if (slash <= 0)
        return {};
#if defined(_WIN32)
    // Keep drive root "C:/" intact
    if (slash == 2 && rPath.getLength() > 2 && rPath[1] == u':')
        return rPath.copy(0, 3);
#endif
    return rPath.copy(0, slash);
}

OUString kqofficeFileName(const OUString& rPath)
{
    sal_Int32 slash = rPath.lastIndexOf(u'/');
    const sal_Int32 bslash = rPath.lastIndexOf(u'\\');
    if (bslash > slash)
        slash = bslash;
    if (slash >= 0 && slash + 1 < rPath.getLength())
        return rPath.copy(slash + 1);
    return rPath;
}

} // namespace kqoffice::ai
