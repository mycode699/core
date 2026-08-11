/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: provider Slot as data).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ProviderSlotManifest.hxx"

#include "AiPaths.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>
#include <vector>

namespace kqoffice::ai::control
{

namespace
{
OUString jsonStringField(const OUString& json, const OUString& key)
{
    const OUString pat = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = json.indexOf(pat);
    if (p < 0)
        return {};
    p = json.indexOf(u':', p + pat.getLength());
    if (p < 0)
        return {};
    ++p;
    while (p < json.getLength() && (json[p] == ' ' || json[p] == '\t'))
        ++p;
    if (p >= json.getLength() || json[p] != '"')
        return {};
    ++p;
    sal_Int32 end = p;
    while (end < json.getLength() && json[end] != '"')
    {
        if (json[end] == '\\' && end + 1 < json.getLength())
            end += 2;
        else
            ++end;
    }
    if (end > p)
        return json.copy(p, end - p);
    return {};
}

bool jsonBoolField(const OUString& json, const OUString& key, bool def)
{
    const OUString pat = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = json.indexOf(pat);
    if (p < 0)
        return def;
    p = json.indexOf(u':', p + pat.getLength());
    if (p < 0)
        return def;
    const OUString rest = json.copy(p + 1).trim();
    if (rest.startsWith(u"true"_ustr))
        return true;
    if (rest.startsWith(u"false"_ustr))
        return false;
    return def;
}

std::vector<OUString> jsonStringArrayField(const OUString& json, const OUString& key)
{
    std::vector<OUString> out;
    const OUString pat = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = json.indexOf(pat);
    if (p < 0)
        return out;
    p = json.indexOf(u'[', p);
    if (p < 0)
        return out;
    sal_Int32 end = json.indexOf(u']', p);
    if (end < 0)
        return out;
    const OUString arr = json.copy(p + 1, end - p - 1);
    sal_Int32 i = 0;
    while (i < arr.getLength())
    {
        sal_Int32 q1 = arr.indexOf(u'"', i);
        if (q1 < 0)
            break;
        sal_Int32 q2 = arr.indexOf(u'"', q1 + 1);
        if (q2 < 0)
            break;
        out.push_back(arr.copy(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
    return out;
}

bool readFileUtf8(const OUString& systemPath, OUString& out)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    sal_uInt64 size = 0;
    f.getSize(size);
    if (size == 0 || size > 256 * 1024)
    {
        f.close();
        return false;
    }
    std::vector<char> buf(static_cast<size_t>(size) + 1, 0);
    sal_uInt64 n = 0;
    f.read(buf.data(), size, n);
    f.close();
    out = OStringToOUString(OString(buf.data(), static_cast<sal_Int32>(n)), RTL_TEXTENCODING_UTF8);
    return true;
}

void loadDir(const OUString& dir, std::vector<ProviderSlotManifest>& slots)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) != osl::FileBase::E_None)
        return;
    osl::Directory d(url);
    if (d.open() != osl::FileBase::E_None)
        return;
    osl::DirectoryItem item;
    while (d.getNextItem(item) == osl::FileBase::E_None)
    {
        osl::FileStatus st(osl_FileStatus_Mask_FileName | osl_FileStatus_Mask_Type
                           | osl_FileStatus_Mask_FileURL);
        if (item.getFileStatus(st) != osl::FileBase::E_None)
            continue;
        if (st.getFileType() != osl::FileStatus::Regular)
            continue;
        const OUString name = st.getFileName();
        if (!name.endsWithIgnoreAsciiCase(u".json"_ustr))
            continue;
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(st.getFileURL(), sys) != osl::FileBase::E_None)
            continue;
        OUString json;
        if (!readFileUtf8(sys, json))
            continue;
        ProviderSlotManifest m;
        if (!ProviderSlotRegistry::parseOne(json, m))
            continue;
        if (!m.enabled)
            continue;
        // later id replaces earlier
        bool replaced = false;
        for (auto& existing : slots)
        {
            if (existing.id == m.id)
            {
                existing = m;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            slots.push_back(m);
    }
    d.close();
}
} // namespace

bool ProviderSlotRegistry::isValidId(const OUString& id)
{
    if (id.isEmpty() || id.getLength() > 64)
        return false;
    for (sal_Int32 i = 0; i < id.getLength(); ++i)
    {
        const sal_Unicode c = id[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'
                        || (c >= 'A' && c <= 'Z');
        if (!ok)
            return false;
    }
    if (id.indexOf(u"/"_ustr) >= 0 || id.indexOf(u"\\"_ustr) >= 0 || id.indexOf(u".."_ustr) >= 0)
        return false;
    return true;
}

bool ProviderSlotRegistry::parseOne(const OUString& json, ProviderSlotManifest& out)
{
    out = ProviderSlotManifest();
    out.id = jsonStringField(json, u"id"_ustr);
    if (out.id.isEmpty())
        out.id = jsonStringField(json, u"slotId"_ustr);
    if (!isValidId(out.id))
        return false;
    out.displayNameZh = jsonStringField(json, u"displayNameZh"_ustr);
    if (out.displayNameZh.isEmpty())
        out.displayNameZh = jsonStringField(json, u"name"_ustr);
    if (out.displayNameZh.isEmpty())
        out.displayNameZh = out.id;
    out.backend = jsonStringField(json, u"backend"_ustr);
    if (out.backend.isEmpty())
        out.backend = u"openai-compatible"_ustr;
    out.baseUrl = jsonStringField(json, u"baseUrl"_ustr);
    out.healthPath = jsonStringField(json, u"healthPath"_ustr);
    out.allowNetwork = jsonBoolField(json, u"allowNetwork"_ustr, true);
    out.enabled = jsonBoolField(json, u"enabled"_ustr, true);
    out.aliases = jsonStringArrayField(json, u"aliases"_ustr);
    return true;
}

std::vector<ProviderSlotManifest> ProviderSlotRegistry::builtinDefaults()
{
    std::vector<ProviderSlotManifest> v;
    {
        ProviderSlotManifest m;
        m.id = u"ollama-local"_ustr;
        m.displayNameZh = u"本地 Ollama"_ustr;
        m.backend = u"ollama"_ustr;
        m.baseUrl = u"http://127.0.0.1:11434"_ustr;
        m.healthPath = u"/api/tags"_ustr;
        m.allowNetwork = true;
        m.aliases = { u"ollama"_ustr, u"local"_ustr };
        v.push_back(m);
    }
    {
        ProviderSlotManifest m;
        m.id = u"openai-compatible"_ustr;
        m.displayNameZh = u"OpenAI 兼容网关"_ustr;
        m.backend = u"openai-compatible"_ustr;
        m.baseUrl = u""_ustr; // from routing config / env
        m.healthPath = u"/models"_ustr;
        m.allowNetwork = true;
        m.aliases = { u"openai"_ustr, u"gateway"_ustr };
        v.push_back(m);
    }
    {
        ProviderSlotManifest m;
        m.id = u"membership"_ustr;
        m.displayNameZh = u"可圈会员网关"_ustr;
        m.backend = u"membership"_ustr;
        m.baseUrl = u"https://api.03122.com"_ustr;
        m.healthPath = u""_ustr;
        m.allowNetwork = true;
        m.aliases = { u"kq"_ustr, u"03122"_ustr };
        v.push_back(m);
    }
    return v;
}

OUString ProviderSlotRegistry::overrideDir()
{
    const char* env = std::getenv("KQOFFICE_AI_PROVIDER_SLOTS_DIR");
    if (env && *env)
        return OUString::createFromAscii(env);
    return kqofficePathJoin(kqofficeAiConfigDir(), u"provider-slots"_ustr);
}

std::vector<ProviderSlotManifest> ProviderSlotRegistry::loadAll()
{
    auto slots = builtinDefaults();
    loadDir(overrideDir(), slots);
    return slots;
}

const ProviderSlotManifest* ProviderSlotRegistry::find(const std::vector<ProviderSlotManifest>& slots,
                                                       const OUString& idOrAlias)
{
    if (idOrAlias.isEmpty())
        return nullptr;
    const OUString key = idOrAlias.toAsciiLowerCase();
    for (const auto& s : slots)
    {
        if (s.id.toAsciiLowerCase() == key)
            return &s;
        for (const auto& a : s.aliases)
        {
            if (a.toAsciiLowerCase() == key)
                return &s;
        }
    }
    return nullptr;
}

OUString ProviderSlotRegistry::summaryLineZh(const std::vector<ProviderSlotManifest>& slots)
{
    OUStringBuffer b;
    b.append(u"Provider Slot · "_ustr);
    b.append(OUString::number(static_cast<sal_Int32>(slots.size())));
    b.append(u" 个"_ustr);
    sal_Int32 n = 0;
    for (const auto& s : slots)
    {
        if (n >= 4)
        {
            b.append(u" …"_ustr);
            break;
        }
        b.append(n == 0 ? u"： "_ustr : u", "_ustr);
        b.append(s.id);
        ++n;
    }
    return b.makeStringAndClear();
}

bool ProviderSlotRegistry::ensureOverrideTemplate()
{
    const OUString dir = overrideDir();
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) != osl::FileBase::E_None)
        return false;
    (void)osl::Directory::createPath(url);
    const OUString readme = kqofficePathJoin(dir, u"README.txt"_ustr);
    OUString existing;
    if (readFileUtf8(readme, existing))
        return true;
    const OUString body
        = u"# 可圈 Provider Slot 覆盖目录\n"
          "# 每个 .json 一个引擎，后加载的同 id 覆盖内置。\n"
          "# 示例 example-local.json:\n"
          "# {\n"
          "#   \"id\": \"my-gateway\",\n"
          "#   \"displayNameZh\": \"我的网关\",\n"
          "#   \"backend\": \"openai-compatible\",\n"
          "#   \"baseUrl\": \"http://127.0.0.1:8080/v1\",\n"
          "#   \"aliases\": [\"mine\"],\n"
          "#   \"allowNetwork\": true\n"
          "# }\n"_ustr;
    OUString fileUrl;
    if (osl::FileBase::getFileURLFromSystemPath(readme, fileUrl) != osl::FileBase::E_None)
        return false;
    osl::File f(fileUrl);
    if (f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create) != osl::FileBase::E_None)
        return false;
    const OString utf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);
    sal_uInt64 w = 0;
    f.write(utf8.getStr(), static_cast<sal_uInt64>(utf8.getLength()), w);
    f.close();
    return true;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
