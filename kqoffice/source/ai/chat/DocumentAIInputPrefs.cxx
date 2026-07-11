/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <DocumentAIInputPrefs.hxx>

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cstdlib>
#include <string>
#include <string_view>

namespace kqoffice::ai::chat
{
namespace
{
OUString envOrEmpty(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return OUString();
    return OUString::fromUtf8(v);
}

bool writeFileUtf8(const OUString& rSysPath, const std::string& body)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    osl::FileBase::RC e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    sal_uInt64 n = 0;
    const bool ok = f.write(body.data(), body.size(), n) == osl::FileBase::E_None
                    && n == body.size();
    f.close();
    return ok;
}

OUString readFileUtf8(const OUString& rSysPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return OUString();
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return OUString();
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 256 * 1024)
    {
        f.close();
        return OUString();
    }
    std::string buf(static_cast<size_t>(sz), '\0');
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    if (n != sz)
        return OUString();
    return OUString::fromUtf8(std::string_view(buf.data(), buf.size()));
}

OUString jsonStringField(const OUString& frag, const OUString& key)
{
    const OUString needle = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = frag.indexOf(needle);
    if (p < 0)
        return OUString();
    p = frag.indexOf(u':', p);
    if (p < 0)
        return OUString();
    sal_Int32 q1 = frag.indexOf(u'"', p + 1);
    if (q1 < 0)
        return OUString();
    sal_Int32 q2 = q1 + 1;
    while (q2 < frag.getLength())
    {
        if (frag[q2] == u'\\' && q2 + 1 < frag.getLength())
        {
            q2 += 2;
            continue;
        }
        if (frag[q2] == u'"')
            break;
        ++q2;
    }
    if (q2 >= frag.getLength())
        return OUString();
    return frag.copy(q1 + 1, q2 - q1 - 1);
}

bool jsonBoolField(const OUString& frag, const OUString& key, bool def)
{
    const OUString needle = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = frag.indexOf(needle);
    if (p < 0)
        return def;
    p = frag.indexOf(u':', p);
    if (p < 0)
        return def;
    const OUString rest = frag.copy(p + 1);
    if (rest.indexOf(u"true"_ustr) >= 0 && rest.indexOf(u"true"_ustr) < 12)
        return true;
    if (rest.indexOf(u"false"_ustr) >= 0 && rest.indexOf(u"false"_ustr) < 12)
        return false;
    return def;
}

sal_Int32 jsonIntField(const OUString& frag, const OUString& key, sal_Int32 def)
{
    const OUString s = jsonStringField(frag, key);
    if (!s.isEmpty())
    {
        // also accept bare number after key — simple digit scan
    }
    const OUString needle = u"\""_ustr + key + u"\""_ustr;
    sal_Int32 p = frag.indexOf(needle);
    if (p < 0)
        return def;
    p = frag.indexOf(u':', p);
    if (p < 0)
        return def;
    sal_Int32 i = p + 1;
    while (i < frag.getLength() && (frag[i] == u' ' || frag[i] == u'\t'))
        ++i;
    sal_Int32 j = i;
    if (j < frag.getLength() && frag[j] == u'-')
        ++j;
    while (j < frag.getLength() && frag[j] >= u'0' && frag[j] <= u'9')
        ++j;
    if (j == i || (j == i + 1 && frag[i] == u'-'))
        return def;
    return frag.copy(i, j - i).toInt32();
}

void appendJsonString(OUStringBuffer& b, const OUString& key, const OUString& val)
{
    b.append(u"  \""_ustr);
    b.append(key);
    b.append(u"\": \""_ustr);
    for (sal_Int32 i = 0; i < val.getLength(); ++i)
    {
        const sal_Unicode c = val[i];
        if (c == u'\\' || c == u'"')
            b.append(u'\\');
        if (c == u'\n')
        {
            b.append(u"\\n"_ustr);
            continue;
        }
        b.append(c);
    }
    b.append(u"\""_ustr);
}

