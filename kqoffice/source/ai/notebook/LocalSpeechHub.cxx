/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "LocalSpeechHub.hxx"
#include "MediaTranscriptService.hxx"
#include "NotebookMaterialStore.hxx"

#include <osl/file.hxx>
#include <osl/mutex.hxx>
#include <osl/process.h>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <cstdio>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace kqoffice::ai::notebook
{
namespace
{
osl::Mutex& speechMutex()
{
    static osl::Mutex a;
    return a;
}

std::atomic<bool> g_meeting{ false };
OUString g_meetingPath;
#if !defined(_WIN32)
pid_t g_meetingPid = 0;
#endif

// —— Live STT (边录边出字) ——
std::atomic<bool> g_live{ false };
OUString g_liveDir;
OUString g_liveChunks;
sal_Int32 g_liveNext = 0; ///< next chunk index to STT
OUStringBuffer g_liveText;
#if !defined(_WIN32)
pid_t g_livePid = 0;
#endif

OUString runCapture(const OUString& rCmd, sal_Int32 nMax = 8000)
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
        if (static_cast<sal_Int32>(buf.size()) >= nMax)
            break;
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r'))
        buf.pop_back();
    return OUString::fromUtf8(std::string_view(buf.data(), buf.size()));
}

int runShell(const OUString& rCmd)
{
    return std::system(OUStringToOString(rCmd, RTL_TEXTENCODING_UTF8).getStr());
}

bool pathExists(const OUString& p)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(p, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    return osl::DirectoryItem::get(url, item) == osl::FileBase::E_None;
}

OUString shellQuote(const OUString& s)
{
    OUStringBuffer b;
    b.append(u'\'');
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        if (s[i] == u'\'')
            b.append(u"'\\''"_ustr);
        else
            b.append(s[i]);
    }
    b.append(u'\'');
    return b.makeStringAndClear();
}

bool writeSys(const OUString& path, const OUString& content)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString utf8 = OUStringToOString(content, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
    return true;
}

bool fileNonEmpty(const OUString& path)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    f.close();
    return sz > 44;
}

sal_Int32 maxChunkIndex(const OUString& rChunksDir)
{
    sal_Int32 max = -1;
    for (sal_Int32 i = 0; i < 5000; ++i)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "c_%03d.wav", static_cast<int>(i));
        const OUString p = rChunksDir + u"/"_ustr + OUString::fromUtf8(buf);
        if (!pathExists(p))
            break;
        max = i;
    }
    return max;
}

OUString chunkPath(const OUString& rChunksDir, sal_Int32 idx)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "c_%03d.wav", static_cast<int>(idx));
    return rChunksDir + u"/"_ustr + OUString::fromUtf8(buf);
}

OUString defaultVoice()
{
#if defined(MACOSX) || defined(__APPLE__)
    return u"Ting-Ting"_ustr;
#else
    return OUString();
#endif
}
} // namespace

OUString LocalSpeechHub::meetingsDir()
{
    return NotebookMaterialStore::rootDir() + u"/meetings"_ustr;
}

