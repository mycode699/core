/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <sidebar/MaterialOcr.hxx>

#include <osl/file.hxx>
#include <osl/process.h>
#include <osl/time.h>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>
#include <sal/log.hxx>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(MACOSX)
extern "C" sal_Bool kqoffice_ocr_vision(const char* utf8Path, char** outUtf8, sal_Int32* outLen);
#endif

namespace sfx2::sidebar
{
namespace
{
bool pathExistsSys(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    return osl::DirectoryItem::get(url, item) == osl::FileBase::E_None;
}

OUString readUtf8File(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return OUString();
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return OUString();
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 2 * 1024 * 1024)
    {
        f.close();
        return OUString();
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    return OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
}

OUString whichTesseract()
{
    // Common install locations + PATH lookup
    const char* candidates[] = {
        "/opt/homebrew/bin/tesseract",
        "/usr/local/bin/tesseract",
        "/usr/bin/tesseract",
        // Windows (if installed via UB Mannheim / scoop / chocolatey)
        "C:/Program Files/Tesseract-OCR/tesseract.exe",
        "C:/Program Files (x86)/Tesseract-OCR/tesseract.exe",
    };
    for (const char* c : candidates)
    {
        if (pathExistsSys(OUString::fromUtf8(c)))
            return OUString::fromUtf8(c);
    }
#if defined(_WIN32)
    // where tesseract
    FILE* p = _popen("where tesseract 2>NUL", "r");
    if (!p)
        return OUString();
    char buf[512];
    if (!fgets(buf, sizeof(buf), p))
    {
        _pclose(p);
        return OUString();
    }
    _pclose(p);
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    if (s.empty())
        return OUString();
    const OUString path = OUString::fromUtf8(s.c_str());
    return pathExistsSys(path) ? path : OUString();
#else
    FILE* p = popen("command -v tesseract 2>/dev/null", "r");
    if (!p)
        return OUString();
    char buf[512];
    if (!fgets(buf, sizeof(buf), p))
    {
        pclose(p);
        return OUString();
    }
    pclose(p);
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    if (s.empty())
        return OUString();
    const OUString path = OUString::fromUtf8(s.c_str());
    return pathExistsSys(path) ? path : OUString();
#endif
}

OUString shellQuote(const OUString& r)
{
    // single-quote for POSIX shell; escape embedded '
    OUStringBuffer b;
    b.append(u'\'');
    for (sal_Int32 i = 0; i < r.getLength(); ++i)
    {
        if (r[i] == u'\'')
            b.append(u"'\\''"_ustr);
        else
            b.append(r[i]);
    }
    b.append(u'\'');
    return b.makeStringAndClear();
}

OUString runTesseract(const OUString& rImagePath)
{
    const OUString tess = whichTesseract();
    if (tess.isEmpty())
        return OUString();

    // temp output base (tesseract appends .txt)
    const char* home = std::getenv("HOME");
    OUString base = home && *home
                        ? OUString::fromUtf8(home) + u"/.config/kqoffice/notebook/materials/.ocr-tmp"_ustr
                        : u"/tmp/kqoffice-ocr-tmp"_ustr;
    // ensure parent dir
    {
        const sal_Int32 slash = base.lastIndexOf(u'/');
        if (slash > 0)
        {
            OUString dirUrl;
            if (osl::FileBase::getFileURLFromSystemPath(base.copy(0, slash), dirUrl)
                == osl::FileBase::E_None)
                osl::Directory::createPath(dirUrl);
        }
    }
    // unique-ish suffix
    base += u"-"_ustr + OUString::number(static_cast<sal_Int32>(osl_getGlobalTimer()));

    const OUString outTxt = base + u".txt"_ustr;
    // remove stale
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(outTxt, url) == osl::FileBase::E_None)
            osl::File::remove(url);
    }

    // Prefer Chinese simplified + English when tessdata available; fall back to eng.
    const OUString cmd
        = shellQuote(tess) + u" "_ustr + shellQuote(rImagePath) + u" "_ustr + shellQuote(base)
          + u" -l chi_sim+eng --psm 3 2>/dev/null || "_ustr + shellQuote(tess) + u" "_ustr
          + shellQuote(rImagePath) + u" "_ustr + shellQuote(base) + u" -l eng --psm 3 2>/dev/null"_ustr;

    const OString cmd8 = OUStringToOString(cmd, RTL_TEXTENCODING_UTF8);
    const int rc = std::system(cmd8.getStr());
    (void)rc;

    OUString text = readUtf8File(outTxt);
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(outTxt, url) == osl::FileBase::E_None)
            osl::File::remove(url);
    }
    return text.trim();
}

#if defined(MACOSX)
OUString runVision(const OUString& rImagePath)
{
    const OString path8 = OUStringToOString(rImagePath, RTL_TEXTENCODING_UTF8);
    char* out = nullptr;
    sal_Int32 len = 0;
    if (!kqoffice_ocr_vision(path8.getStr(), &out, &len) || !out || len <= 0)
    {
        if (out)
            std::free(out);
        return OUString();
    }
    const OUString text = OUString::fromUtf8(std::string_view(out, static_cast<size_t>(len))).trim();
    std::free(out);
    return text;
}
#endif
} // namespace

OUString RecognizeImageText(const OUString& rSystemPath)
{
    if (rSystemPath.isEmpty() || !pathExistsSys(rSystemPath))
        return OUString();

#if defined(MACOSX)
    try
    {
        const OUString v = runVision(rSystemPath);
        if (v.getLength() >= 2)
        {
            SAL_INFO("sfx.sidebar", "MaterialOcr: Vision recognized " << v.getLength() << " chars");
            return u"[图片 OCR · Vision 本地]\n"_ustr + v;
        }
    }
    catch (...)
    {
    }
#endif

    try
    {
        const OUString t = runTesseract(rSystemPath);
        if (t.getLength() >= 2)
        {
            SAL_INFO("sfx.sidebar", "MaterialOcr: tesseract recognized " << t.getLength() << " chars");
            return u"[图片 OCR · tesseract 本地]\n"_ustr + t;
        }
    }
    catch (...)
    {
    }

    return OUString();
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
