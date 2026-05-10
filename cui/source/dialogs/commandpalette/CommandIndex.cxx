/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2 Day-1a: Command Index).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * loadFromDirectory only — the parser itself is header-only inside
 * CommandIndex.hxx so cppunit can link without dragging in osl/file.
 */

#include <commandpalette/CommandIndex.hxx>

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace cui::commandpalette
{
namespace
{
/// Slurp a file URL (`file://...`) into an OString. Empty on any error.
OString readFileUrl(const OUString& fileUrl)
{
    OUString sysPath;
    if (osl::FileBase::getSystemPathFromFileURL(fileUrl, sysPath)
        != osl::FileBase::E_None)
    {
        return {};
    }
    OString sysPathUtf8 = OUStringToOString(sysPath,
                                            RTL_TEXTENCODING_UTF8);
    std::ifstream in(sysPathUtf8.getStr(), std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string s = ss.str();
    return OString(s.data(), static_cast<sal_Int32>(s.size()));
}
} // namespace

std::vector<CommandEntry> CommandIndex::loadFromDirectory(const OUString& dir)
{
    std::vector<CommandEntry> out;
    osl::Directory directory(dir);
    if (directory.open() != osl::FileBase::E_None)
        return out;

    osl::DirectoryItem item;
    while (directory.getNextItem(item) == osl::FileBase::E_None)
    {
        osl::FileStatus status(osl_FileStatus_Mask_FileName
                               | osl_FileStatus_Mask_FileURL);
        if (item.getFileStatus(status) != osl::FileBase::E_None)
            continue;
        OUString name = status.getFileName();
        if (!name.endsWith("Commands.xcu"))
            continue;

        OString body = readFileUrl(status.getFileURL());
        if (body.isEmpty())
            continue;
        auto entries = parseCommandsXcu(body);
        out.insert(out.end(),
                   std::make_move_iterator(entries.begin()),
                   std::make_move_iterator(entries.end()));
    }
    directory.close();
    return out;
}

} // namespace cui::commandpalette

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
