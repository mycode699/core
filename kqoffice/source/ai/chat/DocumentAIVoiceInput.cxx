/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <DocumentAIVoiceInput.hxx>
#include <NotebookMaterialStore.hxx>

#include <osl/file.hxx>
#include <osl/mutex.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <array>
#include <atomic>
#include <cstdlib>
#include <string>
#include <string_view>

#if !defined(_WIN32)
#include <cstdio>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace kqoffice::ai::chat
{
namespace
{
std::atomic<bool> g_listening{ false };
OUString g_recordPath;
#if !defined(_WIN32)
pid_t g_recordPid = 0;
#endif
osl::Mutex g_voiceMutex;

OUString envOrEmpty(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return OUString();
    return OUString::fromUtf8(v);
}

OUString runCommandCaptureStdout(const OUString& rCmd, sal_Int32 nMaxBytes = 16000)
{
    if (rCmd.isEmpty())
        return OUString();
    const OString cmd = OUStringToOString(rCmd, RTL_TEXTENCODING_UTF8);
#if defined(_WIN32)
    FILE* pipe = _popen(cmd.getStr(), "r");
#else
    FILE* pipe = popen(cmd.getStr(), "r");
#endif
    if (!pipe)
        return OUString();
    std::string buf;
    std::array<char, 512> chunk{};
    while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe))
    {
        buf.append(chunk.data());
        if (static_cast<sal_Int32>(buf.size()) >= nMaxBytes)
            break;
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r' || buf.back() == ' '))
        buf.pop_back();
    if (buf.empty())
        return OUString();
    return OUString::fromUtf8(std::string_view(buf.data(), buf.size()));
}

int runShell(const OUString& rCmd)
{
    const OString cmd = OUStringToOString(rCmd, RTL_TEXTENCODING_UTF8);
    return std::system(cmd.getStr());
}

OUString expandAudioPlaceholder(const OUString& rCmd, const OUString& rAudioPath)
{
    OUString out = rCmd;
    const OUString needle = u"$AUDIO"_ustr;
    sal_Int32 p = out.indexOf(needle);
    while (p >= 0)
    {
        out = out.replaceAt(p, needle.getLength(), rAudioPath);
        p = out.indexOf(needle);
    }
    return out;
}

OUString makeRecordPath()
{
    const char* home = std::getenv("HOME");
    OUString dir = home ? (OUString::fromUtf8(home) + u"/.config/kqoffice/voice"_ustr)
                        : u"/tmp/kqoffice-voice"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) == osl::FileBase::E_None)
        osl::Directory::createPath(url);
    return dir + u"/ptt-last.wav"_ustr;
}

bool startRecording(const DocumentAIInputPrefs& prefs)
{
    g_recordPath = makeRecordPath();
    // Remove old file
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(g_recordPath, url) == osl::FileBase::E_None)
        osl::File::remove(url);

#if defined(MACOSX)
    // Prefer sox `rec`, then ffmpeg avfoundation default mic
    const sal_Int32 sec = prefs.voiceMaxRecordSec;
    OUString cmd;
    if (runShell(u"command -v rec >/dev/null 2>&1"_ustr) == 0)
    {
        cmd = u"rec -q -c 1 -r 16000 \""_ustr + g_recordPath + u"\" trim 0 "_ustr
              + OUString::number(sec) + u" & echo $!"_ustr;
    }
    else if (runShell(u"command -v ffmpeg >/dev/null 2>&1"_ustr) == 0)
    {
        // Background ffmpeg; store pid via shell
        cmd = u"ffmpeg -y -f avfoundation -i \":0\" -ac 1 -ar 16000 -t "_ustr
              + OUString::number(sec) + u" \""_ustr + g_recordPath
              + u"\" >/dev/null 2>&1 & echo $!"_ustr;
    }
    else
        return false;

    const OUString pidStr = runCommandCaptureStdout(cmd).trim();
    if (pidStr.isEmpty())
        return false;
    g_recordPid = static_cast<pid_t>(pidStr.toInt32());
    return g_recordPid > 0;
#elif !defined(_WIN32)
    const sal_Int32 sec = prefs.voiceMaxRecordSec;
    if (runShell(u"command -v arecord >/dev/null 2>&1"_ustr) == 0)
    {
        OUString cmd = u"arecord -q -f S16_LE -r 16000 -c 1 -d "_ustr + OUString::number(sec)
                       + u" \""_ustr + g_recordPath + u"\" & echo $!"_ustr;
        const OUString pidStr = runCommandCaptureStdout(cmd).trim();
        g_recordPid = static_cast<pid_t>(pidStr.toInt32());
        return g_recordPid > 0;
    }
    if (runShell(u"command -v ffmpeg >/dev/null 2>&1"_ustr) == 0)
    {
        OUString cmd = u"ffmpeg -y -f pulse -i default -ac 1 -ar 16000 -t "_ustr
                       + OUString::number(sec) + u" \""_ustr + g_recordPath
                       + u"\" >/dev/null 2>&1 & echo $!"_ustr;
        const OUString pidStr = runCommandCaptureStdout(cmd).trim();
        g_recordPid = static_cast<pid_t>(pidStr.toInt32());
        return g_recordPid > 0;
    }
    return false;
