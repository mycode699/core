/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "VaultLint.hxx"
#include "VaultStore.hxx"

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/ustrbuf.hxx>

#include <fstream>
#include <string>
#include <vector>

namespace kqoffice::ai::vault
{
namespace
{
OUString ReadFile(const OUString& rSys, sal_Int32 nMax = 8000)
{
    const OString p = OUStringToOString(rSys, RTL_TEXTENCODING_UTF8);
    std::ifstream in(p.getStr(), std::ios::binary);
    if (!in)
        return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (static_cast<sal_Int32>(s.size()) > nMax)
        s.resize(static_cast<size_t>(nMax));
    return OStringToOUString(std::string_view(s.data(), s.size()), RTL_TEXTENCODING_UTF8);
}

bool WriteFile(const OUString& rSys, const OUString& rBody)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    const sal_Int32 slash = rSys.lastIndexOf('/');
    if (slash > 0)
    {
        OUString d;
        if (osl::FileBase::getFileURLFromSystemPath(rSys.copy(0, slash), d) == osl::FileBase::E_None)
            (void)osl::Directory::createPath(d);
    }
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString b = OUStringToOString(rBody, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(b.getStr(), b.getLength(), n);
    f.close();
    return true;
}

void ScanDir(const OUString& rDir, std::vector<OUString>& rFiles)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rDir, url) != osl::FileBase::E_None)
        return;
    osl::Directory dir(url);
    if (dir.open() != osl::FileBase::E_None)
        return;
    for (;;)
    {
        osl::DirectoryItem item;
        if (dir.getNextItem(item) != osl::FileBase::E_None)
            break;
        osl::FileStatus st(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileURL);
        if (item.getFileStatus(st) != osl::FileBase::E_None)
            continue;
        if (st.getFileType() != osl::FileStatus::Regular)
            continue;
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(st.getFileURL(), sys) == osl::FileBase::E_None)
            rFiles.push_back(sys);
    }
    dir.close();
}
} // namespace

VaultLintResult VaultLint::run(const OUString& rVaultRoot)
{
    VaultLintResult r;
    if (!VaultStore::ensureLayout(rVaultRoot))
    {
        r.messageZh = u"资料盘未就绪"_ustr;
        return r;
    }
    const VaultPaths vp = VaultStore::pathsFor(rVaultRoot);
    std::vector<OUString> sources, concepts;
    ScanDir(vp.wiki + u"/sources"_ustr, sources);
    ScanDir(vp.wiki + u"/concepts"_ustr, concepts);

    OUStringBuffer report;
    report.append(u"# 资料盘健康检查\n\n"_ustr);

    for (const auto& s : sources)
    {
        const OUString body = ReadFile(s);
        if (body.getLength() < 40)
        {
            ++r.emptyPages;
            report.append(u"- 过短来源页："_ustr);
            report.append(s);
            report.append(u"\n"_ustr);
        }
        if (body.indexOf(u"来源"_ustr) < 0 && body.indexOf(u"raw/"_ustr) < 0)
        {
            ++r.sourcesWithoutCite;
            report.append(u"- 缺引用线索："_ustr);
            report.append(s);
            report.append(u"\n"_ustr);
        }
    }
    for (const auto& c : concepts)
    {
        const OUString body = ReadFile(c);
        if (body.indexOf(u"sources"_ustr) < 0 && body.indexOf(u"来源"_ustr) < 0)
        {
            ++r.orphanConcepts;
            report.append(u"- 主题页缺回链："_ustr);
            report.append(c);
            report.append(u"\n"_ustr);
        }
    }

    if (r.emptyPages == 0 && r.orphanConcepts == 0 && r.sourcesWithoutCite == 0)
        report.append(u"\n状态：良好（未发现明显问题）\n"_ustr);
    else
    {
        report.append(u"\n汇总：过短页 "_ustr);
        report.append(r.emptyPages);
        report.append(u" · 缺引用 "_ustr);
        report.append(r.sourcesWithoutCite);
        report.append(u" · 弱主题 "_ustr);
        report.append(r.orphanConcepts);
        report.append(u"\n"_ustr);
    }

    TimeValue tv{};
    osl_getSystemTime(&tv);
    const OUString name = u"health-"_ustr + OUString::number(static_cast<sal_Int64>(tv.Seconds))
                          + u".md"_ustr;
    r.reportPath = vp.outputs + u"/health/"_ustr + name;
    r.ok = WriteFile(r.reportPath, report.makeStringAndClear());
    r.messageZh = r.ok ? (u"健康检查已写入 "_ustr + r.reportPath)
                       : u"健康检查写入失败"_ustr;
    return r;
}

} // namespace kqoffice::ai::vault