SpeechCapability LocalSpeechHub::diagnose()
{
    SpeechCapability c;
    const bool ffmpeg = pathExists(u"/opt/homebrew/bin/ffmpeg"_ustr)
                        || pathExists(u"/usr/local/bin/ffmpeg"_ustr)
                        || pathExists(u"/usr/bin/ffmpeg"_ustr)
                        || runShell(u"command -v ffmpeg >/dev/null 2>&1"_ustr) == 0;
    const bool rec = runShell(u"command -v rec >/dev/null 2>&1"_ustr) == 0
                     || runShell(u"command -v arecord >/dev/null 2>&1"_ustr) == 0;
    c.hasMicRecord = ffmpeg || rec;
#if defined(MACOSX) || defined(__APPLE__)
    c.hasTts = pathExists(u"/usr/bin/say"_ustr);
#else
    c.hasTts = runShell(u"command -v espeak >/dev/null 2>&1"_ustr) == 0
               || runShell(u"command -v spd-say >/dev/null 2>&1"_ustr) == 0;
#endif
    const auto asr = NotebookMaterialStore::diagnoseLocalAsr();
    c.hasStt = asr.hasWhisper;
    // also env voice cmd counts as STT
    const char* vc = std::getenv("KQOFFICE_AI_VOICE_CMD");
    if (vc && *vc)
        c.hasStt = true;

    OUStringBuffer sum;
    sum.append(u"麦克风录音: "_ustr);
    sum.append(c.hasMicRecord ? u"就绪"_ustr : u"需 ffmpeg/rec"_ustr);
    sum.append(u" · 语音转写: "_ustr);
    sum.append(c.hasStt ? u"就绪"_ustr : u"需 whisper"_ustr);
    sum.append(u" · 朗读: "_ustr);
    sum.append(c.hasTts ? u"就绪"_ustr : u"未就绪"_ustr);
    c.summary = sum.makeStringAndClear();

    if (!c.hasMicRecord || !c.hasStt)
    {
        OUStringBuffer h;
        if (!c.hasMicRecord)
            h.append(u"录音: brew install ffmpeg\n"_ustr);
        if (!c.hasStt)
            h.append(u"转写: pip install -U openai-whisper\n"_ustr);
        h.append(u"全部本机处理，不上传云端。"_ustr);
        c.installHint = h.makeStringAndClear();
    }
    return c;
}

bool LocalSpeechHub::isMeetingRecording() { return g_meeting.load(); }

OUString LocalSpeechHub::meetingStatusHint()
{
    if (g_meeting.load())
        return u"会议录音中… 再点「会议记录」结束并转写"_ustr;
    return u"会议记录：点按开始，再点结束（本地麦克风 · 可超长）"_ustr;
}

bool LocalSpeechHub::startMeeting(OUString& rStatusOut, sal_Int32 nMaxSec)
{
    osl::MutexGuard g(speechMutex());
    if (g_meeting.load())
    {
        rStatusOut = u"会议已在录音中"_ustr;
        return false;
    }
    if (nMaxSec < 60)
        nMaxSec = 60;
    if (nMaxSec > 4 * 3600)
        nMaxSec = 4 * 3600;

    const OUString dir = meetingsDir();
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) == osl::FileBase::E_None)
        osl::Directory::createPath(url);

    // timestamped file
    TimeValue tv{};
    osl_getSystemTime(&tv);
    g_meetingPath = dir + u"/meeting-"_ustr + OUString::number(static_cast<sal_Int64>(tv.Seconds))
                    + u".wav"_ustr;
    if (pathExists(g_meetingPath))
    {
        OUString u2;
        if (osl::FileBase::getFileURLFromSystemPath(g_meetingPath, u2) == osl::FileBase::E_None)
            osl::File::remove(u2);
    }

#if defined(MACOSX) || defined(__APPLE__)
    OUString cmd;
    if (runShell(u"command -v rec >/dev/null 2>&1"_ustr) == 0)
    {
        cmd = u"rec -q -c 1 -r 16000 "_ustr + shellQuote(g_meetingPath) + u" trim 0 "_ustr
              + OUString::number(nMaxSec) + u" >/dev/null 2>&1 & echo $!"_ustr;
    }
    else if (runShell(u"command -v ffmpeg >/dev/null 2>&1"_ustr) == 0)
    {
        cmd = u"ffmpeg -y -f avfoundation -i \":0\" -ac 1 -ar 16000 -t "_ustr
              + OUString::number(nMaxSec) + u" "_ustr + shellQuote(g_meetingPath)
              + u" >/dev/null 2>&1 & echo $!"_ustr;
    }
    else
    {
        rStatusOut = u"无法录音：请安装 ffmpeg（brew install ffmpeg）或 sox"_ustr;
        return false;
    }
    const OUString pidStr = runCapture(cmd).trim();
    g_meetingPid = static_cast<pid_t>(pidStr.toInt32());
    if (g_meetingPid <= 0)
    {
        rStatusOut = u"启动会议录音失败（检查麦克风权限）"_ustr;
        return false;
    }
    g_meeting = true;
    rStatusOut = u"会议录音已开始（最长 "_ustr + OUString::number(nMaxSec / 60)
                 + u" 分钟）。再点一次「会议记录」结束并转写。本机处理，不上传。"_ustr;
    return true;
