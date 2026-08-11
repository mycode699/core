/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "MediaTranscriptService.hxx"
#include "NotebookMaterialStore.hxx"

#include <osl/file.hxx>
#include <osl/process.h>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace kqoffice::ai::notebook
{
namespace
{
OUString fileNameOf(const OUString& rPath)
{
    sal_Int32 slash = rPath.lastIndexOf(u'/');
#if defined(_WIN32)
    const sal_Int32 bslash = rPath.lastIndexOf(u'\\');
    if (bslash > slash)
        slash = bslash;
#endif
    if (slash >= 0 && slash + 1 < rPath.getLength())
        return rPath.copy(slash + 1);
    return rPath;
}

OUString extOf(const OUString& rPath)
{
    const OUString name = fileNameOf(rPath);
    const sal_Int32 dot = name.lastIndexOf(u'.');
    if (dot < 0 || dot + 1 >= name.getLength())
        return OUString();
    return name.copy(dot + 1).toAsciiLowerCase();
}

OUString baseNoExt(const OUString& rPath)
{
    const OUString name = fileNameOf(rPath);
    const sal_Int32 dot = name.lastIndexOf(u'.');
    if (dot > 0)
        return name.copy(0, dot);
    return name;
}

bool pathExists(const OUString& p)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(p, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    return osl::DirectoryItem::get(url, item) == osl::FileBase::E_None;
}

OUString whichFfmpeg()
{
    static const char* cands[] = { "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg",
                                   "/usr/bin/ffmpeg" };
    for (const char* c : cands)
    {
        const OUString p = OUString::fromUtf8(c);
        if (pathExists(p))
            return p;
    }
    // PATH
    const int rc = std::system("command -v ffmpeg >/dev/null 2>&1");
    if (rc == 0)
        return u"ffmpeg"_ustr;
    return OUString();
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

bool ensureDir(const OUString& sys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(sys, url) != osl::FileBase::E_None)
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

bool runFfmpegConvert(const OUString& rIn, const OUString& rOut, const OUString& rExtraArgs,
                      OUString& rLog)
{
    const OUString ff = whichFfmpeg();
    if (ff.isEmpty())
    {
        rLog = u"未找到 ffmpeg"_ustr;
        return false;
    }
    // parent dir of out
    {
        sal_Int32 slash = rOut.lastIndexOf(u'/');
#if defined(_WIN32)
        const sal_Int32 bslash = rOut.lastIndexOf(u'\\');
        if (bslash > slash)
            slash = bslash;
#endif
        if (slash > 0)
            ensureDir(rOut.copy(0, slash));
    }
    const OUString logPath = MediaTranscriptService::convertWorkDir() + u"/ffmpeg-last.log"_ustr;
    ensureDir(MediaTranscriptService::convertWorkDir());
    OUStringBuffer sh;
    sh.append(u"#!/bin/bash\nset -e\n"_ustr);
    sh.append(shellQuote(ff));
    sh.append(u" -y -i "_ustr);
    sh.append(shellQuote(rIn));
    if (!rExtraArgs.isEmpty())
    {
        sh.append(u' ');
        sh.append(rExtraArgs);
    }
    sh.append(u' ');
    sh.append(shellQuote(rOut));
    sh.append(u" >"_ustr);
    sh.append(shellQuote(logPath));
    sh.append(u" 2>&1\n"_ustr);
    const OUString script = MediaTranscriptService::convertWorkDir() + u"/convert.sh"_ustr;
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(script, url) != osl::FileBase::E_None)
            return false;
        osl::File f(url);
        auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
        if (e != osl::FileBase::E_None)
            e = f.open(osl_File_OpenFlag_Write);
        if (e != osl::FileBase::E_None)
            return false;
        f.setSize(0);
        const OString utf8 = OUStringToOString(sh.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
        sal_uInt64 n = 0;
        f.write(utf8.getStr(), utf8.getLength(), n);
        f.close();
    }
    rtl_uString* args[2] = {};
    OUString bash(u"/bin/bash"_ustr);
    args[0] = bash.pData;
    args[1] = script.pData;
    oslProcess h = nullptr;
    if (osl_executeProcess(bash.pData, args + 1, 1, osl_Process_WAIT, nullptr, nullptr, nullptr, 0,
                           &h)
        != osl_Process_E_None)
    {
        rLog = u"无法启动 ffmpeg"_ustr;
        return false;
    }
    if (h)
        osl_freeProcessHandle(h);
    if (!pathExists(rOut))
    {
        rLog = u"转换失败，详见 "_ustr + logPath;
        return false;
    }
    rLog = u"OK"_ustr;
    return true;
}
} // namespace

bool MediaTranscriptService::isAudioExt(const OUString& rExt)
{
    const OUString e = rExt.toAsciiLowerCase();
    return e == u"mp3"_ustr || e == u"wav"_ustr || e == u"m4a"_ustr || e == u"aac"_ustr
           || e == u"flac"_ustr || e == u"ogg"_ustr || e == u"opus"_ustr || e == u"aiff"_ustr
           || e == u"aif"_ustr || e == u"wma"_ustr || e == u"amr"_ustr || e == u"caf"_ustr
           || e == u"webm"_ustr;
}

bool MediaTranscriptService::isVideoExt(const OUString& rExt)
{
    const OUString e = rExt.toAsciiLowerCase();
    return e == u"mp4"_ustr || e == u"mov"_ustr || e == u"mkv"_ustr || e == u"avi"_ustr
           || e == u"m4v"_ustr || e == u"wmv"_ustr || e == u"mpeg"_ustr || e == u"mpg"_ustr
           || e == u"webm"_ustr || e == u"ts"_ustr || e == u"flv"_ustr || e == u"3gp"_ustr;
}

bool MediaTranscriptService::isMediaPath(const OUString& rSystemPath)
{
    const OUString e = extOf(rSystemPath);
    return isAudioExt(e) || isVideoExt(e);
}

OUString MediaTranscriptService::supportedFormatsHint()
{
    return u"音频: mp3 wav m4a aac flac ogg opus aiff wma amr caf\n"
           u"视频: mp4 mov mkv avi m4v webm mpeg flv 3gp\n"
           u"导出: wav / mp3 / m4a / flac / ogg / aiff / opus\n"
           u"依赖: brew install ffmpeg · pip install openai-whisper"_ustr;
}

bool MediaTranscriptService::hasFfmpeg() { return !whichFfmpeg().isEmpty(); }

OUString MediaTranscriptService::ffmpegPath() { return whichFfmpeg(); }

OUString MediaTranscriptService::convertWorkDir()
{
    return NotebookMaterialStore::rootDir() + u"/media-convert"_ustr;
}

MediaConvertResult MediaTranscriptService::convertToWav16k(const OUString& rInPath,
                                                           const OUString& rOutDir)
{
    MediaConvertResult r;
    if (rInPath.isEmpty() || !pathExists(rInPath))
    {
        r.message = u"输入文件不存在"_ustr;
        return r;
    }
    if (!hasFfmpeg())
    {
        r.message = u"需要 ffmpeg：brew install ffmpeg"_ustr;
        return r;
    }
    OUString dir = rOutDir.isEmpty() ? convertWorkDir() : rOutDir;
    ensureDir(dir);
    const OUString out = dir + u"/"_ustr + baseNoExt(rInPath) + u"-16k.wav"_ustr;
    OUString log;
    // universal extract: strip video, mono 16k pcm
    if (!runFfmpegConvert(rInPath, out, u"-vn -ac 1 -ar 16000 -c:a pcm_s16le"_ustr, log))
    {
        r.message = log;
        return r;
    }
    r.ok = true;
    r.outPath = out;
    r.message = u"已转为 16k 单声道 WAV（STT 就绪）"_ustr;
    return r;
}

MediaConvertResult MediaTranscriptService::convertToFormat(const OUString& rInPath,
                                                           const OUString& rFormat,
                                                           const OUString& rOutPath)
{
    MediaConvertResult r;
    if (rInPath.isEmpty() || !pathExists(rInPath))
    {
        r.message = u"输入文件不存在"_ustr;
        return r;
    }
    if (!hasFfmpeg())
    {
        r.message = u"需要 ffmpeg"_ustr;
        return r;
    }
    OUString fmt = rFormat.toAsciiLowerCase().trim();
    if (fmt.startsWith(u"."_ustr))
        fmt = fmt.copy(1);
    if (fmt.isEmpty())
        fmt = u"mp3"_ustr;

    OUString out = rOutPath;
    if (out.isEmpty())
    {
        const char* home = std::getenv("HOME");
        const OUString downloads = OUString::fromUtf8(home && *home ? home : "/tmp")
                                   + u"/Downloads"_ustr;
        ensureDir(downloads);
        out = downloads + u"/"_ustr + baseNoExt(rInPath) + u"."_ustr + fmt;
    }

    OUString extra;
    if (fmt == u"wav"_ustr)
        extra = u"-vn -ac 1 -ar 44100 -c:a pcm_s16le"_ustr;
    else if (fmt == u"mp3"_ustr)
        extra = u"-vn -ac 2 -ar 44100 -c:a libmp3lame -b:a 192k"_ustr;
    else if (fmt == u"m4a"_ustr || fmt == u"aac"_ustr)
        extra = u"-vn -c:a aac -b:a 192k"_ustr;
    else if (fmt == u"flac"_ustr)
        extra = u"-vn -c:a flac"_ustr;
    else if (fmt == u"ogg"_ustr || fmt == u"opus"_ustr)
        extra = u"-vn -c:a libopus -b:a 96k"_ustr;
    else if (fmt == u"aiff"_ustr || fmt == u"aif"_ustr)
        extra = u"-vn -c:a pcm_s16be"_ustr;
    else
    {
        r.message = u"不支持的导出格式: "_ustr + fmt + u"\n"_ustr + supportedFormatsHint();
        return r;
    }

    OUString log;
    if (!runFfmpegConvert(rInPath, out, extra, log))
    {
        r.message = log;
        return r;
    }
    r.ok = true;
    r.outPath = out;
    r.message = u"已导出 "_ustr + fmt + u"： "_ustr + out;
    return r;
}

OUString MediaTranscriptService::transcribeMediaFile(const OUString& rMediaPath,
                                                     OUString& rStatusOut, sal_Int32 nTimeoutSec,
                                                     const std::function<void()>& rOnTick)
{
    if (!pathExists(rMediaPath))
    {
        rStatusOut = u"媒体文件不存在"_ustr;
        return OUString();
    }
    if (!isMediaPath(rMediaPath))
    {
        rStatusOut = u"扩展名未识别为音视频，仍尝试转写…"_ustr;
    }
    // Always normalize via 16k wav for whisper quality
    const MediaConvertResult conv = convertToWav16k(rMediaPath);
    if (!conv.ok)
    {
        // fallback: try original path (wav already)
        rStatusOut = conv.message + u" · 尝试直接转写原文件…"_ustr;
        return NotebookMaterialStore::transcribeLocalMedia(rMediaPath, rStatusOut, nTimeoutSec,
                                                           rOnTick);
    }
    OUString st;
    const OUString text
        = NotebookMaterialStore::transcribeLocalMedia(conv.outPath, st, nTimeoutSec, rOnTick);
    if (text.isEmpty())
    {
        rStatusOut = st;
        return OUString();
    }
    rStatusOut = u"音视频转写完成（ffmpeg 归一化 + 本机 STT）· "_ustr + st;
    return text;
}

OUString MediaTranscriptService::normalizeTranscriptEdit(const OUString& rRaw)
{
    if (rRaw.isEmpty())
        return OUString();
    OUStringBuffer out;
    sal_Int32 lineStart = 0;
    const sal_Int32 n = rRaw.getLength();
    bool prevBlank = false;
    for (sal_Int32 i = 0; i <= n; ++i)
    {
        if (i != n && rRaw[i] != u'\n')
            continue;
        OUString line = rRaw.copy(lineStart, i - lineStart);
        lineStart = i + 1;
        if (line.endsWith(u"\r"_ustr))
            line = line.copy(0, line.getLength() - 1);
        const OUString t = line.trim();
        if (t.isEmpty())
        {
            if (!prevBlank && out.getLength() > 0)
            {
                out.append(u'\n');
                prevBlank = true;
            }
            continue;
        }
        prevBlank = false;
        out.append(t);
        out.append(u'\n');
    }
    return out.makeStringAndClear().trim();
}

OUString MediaTranscriptService::buildChapterOutline(const OUString& rTranscript)
{
    const OUString t = normalizeTranscriptEdit(rTranscript);
    if (t.isEmpty())
        return OUString();
    OUStringBuffer b;
    b.append(u"# 章节编排草稿\n\n"_ustr);
    sal_Int32 para = 0;
    sal_Int32 lineStart = 0;
    const sal_Int32 n = t.getLength();
    OUStringBuffer chunk;
    for (sal_Int32 i = 0; i <= n; ++i)
    {
        if (i != n && t[i] != u'\n')
            continue;
        OUString line = t.copy(lineStart, i - lineStart).trim();
        lineStart = i + 1;
        if (line.isEmpty())
        {
            if (chunk.getLength() > 40)
            {
                ++para;
                OUString head = chunk.makeStringAndClear().trim();
                if (head.getLength() > 36)
                    head = head.copy(0, 36) + u"…"_ustr;
                b.append(u"## "_ustr);
                b.append(OUString::number(para));
                b.append(u". "_ustr);
                b.append(head);
                b.append(u"\n\n"_ustr);
            }
            else
                chunk.setLength(0);
            continue;
        }
        if (chunk.getLength() > 0)
            chunk.append(u' ');
        chunk.append(line);
        // hard split long runs
        if (chunk.getLength() > 280)
        {
            ++para;
            OUString head = chunk.makeStringAndClear().trim();
            if (head.getLength() > 36)
                head = head.copy(0, 36) + u"…"_ustr;
            b.append(u"## "_ustr);
            b.append(OUString::number(para));
            b.append(u". "_ustr);
            b.append(head);
            b.append(u"\n\n"_ustr);
        }
    }
    if (chunk.getLength() > 20)
    {
        ++para;
        OUString head = chunk.makeStringAndClear().trim();
        if (head.getLength() > 36)
            head = head.copy(0, 36) + u"…"_ustr;
        b.append(u"## "_ustr);
        b.append(OUString::number(para));
        b.append(u". "_ustr);
        b.append(head);
        b.append(u"\n"_ustr);
    }
    if (para == 0)
        b.append(u"（正文较短，可直接 AI 分章）\n"_ustr);
    return b.makeStringAndClear();
}

bool MediaTranscriptService::hasWhisperX()
{
    return std::system("command -v whisperx >/dev/null 2>&1") == 0
           || pathExists(u"/opt/homebrew/bin/whisperx"_ustr)
           || pathExists(u"/usr/local/bin/whisperx"_ustr);
}

OUString MediaTranscriptService::diarizePlainText(const OUString& rTranscript, sal_Int32 nSpeakers)
{
    if (nSpeakers < 2)
        nSpeakers = 2;
    if (nSpeakers > 6)
        nSpeakers = 6;
    const OUString t = normalizeTranscriptEdit(rTranscript);
    if (t.isEmpty())
        return OUString();

    // Split into sentences / short turns on 。！？.!?\n
    std::vector<OUString> turns;
    OUStringBuffer cur;
    auto flush = [&]() {
        const OUString s = cur.makeStringAndClear().trim();
        if (!s.isEmpty())
            turns.push_back(s);
    };
    for (sal_Int32 i = 0; i < t.getLength(); ++i)
    {
        const sal_Unicode c = t[i];
        cur.append(c);
        if (c == u'。' || c == u'！' || c == u'？' || c == u'!' || c == u'?' || c == u'\n')
        {
            if (cur.getLength() > 8)
                flush();
        }
        else if (cur.getLength() > 120)
            flush();
    }
    flush();

    OUStringBuffer out;
    out.append(u"# 说话人分离（文本启发式）\n\n"_ustr);
    out.append(u"_说明：无声纹模型时按停顿/句读轮换标注；可用 whisperx 获得更准分离。_\n\n"_ustr);
    for (size_t i = 0; i < turns.size(); ++i)
    {
        const sal_Int32 sp = static_cast<sal_Int32>(i % static_cast<size_t>(nSpeakers)) + 1;
        out.append(u"**[说话人"_ustr);
        out.append(OUString::number(sp));
        out.append(u"]** "_ustr);
        out.append(turns[i]);
        out.append(u"\n\n"_ustr);
    }
    return out.makeStringAndClear();
}

OUString MediaTranscriptService::diarizeMediaFile(const OUString& rMediaPath, OUString& rStatusOut,
                                                  sal_Int32 nSpeakers, sal_Int32 nTimeoutSec,
                                                  const std::function<void()>& rOnTick)
{
    if (nSpeakers < 2)
        nSpeakers = 2;
    if (nSpeakers > 6)
        nSpeakers = 6;
    if (!pathExists(rMediaPath))
    {
        rStatusOut = u"媒体不存在"_ustr;
        return OUString();
    }

    // 1) Prefer whisperx --diarize
    if (hasWhisperX())
    {
        const OUString work = convertWorkDir() + u"/diarize"_ustr;
        ensureDir(work);
        const OUString outTxt = work + u"/diarize.txt"_ustr;
        OUStringBuffer sh;
        sh.append(u"#!/bin/bash\nset +e\n"_ustr);
        sh.append(u"whisperx "_ustr);
        sh.append(shellQuote(rMediaPath));
        sh.append(u" --language zh --diarize --output_dir "_ustr);
        sh.append(shellQuote(work));
        sh.append(u" --output_format txt >"_ustr);
        sh.append(shellQuote(work + u"/wx.log"_ustr));
        sh.append(u" 2>&1\n"_ustr);
        // whisperx names output after stem
        sh.append(u"ls -1 "_ustr);
        sh.append(shellQuote(work));
        sh.append(u"/*.txt 2>/dev/null | head -1\n"_ustr);
        const OUString script = work + u"/run-wx.sh"_ustr;
        {
            OUString url;
            if (osl::FileBase::getFileURLFromSystemPath(script, url) == osl::FileBase::E_None)
            {
                osl::File f(url);
                auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
                if (e != osl::FileBase::E_None)
                    e = f.open(osl_File_OpenFlag_Write);
                if (e == osl::FileBase::E_None)
                {
                    f.setSize(0);
                    const OString utf8
                        = OUStringToOString(sh.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
                    sal_uInt64 n = 0;
                    f.write(utf8.getStr(), utf8.getLength(), n);
                    f.close();
                }
            }
        }
        // run with wait + timeout via background poll simplified: system with long block
        (void)nTimeoutSec;
        (void)rOnTick;
        const OUString runCmd = u"/bin/bash "_ustr + shellQuote(script);
        const int rc
            = std::system(OUStringToOString(runCmd, RTL_TEXTENCODING_UTF8).getStr());
        (void)rc;
        // pick any txt larger than 20 bytes
        // try common names
        std::vector<OUString> cands;
        cands.push_back(outTxt);
        // read directory is heavy — try stem
        const OUString stem = baseNoExt(rMediaPath);
        cands.push_back(work + u"/"_ustr + stem + u".txt"_ustr);
        for (const auto& c : cands)
        {
            if (!pathExists(c))
                continue;
            OUString url;
            if (osl::FileBase::getFileURLFromSystemPath(c, url) != osl::FileBase::E_None)
                continue;
            osl::File f(url);
            if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
                continue;
            sal_uInt64 sz = 0;
            f.getSize(sz);
            if (sz < 20 || sz > 8 * 1024 * 1024)
            {
                f.close();
                continue;
            }
            std::vector<char> buf(static_cast<size_t>(sz));
            sal_uInt64 n = 0;
            f.read(buf.data(), sz, n);
            f.close();
            OUString text = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
            if (!text.isEmpty())
            {
                rStatusOut = u"说话人分离完成（whisperx）"_ustr;
                return u"# 说话人分离（whisperx）\n\n"_ustr + text;
            }
        }
        rStatusOut = u"whisperx 未产出有效文本，回退静音分段…"_ustr;
    }

    // 2) Silence-based turns: extract speech islands, STT each, alternate speakers
    if (!hasFfmpeg())
    {
        // plain text fallback after full STT
        OUString st;
        const OUString plain = transcribeMediaFile(rMediaPath, st, nTimeoutSec, rOnTick);
        if (plain.isEmpty())
        {
            rStatusOut = st;
            return OUString();
        }
        rStatusOut = u"说话人分离（文本启发式）· "_ustr + st;
        return diarizePlainText(plain, nSpeakers);
    }

    const MediaConvertResult wav = convertToWav16k(rMediaPath);
    const OUString audio = wav.ok ? wav.outPath : rMediaPath;
    const OUString work = convertWorkDir() + u"/diarize-sil"_ustr;
    ensureDir(work);
    const OUString silLog = work + u"/silence.log"_ustr;
    // silencedetect
    {
        OUString cmd = u"ffmpeg -i "_ustr + shellQuote(audio)
                       + u" -af silencedetect=noise=-30dB:d=0.55 -f null - >"_ustr
                       + shellQuote(silLog) + u" 2>&1"_ustr;
        std::system(OUStringToOString(cmd, RTL_TEXTENCODING_UTF8).getStr());
    }
    // parse silence_end / silence_start → speech ranges
    struct Seg
    {
        double a, b;
    };
    std::vector<Seg> segs;
    {
        OUString url;
        std::string raw;
        if (osl::FileBase::getFileURLFromSystemPath(silLog, url) == osl::FileBase::E_None)
        {
            osl::File f(url);
            if (f.open(osl_File_OpenFlag_Read) == osl::FileBase::E_None)
            {
                sal_uInt64 sz = 0;
                f.getSize(sz);
                if (sz > 0 && sz < 4 * 1024 * 1024)
                {
                    std::vector<char> buf(static_cast<size_t>(sz));
                    sal_uInt64 n = 0;
                    f.read(buf.data(), sz, n);
                    raw.assign(buf.data(), static_cast<size_t>(n));
                }
                f.close();
            }
        }
        double speechStart = 0.0;
        bool inSpeech = true;
        auto parseTime = [](const std::string& line, const char* key) -> double {
            const auto p = line.find(key);
            if (p == std::string::npos)
                return -1;
            return std::atof(line.c_str() + p + std::strlen(key));
        };
        size_t pos = 0;
        while (pos < raw.size())
        {
            size_t nl = raw.find('\n', pos);
            if (nl == std::string::npos)
                nl = raw.size();
            std::string line = raw.substr(pos, nl - pos);
            pos = nl + 1;
            if (line.find("silence_start:") != std::string::npos)
            {
                const double t = parseTime(line, "silence_start: ");
                if (t >= 0 && inSpeech)
                {
                    if (t - speechStart > 0.4)
                        segs.push_back({ speechStart, t });
                    inSpeech = false;
                }
            }
            else if (line.find("silence_end:") != std::string::npos)
            {
                const double t = parseTime(line, "silence_end: ");
                if (t >= 0)
                {
                    speechStart = t;
                    inSpeech = true;
                }
            }
        }
        if (inSpeech)
            segs.push_back({ speechStart, speechStart + 600 }); // open end capped later
    }
    if (segs.size() < 2)
    {
        OUString st;
        const OUString plain = NotebookMaterialStore::transcribeLocalMedia(audio, st, nTimeoutSec,
                                                                           rOnTick);
        rStatusOut = u"静音段不足，回退文本启发式 · "_ustr + st;
        return diarizePlainText(plain, nSpeakers);
    }

    // Cap segments
    if (segs.size() > 40)
        segs.resize(40);

    OUStringBuffer out;
    out.append(u"# 说话人分离（静音分段 · "_ustr);
    out.append(OUString::number(nSpeakers));
    out.append(u" 人）\n\n"_ustr);
    sal_Int32 turn = 0;
    for (const auto& s : segs)
    {
        if (rOnTick)
            rOnTick();
        double dur = s.b - s.a;
        if (dur < 0.5)
            continue;
        if (dur > 90)
            dur = 90;
        char name[64];
        std::snprintf(name, sizeof(name), "turn_%02d.wav", static_cast<int>(turn));
        const OUString slice = work + u"/"_ustr + OUString::fromUtf8(name);
        OUString cmd = u"ffmpeg -y -ss "_ustr + OUString::number(s.a) + u" -t "_ustr
                       + OUString::number(dur) + u" -i "_ustr + shellQuote(audio)
                       + u" -ac 1 -ar 16000 "_ustr + shellQuote(slice) + u" >/dev/null 2>&1"_ustr;
        std::system(OUStringToOString(cmd, RTL_TEXTENCODING_UTF8).getStr());
        if (!pathExists(slice))
            continue;
        OUString st;
        const OUString text
            = NotebookMaterialStore::transcribeLocalMedia(slice, st, 90, rOnTick).trim();
        if (text.isEmpty())
            continue;
        const sal_Int32 sp = (turn % nSpeakers) + 1;
        out.append(u"**[说话人"_ustr);
        out.append(OUString::number(sp));
        out.append(u"]** `"_ustr);
        out.append(OUString::number(static_cast<sal_Int32>(s.a)));
        out.append(u"s` "_ustr);
        out.append(text);
        out.append(u"\n\n"_ustr);
        ++turn;
    }
    if (turn == 0)
    {
        OUString st;
        const OUString plain = NotebookMaterialStore::transcribeLocalMedia(audio, st, nTimeoutSec,
                                                                           rOnTick);
        rStatusOut = u"分段转写为空，文本启发式 · "_ustr + st;
        return diarizePlainText(plain, nSpeakers);
    }
    rStatusOut = u"说话人分离完成（静音分段 · "_ustr + OUString::number(turn) + u" 轮 · "
                 + OUString::number(nSpeakers) + u" 人）"_ustr;
    return out.makeStringAndClear();
}

std::vector<MediaTranscriptService::TranscriptCue>
MediaTranscriptService::parseSpeakerTimeline(const OUString& rLabeled)
{
    std::vector<TranscriptCue> out;
    if (rLabeled.isEmpty())
        return out;

    sal_Int32 lineStart = 0;
    const sal_Int32 n = rLabeled.getLength();
    for (sal_Int32 i = 0; i <= n; ++i)
    {
        if (i != n && rLabeled[i] != u'\n')
            continue;
        const sal_Int32 lineLen = i - lineStart;
        OUString line = rLabeled.copy(lineStart, lineLen);
        if (line.endsWith(u"\r"_ustr))
            line = line.copy(0, line.getLength() - 1);
        const sal_Int32 absStart = lineStart;
        lineStart = i + 1;

        const OUString t = line.trim();
        if (t.isEmpty() || t.startsWith(u"#"_ustr) || t.startsWith(u"_"_ustr))
            continue;

        // **[Name]** `12s` text   OR  **[Name]** text  OR  [Name] text
        OUString speaker;
        OUString rest = t;
        sal_Int32 spStart = -1;
        if (t.startsWith(u"**["_ustr))
        {
            const sal_Int32 close = t.indexOf(u"]**"_ustr);
            if (close > 3)
            {
                speaker = t.copy(3, close - 3).trim();
                rest = t.copy(close + 3).trim();
                spStart = absStart + t.indexOf(u"**["_ustr);
            }
        }
        else if (t.startsWith(u"["_ustr))
        {
            const sal_Int32 close = t.indexOf(u']');
            if (close > 1)
            {
                speaker = t.copy(1, close - 1).trim();
                rest = t.copy(close + 1).trim();
                spStart = absStart;
            }
        }
        if (speaker.isEmpty())
            continue;

        double sec = -1;
        // `12s` or `12.5s` or 00:01:02
        if (rest.startsWith(u"`"_ustr))
        {
            const sal_Int32 tick = rest.indexOf(u'`', 1);
            if (tick > 1)
            {
                OUString ts = rest.copy(1, tick - 1).trim();
                rest = rest.copy(tick + 1).trim();
                if (ts.endsWith(u"s"_ustr) || ts.endsWith(u"S"_ustr))
                    ts = ts.copy(0, ts.getLength() - 1);
                // mm:ss
                const sal_Int32 colon = ts.indexOf(u':');
                if (colon > 0)
                {
                    const sal_Int32 c2 = ts.indexOf(u':', colon + 1);
                    if (c2 > 0)
                    {
                        // hh:mm:ss
                        sec = ts.copy(0, colon).toDouble() * 3600
                              + ts.copy(colon + 1, c2 - colon - 1).toDouble() * 60
                              + ts.copy(c2 + 1).toDouble();
                    }
                    else
                    {
                        sec = ts.copy(0, colon).toDouble() * 60 + ts.copy(colon + 1).toDouble();
                    }
                }
                else
                    sec = ts.toDouble();
            }
        }

        TranscriptCue c;
        c.speaker = speaker;
        c.startSec = sec;
        c.text = rest;
        c.startChar = (spStart >= 0) ? spStart : absStart;
        c.endChar = absStart + line.getLength();
        out.push_back(c);
    }
    return out;
}

OUString MediaTranscriptService::renameSpeakers(const OUString& rLabeled, const OUString& rNamesCsv)
{
    if (rLabeled.isEmpty() || rNamesCsv.isEmpty())
        return rLabeled;

    // Split names by , ， 、 ; ； / |
    std::vector<OUString> names;
    {
        OUStringBuffer cur;
        auto flush = [&]() {
            const OUString t = cur.makeStringAndClear().trim();
            if (!t.isEmpty())
                names.push_back(t);
        };
        for (sal_Int32 i = 0; i < rNamesCsv.getLength(); ++i)
        {
            const sal_Unicode c = rNamesCsv[i];
            if (c == u',' || c == u'，' || c == u'、' || c == u';' || c == u'；' || c == u'/'
                || c == u'|')
                flush();
            else
                cur.append(c);
        }
        flush();
    }
    if (names.empty())
        return rLabeled;

    OUString out = rLabeled;
    for (size_t i = 0; i < names.size(); ++i)
    {
        const sal_Int32 n = static_cast<sal_Int32>(i) + 1;
        const OUString old1 = u"说话人"_ustr + OUString::number(n);
        char spk[16];
        std::snprintf(spk, sizeof(spk), "SPEAKER_%02d", static_cast<int>(n - 1));
        const OUString old2 = OUString::fromUtf8(spk);
        const OUString old3 = u"Speaker "_ustr + OUString::number(n);
        out = out.replaceAll(old1, names[i]);
        out = out.replaceAll(old2, names[i]);
        out = out.replaceAll(old3, names[i]);
    }
    return out;
}

OUString MediaTranscriptService::serializeTimeline(const std::vector<TranscriptCue>& rCues)
{
    if (rCues.empty())
        return OUString();
    OUStringBuffer out;
    out.append(u"# 说话人时间轴\n\n"_ustr);
    for (const auto& c : rCues)
    {
        out.append(u"**["_ustr);
        out.append(c.speaker.isEmpty() ? u"说话人?"_ustr : c.speaker);
        out.append(u"]**"_ustr);
        if (c.startSec >= 0)
        {
            out.append(u" `"_ustr);
            out.append(OUString::number(static_cast<sal_Int32>(c.startSec)));
            out.append(u"s`"_ustr);
        }
        out.append(u' ');
        out.append(c.text);
        out.append(u"\n\n"_ustr);
    }
    return out.makeStringAndClear();
}

std::vector<MediaTranscriptService::TranscriptCue>
MediaTranscriptService::mergeAdjacentSameSpeaker(const std::vector<TranscriptCue>& rCues)
{
    std::vector<TranscriptCue> out;
    for (const auto& c : rCues)
    {
        if (out.empty() || out.back().speaker != c.speaker)
        {
            out.push_back(c);
            continue;
        }
        // merge text, keep earliest startSec
        if (!out.back().text.isEmpty() && !c.text.isEmpty())
            out.back().text += u" "_ustr;
        out.back().text += c.text;
        if (out.back().startSec < 0 && c.startSec >= 0)
            out.back().startSec = c.startSec;
    }
    return out;
}

std::vector<MediaTranscriptService::TranscriptCue>
MediaTranscriptService::mergeCueWithNext(std::vector<TranscriptCue> aCues, sal_Int32 nIndex)
{
    if (nIndex < 0 || static_cast<size_t>(nIndex) + 1 >= aCues.size())
        return aCues;
    auto& a = aCues[static_cast<size_t>(nIndex)];
    const auto& b = aCues[static_cast<size_t>(nIndex) + 1];
    if (!a.text.isEmpty() && !b.text.isEmpty())
        a.text += u" "_ustr;
    a.text += b.text;
    // keep a's speaker and start time
    aCues.erase(aCues.begin() + static_cast<std::ptrdiff_t>(nIndex) + 1);
    return aCues;
}

std::vector<MediaTranscriptService::TranscriptCue>
MediaTranscriptService::reassignCueSpeaker(std::vector<TranscriptCue> aCues, sal_Int32 nIndex,
                                           const OUString& rNewSpeaker, sal_Int32 nSpeakers)
{
    if (nIndex < 0 || static_cast<size_t>(nIndex) >= aCues.size())
        return aCues;
    if (nSpeakers < 2)
        nSpeakers = 2;
    if (nSpeakers > 8)
        nSpeakers = 8;

    OUString name = rNewSpeaker.trim();
    if (name.isEmpty())
    {
        // cycle 说话人1..N based on current
        sal_Int32 cur = 1;
        const OUString& sp = aCues[static_cast<size_t>(nIndex)].speaker;
        if (sp.startsWith(u"说话人"_ustr))
        {
            const OUString num = sp.copy(3).trim();
            const sal_Int32 v = num.toInt32();
            if (v >= 1)
                cur = v;
        }
        sal_Int32 next = cur + 1;
        if (next > nSpeakers)
            next = 1;
        name = u"说话人"_ustr + OUString::number(next);
    }
    aCues[static_cast<size_t>(nIndex)].speaker = name;
    return aCues;
}

std::vector<MediaTranscriptService::TranscriptCue>
MediaTranscriptService::deleteCue(std::vector<TranscriptCue> aCues, sal_Int32 nIndex)
{
    if (nIndex < 0 || static_cast<size_t>(nIndex) >= aCues.size())
        return aCues;
    aCues.erase(aCues.begin() + static_cast<std::ptrdiff_t>(nIndex));
    return aCues;
}

std::vector<MediaTranscriptService::TranscriptCue>
MediaTranscriptService::moveCue(std::vector<TranscriptCue> aCues, sal_Int32 nIndex, sal_Int32 nDelta)
{
    if (aCues.empty() || nIndex < 0 || static_cast<size_t>(nIndex) >= aCues.size())
        return aCues;
    const sal_Int32 n = static_cast<sal_Int32>(aCues.size());
    sal_Int32 j = nIndex + nDelta;
    if (j < 0 || j >= n)
        return aCues;
    std::swap(aCues[static_cast<size_t>(nIndex)], aCues[static_cast<size_t>(j)]);
    return aCues;
}

} // namespace kqoffice::ai::notebook

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