#else
    (void)prefs;
    return false;
#endif
}

void stopRecording()
{
#if !defined(_WIN32)
    if (g_recordPid > 0)
    {
        kill(g_recordPid, SIGINT);
        // Give encoder a moment
        runShell(u"sleep 0.4"_ustr);
        g_recordPid = 0;
    }
#endif
}

bool fileNonEmpty(const OUString& rSysPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    f.close();
    return sz > 44; // wav header-ish
}
} // namespace

bool DocumentAIVoiceInput::isEnabled()
{
    return DocumentAIInputPrefs::load().voiceEnabled;
}

bool DocumentAIVoiceInput::isListening() { return g_listening.load(); }

void DocumentAIVoiceInput::cancelListening()
{
    osl::MutexGuard g(g_voiceMutex);
    if (g_listening.load())
    {
        stopRecording();
        g_listening = false;
    }
}

OUString DocumentAIVoiceInput::statusHint()
{
    const auto p = DocumentAIInputPrefs::load();
    if (!p.voiceEnabled)
        return u"语音：已关闭（设置 → 可圈 AI）"_ustr;
    if (g_listening.load())
    {
        if (p.voiceHoldToTalk)
            return u"🎤 正在听… 松开快捷键结束并出字"_ustr;
        return u"🎤 正在听… 再按 F4 / ⌘⇧空格 结束并出字"_ustr;
    }
    switch (p.voiceBackend)
    {
        case VoiceBackend::LocalCommand:
            return u"语音：本地转写 · F4 / ⌘⇧空格"_ustr;
        case VoiceBackend::PushToTalkRecord:
            if (p.voiceHoldToTalk)
                return u"语音：按住 F4 或 ⌘⇧空格 说话 · 松开出字（本机）"_ustr;
            return u"语音：F4 / ⌘⇧空格 切换录音 · 本机转写"_ustr;
        case VoiceBackend::SystemDictation:
        default:
            if (p.voiceShowFnHint)
                return u"语音：系统听写（Fn）或 F4 本机按住说"_ustr;
            return u"语音：系统听写 / 快捷键本机录音"_ustr;
    }
}

bool DocumentAIVoiceInput::requestSystemDictation()
{
#if defined(MACOSX)
    // Best-effort: open Keyboard dictation settings tip + try to invoke dictation.
    // Double-Fn is a system preference ("Press Fn key to" → Dictation).
    // We focus user guidance; also try Accessibility press if enabled.
    runShell(
        u"osascript -e 'tell application \"System Events\" to key code 63' 2>/dev/null || true"_ustr);
    return true;
#elif defined(_WIN32)
    // Win+H starts voice typing on modern Windows
    runShell(u"powershell -NoProfile -Command "
             u"\"$wshell = New-Object -ComObject wscript.shell; $wshell.SendKeys('^{ESC}'); "
             u"Start-Sleep -Milliseconds 200; $wshell.SendKeys('^+{F12}')\" 2>nul"_ustr);
    // Prefer documented Win+H via shell
    runShell(u"explorer.exe ms-settings:speech"_ustr);
    return true;
#else
    return false;
#endif
}

VoiceCaptureResult DocumentAIVoiceInput::captureOnce()
{
    VoiceCaptureResult out;
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.voiceEnabled)
    {
        out.source = u"disabled"_ustr;
        out.message = u"语音输入已在设置中关闭"_ustr;
        return out;
    }

    const OUString fromEnv = envOrEmpty("KQOFFICE_AI_VOICE_TEXT").trim();
    if (!fromEnv.isEmpty())
    {
        out.success = true;
        out.source = u"env-text"_ustr;
        out.text = fromEnv;
        out.message = u"已从测试环境填入语音文本"_ustr;
        unsetenv("KQOFFICE_AI_VOICE_TEXT");
        return out;
    }

    OUString cmd = prefs.voiceCmd.trim();
    if (cmd.isEmpty())
        cmd = envOrEmpty("KQOFFICE_AI_VOICE_CMD").trim();

    if (prefs.voiceBackend == VoiceBackend::LocalCommand && !cmd.isEmpty()
        && cmd.indexOf(u"$AUDIO"_ustr) < 0)
    {
        const OUString text = runCommandCaptureStdout(cmd).trim();
        if (!text.isEmpty())
        {
            out.success = true;
            out.source = u"voice-cmd"_ustr;
            out.text = text;
            out.message = u"本地语音转写完成 · 未上传云端"_ustr;
            return out;
        }
        out.source = u"voice-cmd"_ustr;
        out.message = u"本地转写命令无输出 — 检查设置中的转写命令"_ustr;
        return out;
    }

    // System dictation path: request OS UI + guide
    requestSystemDictation();
    out.success = false;
    out.source = u"system-dictation"_ustr;
    out.message = u"已请求系统听写。请对着输入框说话："
                  u"macOS 请在「系统设置 → 键盘」开启听写，并设置 Fn 键为听写；"
                  u"Windows 使用 Win+H。也可在可圈 AI 设置中配置本地 STT 命令。"_ustr;
    return out;
}