#elif !defined(_WIN32)
    OUString cmd;
    if (runShell(u"command -v arecord >/dev/null 2>&1"_ustr) == 0)
    {
        cmd = u"arecord -q -f S16_LE -r 16000 -c 1 -d "_ustr + OUString::number(nMaxSec) + u" "
              + shellQuote(g_meetingPath) + u" >/dev/null 2>&1 & echo $!"_ustr;
    }
    else if (runShell(u"command -v ffmpeg >/dev/null 2>&1"_ustr) == 0)
    {
        cmd = u"ffmpeg -y -f pulse -i default -ac 1 -ar 16000 -t "_ustr + OUString::number(nMaxSec)
              + u" " + shellQuote(g_meetingPath) + u" >/dev/null 2>&1 & echo $!"_ustr;
    }
    else
    {
        rStatusOut = u"无法录音：请安装 ffmpeg 或 arecord"_ustr;
        return false;
    }
    const OUString pidStr = runCapture(cmd).trim();
    g_meetingPid = static_cast<pid_t>(pidStr.toInt32());
    if (g_meetingPid <= 0)
    {
        rStatusOut = u"启动会议录音失败"_ustr;
        return false;
    }
    g_meeting = true;
    rStatusOut = u"会议录音已开始。再点结束。"_ustr;
    return true;
#else
    rStatusOut = u"当前平台会议录音待接入"_ustr;
    return false;
#endif
}

bool LocalSpeechHub::stopMeeting(OUString& rAudioPath, OUString& rStatusOut)
{
    osl::MutexGuard g(speechMutex());
    if (!g_meeting.load())
    {
        rStatusOut = u"当前没有进行中的会议录音"_ustr;
        return false;
    }
#if !defined(_WIN32)
    if (g_meetingPid > 0)
    {
        kill(g_meetingPid, SIGINT);
        runShell(u"sleep 0.5"_ustr);
        g_meetingPid = 0;
    }
#endif
    g_meeting = false;
    rAudioPath = g_meetingPath;
    if (!fileNonEmpty(rAudioPath))
    {
        rStatusOut = u"未录到有效音频 — 请检查系统麦克风权限"_ustr;
        return false;
    }
    rStatusOut = u"会议录音已结束："_ustr + rAudioPath;
    return true;
}

void LocalSpeechHub::cancelMeeting()
{
    OUString path, st;
    if (g_live.load())
    {
        OUString full;
        stopLiveMeeting(path, full, st);
    }
    else if (g_meeting.load())
        stopMeeting(path, st);
}

bool LocalSpeechHub::isLiveMeeting() { return g_live.load(); }

OUString LocalSpeechHub::liveSessionDir() { return g_liveDir; }

bool LocalSpeechHub::startLiveMeeting(OUString& rStatusOut, sal_Int32 nChunkSec, sal_Int32 nMaxSec)
{
    osl::MutexGuard g(speechMutex());
    if (g_live.load() || g_meeting.load())
    {
        rStatusOut = u"已有录音进行中"_ustr;
        return false;
    }
    if (nChunkSec < 4)
        nChunkSec = 4;
    if (nChunkSec > 30)
        nChunkSec = 30;
    if (nMaxSec < 60)
        nMaxSec = 60;
    if (nMaxSec > 4 * 3600)
        nMaxSec = 4 * 3600;

    if (!MediaTranscriptService::hasFfmpeg())
    {
        rStatusOut = u"边录边出字需要 ffmpeg：brew install ffmpeg"_ustr;
        return false;
    }
    const auto asr = NotebookMaterialStore::diagnoseLocalAsr();
    if (!asr.hasWhisper)
    {
        rStatusOut = u"边录边出字需要本机 whisper：pip install -U openai-whisper"_ustr;
        return false;
    }

    TimeValue tv{};
    osl_getSystemTime(&tv);
    g_liveDir = meetingsDir() + u"/live-"_ustr
                + OUString::number(static_cast<sal_Int64>(tv.Seconds));
    g_liveChunks = g_liveDir + u"/chunks"_ustr;
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(g_liveChunks, url) == osl::FileBase::E_None)
            osl::Directory::createPath(url);
    }
    g_liveNext = 0;
    g_liveText.setLength(0);

