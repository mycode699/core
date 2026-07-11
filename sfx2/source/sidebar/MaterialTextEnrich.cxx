/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <sidebar/MaterialTextEnrich.hxx>
#include <sidebar/MaterialOcr.hxx>

#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/packages/zip/ZipFileAccess.hpp>
#include <com/sun/star/uno/Sequence.hxx>
#include <comphelper/processfactory.hxx>
#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>
#include <tools/stream.hxx>
#include <vcl/filter/PDFiumLibrary.hxx>

#include <algorithm>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr sal_Int32 kMaxOut = 180000;
constexpr sal_Int32 kMaxPages = 40;

OUString pathToUrl(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return OUString();
    return url;
}

OUString stripXmlText(const OUString& rXml, sal_Int32 nMax)
{
    OUStringBuffer b;
    bool inTag = false;
    bool sp = false;
    for (sal_Int32 i = 0; i < rXml.getLength() && b.getLength() < nMax; ++i)
    {
        const sal_Unicode c = rXml[i];
        if (c == u'<')
        {
            inTag = true;
            // block boundaries often mean paragraph
            if (b.getLength() && !sp)
            {
                b.append(u'\n');
                sp = true;
            }
            continue;
        }
        if (c == u'>')
        {
            inTag = false;
            continue;
        }
        if (inTag)
            continue;
        if (c == u'&')
        {
            // minimal entities
            if (rXml.match(u"&lt;"_ustr, i))
            {
                b.append(u'<');
                i += 3;
                sp = false;
                continue;
            }
            if (rXml.match(u"&gt;"_ustr, i))
            {
                b.append(u'>');
                i += 3;
                sp = false;
                continue;
            }
            if (rXml.match(u"&amp;"_ustr, i))
            {
                b.append(u'&');
                i += 4;
                sp = false;
                continue;
            }
            if (rXml.match(u"&quot;"_ustr, i))
            {
                b.append(u'"');
                i += 5;
                sp = false;
                continue;
            }
            if (rXml.match(u"&apos;"_ustr, i))
            {
                b.append(u'\'');
                i += 5;
                sp = false;
                continue;
            }
            // numeric &#...; skip roughly
            sal_Int32 j = i + 1;
            if (j < rXml.getLength() && rXml[j] == u'#')
            {
                while (j < rXml.getLength() && rXml[j] != u';')
                    ++j;
                if (j < rXml.getLength())
                    i = j;
                continue;
            }
        }
        if (c == u' ' || c == u'\t' || c == u'\r' || c == u'\n')
        {
            if (!sp && b.getLength())
            {
                b.append(u' ');
                sp = true;
            }
            continue;
        }
        sp = false;
        b.append(c);
    }
    return b.makeStringAndClear();
}

OUString readStreamAll(const css::uno::Reference<css::io::XInputStream>& xIn, sal_Int32 nMax)
{
    if (!xIn.is())
        return OUString();
    OUStringBuffer b;
    css::uno::Sequence<sal_Int8> chunk(std::min<sal_Int32>(nMax, 65536));
    sal_Int32 total = 0;
    while (total < nMax)
    {
        const sal_Int32 n = xIn->readBytes(chunk, chunk.getLength());
        if (n <= 0)
            break;
        b.append(OUString(reinterpret_cast<const char*>(chunk.getConstArray()), n,
                          RTL_TEXTENCODING_UTF8));
        total += n;
        if (n < chunk.getLength())
            break;
    }
    return b.makeStringAndClear();
}

OUString extractPdfium(const OUString& rSysPath)
{
    const OUString url = pathToUrl(rSysPath);
    if (url.isEmpty())
        return OUString();

    auto pPDFium = vcl::pdf::PDFiumLibrary::get();
    if (!pPDFium)
        return OUString();

    SvFileStream aFile(url, StreamMode::READ);
    if (!aFile.IsOpen())
        return OUString();
    const sal_uInt64 nSize = aFile.remainingSize();
    if (nSize == 0 || nSize > 40 * 1024 * 1024)
        return OUString();
    std::vector<sal_uInt8> buf(static_cast<size_t>(nSize));
    const sal_uInt64 nRead = aFile.ReadBytes(buf.data(), nSize);
    if (nRead == 0)
        return OUString();

    std::unique_ptr<vcl::pdf::PDFiumDocument> pDoc
        = pPDFium->openDocument(buf.data(), static_cast<int>(nRead), OString());
    if (!pDoc)
        return OUString();

    const int nPages = std::min(pDoc->getPageCount(), kMaxPages);
    OUStringBuffer out;
    out.append(u"[PDF 深度提取 · PDFium · "_ustr);
    out.append(static_cast<sal_Int32>(pDoc->getPageCount()));
    out.append(u" 页]\n"_ustr);

    for (int p = 0; p < nPages && out.getLength() < kMaxOut; ++p)
    {
        std::unique_ptr<vcl::pdf::PDFiumPage> pPage = pDoc->openPage(p);
        if (!pPage)
            continue;
        std::unique_ptr<vcl::pdf::PDFiumTextPage> pText = pPage->getTextPage();
        if (!pText)
            continue;
        const int nChars = pText->countChars();
        if (nChars <= 0)
            continue;
        out.append(u"\n—— 第 "_ustr);
        out.append(static_cast<sal_Int32>(p + 1));
        out.append(u" 页 ——\n"_ustr);
        sal_Unicode prev = 0;
        for (int i = 0; i < nChars && out.getLength() < kMaxOut; ++i)
        {
            const unsigned int u = pText->getUnicode(i);
            if (u == 0)
                continue;
            // pdfium often uses \r as line break
            if (u == '\r')
            {
                out.append(u'\n');
                prev = u'\n';
                continue;
            }
            if (u == ' ' && prev == ' ')
                continue;
            out.append(static_cast<sal_Unicode>(u));
            prev = static_cast<sal_Unicode>(u);
        }
        out.append(u'\n');
    }
    return out.makeStringAndClear();
}

