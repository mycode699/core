/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: In-app AI chat).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatHistoryStore.hxx"

#include <sfx2/docfile.hxx>
#include <sfx2/objsh.hxx>
#include <comphelper/hash.hxx>
#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <unotools/pathoptions.hxx>
#include <tools/urlobj.hxx>

#include <string>
#include <string_view>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral HISTORY_DIR_NAME = u"kqoffice-v3-ai-chat-history";
constexpr OUStringLiteral HISTORY_FILE_SUFFIX = u".history";
constexpr sal_uInt64 MAX_HISTORY_BYTES = 1024 * 1024;

OUString EnsureNoTrailingSlash(OUString sUrl)
{
    while (sUrl.endsWith(u"/"))
        sUrl = sUrl.copy(0, sUrl.getLength() - 1);
    return sUrl;
}

OUString EscapeField(const OUString& rValue)
{
    OUStringBuffer aBuffer;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        switch (c)
        {
            case '\\':
                aBuffer.append(u"\\\\"_ustr);
                break;
            case '\n':
                aBuffer.append(u"\\n"_ustr);
                break;
            case '\r':
                aBuffer.append(u"\\r"_ustr);
                break;
            case '\t':
                aBuffer.append(u"\\t"_ustr);
                break;
            default:
                aBuffer.append(c);
                break;
        }
    }
    return aBuffer.makeStringAndClear();
}

OUString UnescapeField(std::u16string_view aValue)
{
    OUStringBuffer aBuffer;
    for (size_t i = 0; i < aValue.size(); ++i)
    {
        if (aValue[i] != u'\\' || i + 1 >= aValue.size())
        {
            aBuffer.append(aValue[i]);
            continue;
        }

        const char16_t cNext = aValue[++i];
        switch (cNext)
        {
            case u'n':
                aBuffer.append(u'\n');
                break;
            case u'r':
                aBuffer.append(u'\r');
                break;
            case u't':
                aBuffer.append(u'\t');
                break;
            case u'\\':
                aBuffer.append(u'\\');
                break;
            default:
                aBuffer.append(cNext);
                break;
        }
    }
    return aBuffer.makeStringAndClear();
}

OUString FormatRecord(const OUString& rSpeaker, const OUString& rMessage)
{
    return EscapeField(rSpeaker) + u"\t"_ustr + EscapeField(rMessage) + u"\n"_ustr;
}

bool ReadFile(const OUString& rUrl, OString& rContent)
{
    osl::File aFile(rUrl);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_HISTORY_BYTES)
    {
        aFile.close();
        return false;
    }

    std::vector<char> aBuffer(static_cast<size_t>(nSize));
    sal_uInt64 nRead = 0;
    if (nSize > 0
        && aFile.read(aBuffer.data(), nSize, nRead) != osl::FileBase::E_None)
    {
        aFile.close();
        return false;
    }
    aFile.close();

    if (nRead != nSize)
        return false;

    rContent = OString(aBuffer.data(), static_cast<sal_Int32>(aBuffer.size()));
    return true;
}

bool WriteFile(const OUString& rUrl, const OString& rContent)
{
    osl::File aFile(rUrl);
    osl::FileBase::RC eError = aFile.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (eError != osl::FileBase::E_None)
        eError = aFile.open(osl_File_OpenFlag_Write);
    if (eError != osl::FileBase::E_None)
        return false;

    aFile.setSize(0);
    sal_uInt64 nWritten = 0;
    const bool bWritten
        = aFile.write(rContent.getStr(), rContent.getLength(), nWritten) == osl::FileBase::E_None
          && nWritten == static_cast<sal_uInt64>(rContent.getLength());
    aFile.close();
    return bWritten;
}
}

AIChatHistoryStore::AIChatHistoryStore()
    : m_sDocumentKey(MakeDocumentHash(ResolveCurrentDocumentIdentity()))
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + HISTORY_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sSidecarUrl = m_sStorageRootUrl + u"/"_ustr + m_sDocumentKey + HISTORY_FILE_SUFFIX;
}

OUString AIChatHistoryStore::ResolveCurrentDocumentIdentity()
{
    if (SfxObjectShell* pShell = SfxObjectShell::Current())
    {
        if (SfxMedium* pMedium = pShell->GetMedium())
        {
            const OUString sUrl
                = pMedium->GetURLObject().GetMainURL(INetURLObject::DecodeMechanism::NONE);
            if (!sUrl.isEmpty())
                return u"document-url:"_ustr + sUrl;
        }

        return u"unsaved-object-shell:"_ustr
               + OUString::number(reinterpret_cast<sal_uInt64>(pShell));
    }

    return u"no-current-document"_ustr;
}

OUString AIChatHistoryStore::MakeDocumentHash(const OUString& rDocumentIdentity)
{
    const OString sIdentity = OUStringToOString(rDocumentIdentity, RTL_TEXTENCODING_UTF8);
    const std::vector<unsigned char> aHash = comphelper::Hash::calculateHash(
        sIdentity.getStr(), sIdentity.getLength(), comphelper::HashType::SHA256);
    return OUString::createFromAscii(comphelper::hashToString(aHash));
}

OUString AIChatHistoryStore::LoadTranscript() const
{
    OString sContent;
    if (!ReadFile(m_sSidecarUrl, sContent) || sContent.isEmpty())
        return OUString();

    const OUString sUtf16 = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    OUStringBuffer aTranscript;

    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sLine = sUtf16.getToken(0, '\n', nIndex);
        if (sLine.isEmpty())
            continue;

        const sal_Int32 nSep = sLine.indexOf('\t');
        if (nSep <= 0)
            continue;

        if (!aTranscript.isEmpty())
            aTranscript.append(u"\n\n"_ustr);

        const OUString sSpeaker = UnescapeField(sLine.subView(0, nSep));
        const OUString sMessage = UnescapeField(sLine.subView(nSep + 1));
        aTranscript.append(sSpeaker + u": "_ustr + sMessage);
    }

    return aTranscript.makeStringAndClear();
}

bool AIChatHistoryStore::AppendMessage(const OUString& rSpeaker, const OUString& rMessage) const
{
    OString sExisting;
    ReadFile(m_sSidecarUrl, sExisting);

    const OUString sRecord = FormatRecord(rSpeaker, rMessage);
    const OString sUtf8Record = OUStringToOString(sRecord, RTL_TEXTENCODING_UTF8);
    return WriteFile(m_sSidecarUrl, sExisting + sUtf8Record);
}

bool AIChatHistoryStore::Clear() const
{
    const osl::FileBase::RC eResult = osl::File::remove(m_sSidecarUrl);
    return eResult == osl::FileBase::E_None || eResult == osl::FileBase::E_NOENT;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