#if defined(MACOSX) || defined(__APPLE__)
    OUString cmd = u"ffmpeg -y -f avfoundation -i \":0\" -ac 1 -ar 16000 -t "_ustr
                   + OUString::number(nMaxSec) + u" -f segment -segment_time "_ustr
                   + OUString::number(nChunkSec) + u" -reset_timestamps 1 "_ustr
                   + shellQuote(g_liveChunks + u"/c_%03d.wav"_ustr)
                   + u" >/dev/null 2>&1 & echo $!"_ustr;
    const OUString pidStr = runCapture(cmd).trim();
    g_livePid = static_cast<pid_t>(pidStr.toInt32());
    if (g_livePid <= 0)
    {
        rStatusOut = u"无法启动边录边出字（检查麦克风权限）"_ustr;
        return false;
    }
#elif !defined(_WIN32)
    OUString cmd = u"ffmpeg -y -f pulse -i default -ac 1 -ar 16000 -t "_ustr
                   + OUString::number(nMaxSec) + u" -f segment -segment_time "_ustr
                   + OUString::number(nChunkSec) + u" -reset_timestamps 1 "_ustr
                   + shellQuote(g_liveChunks + u"/c_%03d.wav"_ustr)
                   + u" >/dev/null 2>&1 & echo $!"_ustr;
    const OUString pidStr = runCapture(cmd).trim();
    g_livePid = static_cast<pid_t>(pidStr.toInt32());
    if (g_livePid <= 0)
    {
        rStatusOut = u"无法启动边录边出字"_ustr;
        return false;
    }
#else
    rStatusOut = u"当前平台边录边出字待接入"_ustr;
    return false;
#endif
    g_live = true;
    g_meeting = true;
    g_meetingPath = g_liveDir + u"/meeting-full.wav"_ustr;
    rStatusOut = u"边录边出字已开始（每约 "_ustr + OUString::number(nChunkSec)
                 + u" 秒出一段）。请对着麦克风说话，转写编辑区会陆续出字。"
                   u"再点「会议」结束。本机处理，不上传。"_ustr;
    return true;
}

OUString LocalSpeechHub::pollLiveDelta(OUString& rStatusOut)
{
    rStatusOut.clear();
    if (!g_live.load())
        return OUString();

    std::vector<OUString> jobs;
    sal_Int32 maxIdx = -1;
    {
        osl::MutexGuard g(speechMutex());
        if (!g_live.load())
            return OUString();
        maxIdx = maxChunkIndex(g_liveChunks);
        // Keep last segment free (still being written by ffmpeg)
        const sal_Int32 lastComplete = maxIdx - 1;
        while (g_liveNext <= lastComplete)
        {
            const OUString path = chunkPath(g_liveChunks, g_liveNext);
            ++g_liveNext;
            if (fileNonEmpty(path))
                jobs.push_back(path);
        }
        rStatusOut = u"边录边出字中 · 已出 "_ustr + OUString::number(g_liveNext) + u" 段"
                     u"（缓冲块 "_ustr
                     + OUString::number(maxIdx + 1) + u"）"_ustr;
    }

    OUStringBuffer dbuf;
    for (const auto& path : jobs)
    {
        OUString st;
        const OUString text
            = NotebookMaterialStore::transcribeLocalMedia(path, st, 120, std::function<void()>());
        if (text.isEmpty())
            continue;
        const OUString t = text.trim();
        dbuf.append(t);
        dbuf.append(u'\n');
        {
            osl::MutexGuard g(speechMutex());
            g_liveText.append(t);
            g_liveText.append(u'\n');
        }
    }
    return dbuf.makeStringAndClear();
}