void appendJsonBool(OUStringBuffer& b, const OUString& key, bool v)
{
    b.append(u"  \""_ustr);
    b.append(key);
    b.append(u"\": "_ustr);
    b.append(v ? u"true"_ustr : u"false"_ustr);
}

void appendJsonInt(OUStringBuffer& b, const OUString& key, sal_Int32 v)
{
    b.append(u"  \""_ustr);
    b.append(key);
    b.append(u"\": "_ustr);
    b.append(v);
}
} // namespace

OUString DocumentAIInputPrefs::defaultConfigPath()
{
    OUString overridePath = envOrEmpty("KQOFFICE_AI_INPUT_PREFS");
    if (!overridePath.isEmpty())
        return overridePath;
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return OUString();
    return OUString::fromUtf8(home) + u"/.config/kqoffice/ai-input-prefs.json"_ustr;
}

OUString DocumentAIInputPrefs::voiceBackendToString(VoiceBackend e)
{
    switch (e)
    {
        case VoiceBackend::LocalCommand:
            return u"local-cmd"_ustr;
        case VoiceBackend::PushToTalkRecord:
            return u"push-to-talk"_ustr;
        case VoiceBackend::SystemDictation:
        default:
            return u"system-dictation"_ustr;
    }
}

VoiceBackend DocumentAIInputPrefs::voiceBackendFromString(const OUString& s)
{
    const OUString t = s.trim().toAsciiLowerCase();
    if (t == u"local-cmd"_ustr || t == u"local"_ustr || t == u"cmd"_ustr)
        return VoiceBackend::LocalCommand;
    if (t == u"push-to-talk"_ustr || t == u"ptt"_ustr || t == u"record"_ustr)
        return VoiceBackend::PushToTalkRecord;
    return VoiceBackend::SystemDictation;
}

OUString DocumentAIInputPrefs::screenshotModeToString(ScreenshotMode e)
{
    switch (e)
    {
        case ScreenshotMode::Window:
            return u"window"_ustr;
        case ScreenshotMode::Fullscreen:
            return u"fullscreen"_ustr;
        case ScreenshotMode::Region:
        default:
            return u"region"_ustr;
    }
}

ScreenshotMode DocumentAIInputPrefs::screenshotModeFromString(const OUString& s)
{
    const OUString t = s.trim().toAsciiLowerCase();
    if (t == u"window"_ustr)
        return ScreenshotMode::Window;
    if (t == u"fullscreen"_ustr || t == u"full"_ustr)
        return ScreenshotMode::Fullscreen;
    return ScreenshotMode::Region;
}

OUString DocumentAIInputPrefs::resolveCaptureDir(const DocumentAIInputPrefs& r)
{
    if (!r.screenshotDir.isEmpty())
        return r.screenshotDir;
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return u"/tmp/kqoffice-captures"_ustr;
    return OUString::fromUtf8(home) + u"/.config/kqoffice/captures"_ustr;
}