OUString extractOfficeZip(const OUString& rSysPath)
{
    const OUString url = pathToUrl(rSysPath);
    if (url.isEmpty())
        return OUString();
    try
    {
        auto xCtx = comphelper::getProcessComponentContext();
        css::uno::Reference<css::packages::zip::XZipFileAccess2> xZip
            = css::packages::zip::ZipFileAccess::createWithURL(xCtx, url);
        if (!xZip.is())
            return OUString();

        // Prefer content paths by format
        const OUString candidates[] = {
            u"content.xml"_ustr, // ODF
            u"word/document.xml"_ustr, // DOCX
            u"xl/sharedStrings.xml"_ustr, // XLSX strings
            u"ppt/slides/slide1.xml"_ustr, // PPTX first slide
        };

        OUStringBuffer all;
        all.append(u"[办公文稿提取 · ZIP/XML]\n"_ustr);
        bool any = false;

        for (const auto& name : candidates)
        {
            if (!xZip->hasByName(name))
                continue;
            css::uno::Any a = xZip->getByName(name);
            css::uno::Reference<css::io::XInputStream> xIn;
            a >>= xIn;
            if (!xIn.is())
                continue;
            const OUString xml = readStreamAll(xIn, 2 * 1024 * 1024);
            const OUString text = stripXmlText(xml, kMaxOut);
            if (text.getLength() < 8)
                continue;
            all.append(u"\n—— "_ustr);
            all.append(name);
            all.append(u" ——\n"_ustr);
            all.append(text);
            all.append(u'\n');
            any = true;
            // For ODF/DOCX one main stream is enough; still allow multi for xlsx/pptx
            if (name == u"content.xml"_ustr || name == u"word/document.xml"_ustr)
                break;
        }

        // PPTX: gather a few more slides if first was found
        if (xZip->hasByName(u"ppt/slides/slide1.xml"_ustr))
        {
            for (sal_Int32 s = 2; s <= 12 && all.getLength() < kMaxOut; ++s)
            {
                const OUString name = u"ppt/slides/slide"_ustr + OUString::number(s) + u".xml"_ustr;
                if (!xZip->hasByName(name))
                    break;
                css::uno::Any a = xZip->getByName(name);
                css::uno::Reference<css::io::XInputStream> xIn;
                a >>= xIn;
                if (!xIn.is())
                    continue;
                const OUString text = stripXmlText(readStreamAll(xIn, 512 * 1024), 20000);
                if (text.getLength() < 4)
                    continue;
                all.append(u"\n—— "_ustr);
                all.append(name);
                all.append(u" ——\n"_ustr);
                all.append(text);
                any = true;
            }
        }

        return any ? all.makeStringAndClear() : OUString();
    }
    catch (...)
    {
        return OUString();
    }
}

OUString extractRtfRough(const OUString& rSysPath)
{
    OUString url = pathToUrl(rSysPath);
    if (url.isEmpty())
        return OUString();
    SvFileStream aFile(url, StreamMode::READ);
    if (!aFile.IsOpen())
        return OUString();
    const sal_uInt64 nSize = std::min<sal_uInt64>(aFile.remainingSize(), 1024 * 1024);
    if (nSize == 0)
        return OUString();
    std::vector<char> buf(static_cast<size_t>(nSize));
    aFile.ReadBytes(buf.data(), nSize);
    // very rough: drop {\...} control words, keep printable runs
    OUStringBuffer out;
    out.append(u"[RTF 粗提取]\n"_ustr);
    bool inCtrl = false;
    for (size_t i = 0; i < buf.size() && out.getLength() < 80000; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(buf[i]);
        if (c == '{')
        {
            inCtrl = false;
            continue;
        }
        if (c == '}')
            continue;
        if (c == '\\')
        {
            inCtrl = true;
            continue;
        }
        if (inCtrl)
        {
            if (c == ' ' || c == '\n' || c == '\r')
                inCtrl = false;
            else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                     || c == '-')
                continue;
            else
                inCtrl = false;
            if (inCtrl)
                continue;
        }
        if (c == '\n' || c == '\r')
        {
            out.append(u'\n');
            continue;
        }
        if (c >= 32 && c < 127)
            out.append(static_cast<sal_Unicode>(c));
    }
    return out.getLength() > 20 ? out.makeStringAndClear() : OUString();
}
} // namespace

OUString EnrichMaterialText(const OUString& rSystemPath, const OUString& rKind)
{
    if (rSystemPath.isEmpty())
        return OUString();
    if (rKind == u"pdf"_ustr)
        return extractPdfium(rSystemPath);
    if (rKind == u"office"_ustr)
    {
        OUString t = extractOfficeZip(rSystemPath);
        if (t.isEmpty() && rSystemPath.toAsciiLowerCase().endsWith(u".rtf"))
            t = extractRtfRough(rSystemPath);
        return t;
    }
    if (rKind == u"image"_ustr)
        return RecognizeImageText(rSystemPath);
    return OUString();
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