bool LocalSpeechHub::stopLiveMeeting(OUString& rAudioPath, OUString& rFullText, OUString& rStatusOut)
{
    if (!g_live.load())
    {
        rStatusOut = u"当前没有边录边出字会话"_ustr;
        return false;
    }
#if !defined(_WIN32)
    {
        osl::MutexGuard g(speechMutex());
        if (g_livePid > 0)
        {
            kill(g_livePid, SIGINT);
            runShell(u"sleep 0.7"_ustr);
            g_livePid = 0;
        }
    }
#endif

    // Drain remaining chunks including the last one
    std::vector<OUString> tailJobs;
    std::vector<OUString> allChunks;
    {
        osl::MutexGuard g(speechMutex());
        const sal_Int32 maxIdx = maxChunkIndex(g_liveChunks);
        for (sal_Int32 i = 0; i <= maxIdx; ++i)
        {
            const OUString path = chunkPath(g_liveChunks, i);
            if (fileNonEmpty(path))
                allChunks.push_back(path);
        }
        while (g_liveNext <= maxIdx)
        {
            const OUString path = chunkPath(g_liveChunks, g_liveNext);
            ++g_liveNext;
            if (fileNonEmpty(path))
                tailJobs.push_back(path);
        }
    }

    for (const auto& path : tailJobs)
    {
        OUString st;
        const OUString t
            = NotebookMaterialStore::transcribeLocalMedia(path, st, 180, std::function<void()>())
                  .trim();
        if (t.isEmpty())
            continue;
        osl::MutexGuard g(speechMutex());
        g_liveText.append(t);
        g_liveText.append(u'\n');
    }

    {
        osl::MutexGuard g(speechMutex());
        rFullText = g_liveText.makeStringAndClear().trim();
    }
    if (rFullText.getLength() < 8 && !allChunks.empty())
    {
        OUStringBuffer full;
        for (const auto& p : allChunks)
        {
            OUString st;
            const OUString t
                = NotebookMaterialStore::transcribeLocalMedia(p, st, 180, std::function<void()>())
                      .trim();
            if (!t.isEmpty())
            {
                full.append(t);
                full.append(u'\n');
            }
        }
        rFullText = full.makeStringAndClear().trim();
    }

    rAudioPath = g_liveDir + u"/meeting-full.wav"_ustr;
    {
        OUString listPath = g_liveDir + u"/concat.txt"_ustr;
        OUStringBuffer list;
        for (const auto& p : allChunks)
        {
            list.append(u"file '"_ustr);
            list.append(p);
            list.append(u"'\n"_ustr);
        }
        writeSys(listPath, list.makeStringAndClear());
        if (MediaTranscriptService::hasFfmpeg() && !allChunks.empty())
        {
            runShell(u"ffmpeg -y -f concat -safe 0 -i "_ustr + shellQuote(listPath)
                     + u" -c copy "_ustr + shellQuote(rAudioPath) + u" >/dev/null 2>&1"_ustr);
        }
        if (!fileNonEmpty(rAudioPath) && !allChunks.empty())
            rAudioPath = allChunks.back();
    }

    {
        osl::MutexGuard g(speechMutex());
        g_live = false;
        g_meeting = false;
        g_meetingPath = rAudioPath;
    }
    if (rFullText.isEmpty() && !fileNonEmpty(rAudioPath))
    {
        rStatusOut = u"边录边出字结束，但未得到有效音频/文字"_ustr;
        return false;
    }
    rStatusOut = u"边录边出字已结束 · "_ustr + OUString::number(rFullText.getLength())
                 + u" 字 · "_ustr + rAudioPath;
    return true;
}