VoiceCaptureResult DocumentAIVoiceInput::togglePushToTalk()
{
    VoiceCaptureResult out;
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.voiceEnabled)
    {
        out.source = u"disabled"_ustr;
        out.message = u"语音输入已关闭"_ustr;
        return out;
    }

    // Env one-shot always wins
    const OUString fromEnv = envOrEmpty("KQOFFICE_AI_VOICE_TEXT").trim();
    if (!fromEnv.isEmpty() && !g_listening.load())
        return captureOnce();

    OUString cmd = prefs.voiceCmd.trim();
    if (cmd.isEmpty())
        cmd = envOrEmpty("KQOFFICE_AI_VOICE_CMD").trim();

    // Prefer local mic+whisper (or $AUDIO cmd) for world-class offline STT
    const auto asrDiag = kqoffice::ai::notebook::NotebookMaterialStore::diagnoseLocalAsr();
    const bool canLocalAsr = asrDiag.hasWhisper && asrDiag.hasFfmpeg;
    const bool hasAudioCmd = !cmd.isEmpty() && cmd.indexOf(u"$AUDIO"_ustr) >= 0;
    const bool wantRecord = prefs.voiceBackend == VoiceBackend::PushToTalkRecord || hasAudioCmd
                            || canLocalAsr;

    // System dictation only when user chose it and no local record path
    if (!wantRecord && prefs.voiceBackend == VoiceBackend::SystemDictation)
        return captureOnce();

    // Local command without $AUDIO: one-shot
    if (!cmd.isEmpty() && !hasAudioCmd && prefs.voiceBackend != VoiceBackend::PushToTalkRecord
        && !canLocalAsr)
        return captureOnce();

    // No record tools → system dictation guidance
    if (!g_listening.load() && !wantRecord)
        return captureOnce();

    OUString audioPath;
    bool stopping = false;
    {
        osl::MutexGuard g(g_voiceMutex);
        if (!g_listening.load())
        {
            if (!startRecording(prefs))
            {
                out.source = u"error"_ustr;
                out.message = u"无法开始录音（需要 sox/rec 或 ffmpeg）。"
                              u"也可改用系统听写，或配置不依赖 $AUDIO 的转写命令。"_ustr;
                return out;
            }
            g_listening = true;
            out.success = true;
            out.listening = true;
            out.source = u"listening"_ustr;
            out.message = u"正在听… 再点「语音」结束并转写（最长 "_ustr
                          + OUString::number(prefs.voiceMaxRecordSec) + u" 秒）"_ustr;
            return out;
        }
        // Stop + prepare transcribe
        stopRecording();
        g_listening = false;
        audioPath = g_recordPath;
        stopping = true;
    }

    if (!stopping)
        return out;

    out.listening = false;
    out.source = u"push-to-talk"_ustr;
    if (!fileNonEmpty(audioPath))
    {
        out.message = u"未录到有效音频 — 请检查麦克风权限（系统设置 → 隐私与安全性）"_ustr;
        return out;
    }

    OUString text;
    if (!cmd.isEmpty() && cmd.indexOf(u"$AUDIO"_ustr) >= 0)
    {
        const OUString sttCmd = expandAudioPlaceholder(cmd, audioPath);
        text = runCommandCaptureStdout(sttCmd).trim();
    }
    // World-class default: local whisper when no custom STT command
    if (text.isEmpty())
    {
        OUString st;
        text = kqoffice::ai::notebook::NotebookMaterialStore::transcribeLocalMedia(audioPath, st,
                                                                                   300);
        if (!text.isEmpty())
        {
            out.success = true;
            out.text = text;
            out.message = u"语音转写完成（本机 whisper）· 未上传云端"_ustr;
            return out;
        }
        if (!cmd.isEmpty())
        {
            out.message = u"转写无结果 — 检查命令与 $AUDIO，或安装 whisper。"
                          u"录音："_ustr
                          + audioPath + u"\n"_ustr + st;
            return out;
        }
        out.message = st.isEmpty()
                          ? (u"转写无结果。录音："_ustr + audioPath
                             + u"\n可: pip install -U openai-whisper  或配置本地 STT 命令。"_ustr)
                          : st;
        return out;
    }
    out.success = true;
    out.text = text;
    out.message = u"语音转写完成 · 本地处理 · 未上传云端"_ustr;
    return out;
}

VoiceCaptureResult DocumentAIVoiceInput::beginPushToTalk()
{
    VoiceCaptureResult out;
    if (g_listening.load())
    {
        out.success = true;
        out.listening = true;
        out.source = u"listening"_ustr;
        out.message = statusHint();
        return out;
    }
    // Start path of toggle
    const auto r = togglePushToTalk();
    if (r.listening)
        return r;
    // If toggle immediately produced text (env/cmd), pass through
    return r;
}

VoiceCaptureResult DocumentAIVoiceInput::endPushToTalk()
{
    VoiceCaptureResult out;
    if (!g_listening.load())
    {
        out.source = u"idle"_ustr;
        out.message = u"未在录音"_ustr;
        return out;
    }
    // Stop path of toggle
    return togglePushToTalk();
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
