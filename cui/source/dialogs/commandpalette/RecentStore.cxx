/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2 Day-1a: Recent Store).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * loadFromUser / saveToUser only — pure parser and serializer are
 * header-only inside RecentStore.hxx so cppunit links without libcui.
 */

#include <commandpalette/RecentStore.hxx>

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <fstream>
#include <sstream>
#include <string>

namespace cui::commandpalette
{
namespace
{
constexpr OUStringLiteral kRecentSubdir = u"/cmdpalette";
constexpr OUStringLiteral kRecentBasename = u"/recent.json";

OUString joinUrl(const OUString& base, const OUString& tail)
{
    if (base.endsWith("/"))
        return base + tail.copy(1);
    return base + tail;
}

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

bool writeFileUrl(const OUString& fileUrl, const OString& body)
{
    OUString sysPath;
    if (osl::FileBase::getSystemPathFromFileURL(fileUrl, sysPath)
        != osl::FileBase::E_None)
    {
        return false;
    }
    OString sysPathUtf8 = OUStringToOString(sysPath,
                                            RTL_TEXTENCODING_UTF8);
    std::ofstream out(sysPathUtf8.getStr(),
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(body.getStr(), body.getLength());
    return out.good();
}
} // namespace

std::vector<RecentEntry> RecentStore::loadFromUser(
    const OUString& userInstallation)
{
    OUString dir = joinUrl(userInstallation, kRecentSubdir);
    OUString file = joinUrl(dir, kRecentBasename);
    OString body = readFileUrl(file);
    if (body.isEmpty())
        return {};
    return parseRecentJson(body);
}

bool RecentStore::saveToUser(const OUString& userInstallation,
                             const std::vector<RecentEntry>& entries)
{
    OUString dir = joinUrl(userInstallation, kRecentSubdir);
    osl::FileBase::RC rc = osl::Directory::createPath(dir);
    if (rc != osl::FileBase::E_None && rc != osl::FileBase::E_EXIST)
        return false;
    OUString file = joinUrl(dir, kRecentBasename);
    return writeFileUrl(file, serializeRecentJson(entries));
}

} // namespace cui::commandpalette

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