OUString LocalSpeechHub::speakPlain(const OUString& rText, const OUString& rVoice)
{
    if (rText.isEmpty())
        return u"没有可朗读的文本"_ustr;
#if defined(MACOSX) || defined(__APPLE__)
    OUString voice = rVoice.isEmpty() ? defaultVoice() : rVoice;
    // Cap spoken length
    OUString spoken = rText;
    if (spoken.getLength() > 4000)
        spoken = spoken.copy(0, 4000) + u"……"_ustr;
    const OUString tmp = u"/tmp/kq-speech-plain.txt"_ustr;
    writeSys(tmp, spoken);
    OUStringBuffer sh;
    sh.append(u"#!/bin/bash\n/usr/bin/say -v "_ustr);
    sh.append(shellQuote(voice));
    sh.append(u" -f "_ustr);
    sh.append(shellQuote(tmp));
    sh.append(u"\n"_ustr);
    const OUString script = u"/tmp/kq-speech-plain.sh"_ustr;
    writeSys(script, sh.makeStringAndClear());
    rtl_uString* args[2] = {};
    OUString bash(u"/bin/bash"_ustr);
    args[0] = bash.pData;
    args[1] = script.pData;
    oslProcess h = nullptr;
    osl_executeProcess(bash.pData, args + 1, 1, osl_Process_DETACHED, nullptr, nullptr, nullptr, 0,
                       &h);
    if (h)
        osl_freeProcessHandle(h);
    return u"正在朗读（"_ustr + voice + u"）…"_ustr;
#else
    (void)rVoice;
    if (runShell(u"command -v espeak >/dev/null 2>&1"_ustr) == 0)
    {
        runShell(u"espeak "_ustr + shellQuote(rText.copy(0, std::min<sal_Int32>(rText.getLength(), 500)))
                 + u" &"_ustr);
        return u"正在 espeak 朗读…"_ustr;
    }
    return u"当前平台未接入 TTS"_ustr;
#endif
}

OUString LocalSpeechHub::exportPlainAiff(const OUString& rText, const OUString& rVoice)
{
    if (rText.isEmpty())
        return u"没有可导出的文本"_ustr;
#if defined(MACOSX) || defined(__APPLE__)
    OUString voice = rVoice.isEmpty() ? defaultVoice() : rVoice;
    const char* home = std::getenv("HOME");
    const OUString downloads = OUString::fromUtf8(home && *home ? home : "/tmp")
                               + u"/Downloads"_ustr;
    const OUString aiff = downloads + u"/可圈-语音朗读.aiff"_ustr;
    const OUString tmp = u"/tmp/kq-speech-export.txt"_ustr;
    OUString spoken = rText;
    if (spoken.getLength() > 8000)
        spoken = spoken.copy(0, 8000);
    writeSys(tmp, spoken);
    OUStringBuffer sh;
    sh.append(u"#!/bin/bash\nmkdir -p "_ustr);
    sh.append(shellQuote(downloads));
    sh.append(u"\n/usr/bin/say -v "_ustr);
    sh.append(shellQuote(voice));
    sh.append(u" -f "_ustr);
    sh.append(shellQuote(tmp));
    sh.append(u" -o "_ustr);
    sh.append(shellQuote(aiff));
    sh.append(u"\nopen -R "_ustr);
    sh.append(shellQuote(aiff));
    sh.append(u"\n"_ustr);
    const OUString script = u"/tmp/kq-speech-export.sh"_ustr;
    writeSys(script, sh.makeStringAndClear());
    rtl_uString* args[2] = {};
    OUString bash(u"/bin/bash"_ustr);
    args[0] = bash.pData;
    args[1] = script.pData;
    oslProcess h = nullptr;
    osl_executeProcess(bash.pData, args + 1, 1, osl_Process_DETACHED, nullptr, nullptr, nullptr, 0,
                       &h);
    if (h)
        osl_freeProcessHandle(h);
    return u"已导出："_ustr + aiff;
#else
    (void)rVoice;
    return u"AIFF 导出仅 macOS"_ustr;
#endif
}

} // namespace kqoffice::ai::notebook

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