DocumentAIInputPrefs DocumentAIInputPrefs::load()
{
    DocumentAIInputPrefs p;
    // Env overrides for automation
    const OUString envCmd = envOrEmpty("KQOFFICE_AI_VOICE_CMD");
    if (!envCmd.isEmpty())
    {
        p.voiceCmd = envCmd;
        p.voiceBackend = VoiceBackend::LocalCommand;
    }
    const OUString path = defaultConfigPath();
    if (path.isEmpty())
        return p;
    const OUString body = readFileUtf8(path);
    if (body.isEmpty())
        return p;

    p.voiceEnabled = jsonBoolField(body, u"voiceEnabled"_ustr, p.voiceEnabled);
    p.voiceBackend = voiceBackendFromString(jsonStringField(body, u"voiceBackend"_ustr));
    const OUString cmd = jsonStringField(body, u"voiceCmd"_ustr);
    if (!cmd.isEmpty())
        p.voiceCmd = cmd;
    p.voicePushToTalk = jsonBoolField(body, u"voicePushToTalk"_ustr, p.voicePushToTalk);
    p.voiceAutoSend = jsonBoolField(body, u"voiceAutoSend"_ustr, false);
    p.voiceShowFnHint = jsonBoolField(body, u"voiceShowFnHint"_ustr, p.voiceShowFnHint);
    p.voiceMaxRecordSec = jsonIntField(body, u"voiceMaxRecordSec"_ustr, p.voiceMaxRecordSec);
    if (p.voiceMaxRecordSec < 5)
        p.voiceMaxRecordSec = 5;
    if (p.voiceMaxRecordSec > 180)
        p.voiceMaxRecordSec = 180;

    p.screenshotEnabled = jsonBoolField(body, u"screenshotEnabled"_ustr, p.screenshotEnabled);
    p.screenshotMode = screenshotModeFromString(jsonStringField(body, u"screenshotMode"_ustr));
    p.screenshotAutoAttachChat
        = jsonBoolField(body, u"screenshotAutoAttachChat"_ustr, p.screenshotAutoAttachChat);
    p.screenshotOpenAiPanel
        = jsonBoolField(body, u"screenshotOpenAiPanel"_ustr, p.screenshotOpenAiPanel);
    p.screenshotCopyClipboard
        = jsonBoolField(body, u"screenshotCopyClipboard"_ustr, p.screenshotCopyClipboard);
    const OUString dir = jsonStringField(body, u"screenshotDir"_ustr);
    if (!dir.isEmpty())
        p.screenshotDir = dir;

    // Env still wins for voice cmd if set
    if (!envCmd.isEmpty())
        p.voiceCmd = envCmd;
    return p;
}

bool DocumentAIInputPrefs::save(const DocumentAIInputPrefs& r)
{
    const OUString path = defaultConfigPath();
    if (path.isEmpty())
        return false;
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(path.copy(0, slash), dirUrl)
            == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    OUStringBuffer b;
    b.append(u"{\n"_ustr);
    b.append(u"  \"schema_version\": \"v1-input-prefs\",\n"_ustr);
    appendJsonBool(b, u"voiceEnabled"_ustr, r.voiceEnabled);
    b.append(u",\n"_ustr);
    appendJsonString(b, u"voiceBackend"_ustr, voiceBackendToString(r.voiceBackend));
    b.append(u",\n"_ustr);
    appendJsonString(b, u"voiceCmd"_ustr, r.voiceCmd);
    b.append(u",\n"_ustr);
    appendJsonBool(b, u"voicePushToTalk"_ustr, r.voicePushToTalk);
    b.append(u",\n"_ustr);
    appendJsonBool(b, u"voiceAutoSend"_ustr, r.voiceAutoSend);
    b.append(u",\n"_ustr);
    appendJsonBool(b, u"voiceShowFnHint"_ustr, r.voiceShowFnHint);
    b.append(u",\n"_ustr);
    appendJsonInt(b, u"voiceMaxRecordSec"_ustr, r.voiceMaxRecordSec);
    b.append(u",\n"_ustr);
    appendJsonBool(b, u"screenshotEnabled"_ustr, r.screenshotEnabled);
    b.append(u",\n"_ustr);
    appendJsonString(b, u"screenshotMode"_ustr, screenshotModeToString(r.screenshotMode));
    b.append(u",\n"_ustr);
    appendJsonBool(b, u"screenshotAutoAttachChat"_ustr, r.screenshotAutoAttachChat);
    b.append(u",\n"_ustr);
    appendJsonBool(b, u"screenshotOpenAiPanel"_ustr, r.screenshotOpenAiPanel);
    b.append(u",\n"_ustr);
    appendJsonBool(b, u"screenshotCopyClipboard"_ustr, r.screenshotCopyClipboard);
    b.append(u",\n"_ustr);
    appendJsonString(b, u"screenshotDir"_ustr, r.screenshotDir);
    b.append(u"\n}\n"_ustr);
    const OString utf8 = OUStringToOString(b.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    return writeFileUtf8(path, std::string(utf8.getStr(), static_cast<size_t>(utf8.getLength())));
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
