/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "NotebookStudioPipeline.hxx"

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>

namespace kqoffice::ai::notebook
{
namespace
{
bool writeUtf8(const OUString& rSysPath, const OUString& rContent)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString utf8 = OUStringToOString(rContent, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    const bool ok = f.write(utf8.getStr(), utf8.getLength(), n) == osl::FileBase::E_None
                    && n == static_cast<sal_uInt64>(utf8.getLength());
    f.close();
    return ok;
}

bool ensureDir(const OUString& rSysDir)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysDir, url) != osl::FileBase::E_None)
        return false;
    return osl::Directory::createPath(url) == osl::FileBase::E_None
           || osl::Directory::createPath(url) == osl::FileBase::E_EXIST;
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

OUString defaultVoiceA()
{
#if defined(MACOSX) || defined(__APPLE__)
    return u"Ting-Ting"_ustr;
#else
    return u"A"_ustr;
#endif
}

OUString defaultVoiceB()
{
#if defined(MACOSX) || defined(__APPLE__)
    return u"Mei-Jia"_ustr;
#else
    return u"B"_ustr;
#endif
}

OUString stampFolder()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    const std::time_t sec = static_cast<std::time_t>(tv.Seconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &sec);
#else
    localtime_r(&sec, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return OUString::fromUtf8(buf);
}

OUString jsonEscape(const OUString& s)
{
    OUStringBuffer b;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u'\\' || c == u'"')
            b.append(u'\\');
        if (c == u'\n')
        {
            b.append(u"\\n"_ustr);
            continue;
        }
        if (c == u'\r')
            continue;
        b.append(c);
    }
    return b.makeStringAndClear();
}
} // namespace

std::vector<StudioSpeakTurn> NotebookStudioPipeline::parseTurns(const OUString& rScript)
{
    std::vector<StudioSpeakTurn> turns;
    const sal_Int32 n = rScript.getLength();
    sal_Int32 lineStart = 0;
    char last = 'A';
    bool anyLabel = false;
    for (sal_Int32 i = 0; i <= n; ++i)
    {
        if (i != n && rScript[i] != u'\n')
            continue;
        OUString line = rScript.copy(lineStart, i - lineStart).trim();
        lineStart = i + 1;
        if (line.isEmpty() || line.startsWith(u"#"_ustr) || line.startsWith(u"---"_ustr))
            continue;
        char sp = last;
        OUString body = line;
        auto stripPrefix = [&](const OUString& p) {
            if (line.startsWith(p))
            {
                body = line.copy(p.getLength()).trim();
                return true;
            }
            return false;
        };
        if (stripPrefix(u"A:"_ustr) || stripPrefix(u"A："_ustr) || stripPrefix(u"**A**"_ustr)
            || stripPrefix(u"**A:**"_ustr) || stripPrefix(u"主播A:"_ustr)
            || stripPrefix(u"主持人A:"_ustr) || stripPrefix(u"主播A："_ustr)
            || stripPrefix(u"说话人1:"_ustr) || stripPrefix(u"说话人1："_ustr)
            || stripPrefix(u"SPEAKER_00:"_ustr) || stripPrefix(u"SPEAKER_0:"_ustr))
        {
            sp = 'A';
            anyLabel = true;
        }
        else if (stripPrefix(u"B:"_ustr) || stripPrefix(u"B："_ustr) || stripPrefix(u"**B**"_ustr)
                 || stripPrefix(u"**B:**"_ustr) || stripPrefix(u"主播B:"_ustr)
                 || stripPrefix(u"主持人B:"_ustr) || stripPrefix(u"主播B："_ustr)
                 || stripPrefix(u"说话人2:"_ustr) || stripPrefix(u"说话人2："_ustr)
                 || stripPrefix(u"SPEAKER_01:"_ustr) || stripPrefix(u"SPEAKER_1:"_ustr))
        {
            sp = 'B';
            anyLabel = true;
        }
        if (body.getLength() > 400)
            body = body.copy(0, 400);
        if (body.isEmpty())
            continue;
        turns.push_back({ sp, body });
        last = sp;
    }
    // No A/B labels: alternate by turn for dual-voice product feel.
    if (!anyLabel && turns.size() > 1)
    {
        for (size_t i = 0; i < turns.size(); ++i)
            turns[i].speaker = (i % 2 == 0) ? 'A' : 'B';
    }
    if (turns.empty() && !rScript.isEmpty())
    {
        OUString chunk = rScript.copy(0, std::min<sal_Int32>(rScript.getLength(), 1500));
        turns.push_back({ 'A', chunk });
    }
    return turns;
}

OUString NotebookStudioPipeline::normalizeDualVoiceScript(const OUString& rScript)
{
    const auto turns = parseTurns(rScript);
    OUStringBuffer b;
    b.append(u"# 可圈笔记 · 双声音频概览脚本\n\n"_ustr);
    b.append(u"> 本地成品 · 不上传 · Tab/批准类写回不适用于本脚本（仅朗读/导出）\n\n"_ustr);
    for (const auto& t : turns)
    {
        b.append(t.speaker == 'B' ? u"B: "_ustr : u"A: "_ustr);
        b.append(t.text);
        b.append(u"\n\n"_ustr);
    }
    return b.makeStringAndClear();
}

OUString NotebookStudioPipeline::audioOverviewInstruction()
{
    return u"写成两位主播 A/B 的「成品级」播客对话脚本：\n"
           u"- 每行必须以「A:」或「B:」开头（全角冒号亦可）\n"
           u"- 约 12–18 轮；有开场钩子、3–5 个要点、一次追问、简短收束\n"
           u"- 口语化、可直接 TTS；关键事实带来源 [n]\n"
           u"- 禁止声称已生成音频文件；只输出脚本文本\n"
           u"- 不要输出与对话无关的大段说明\n"_ustr;
}

OUString NotebookStudioPipeline::videoOverviewInstruction()
{
    return u"产出「视频概览」成品制作稿（Markdown），结构固定：\n"
           u"1) 一句话钩子 + 目标观众 + 总时长建议\n"
           u"2) 章节时间轴（mm:ss 起止）\n"
           u"3) 分镜表（表格）：镜头# | 画面 | 旁白 | 字卡 | 时长 | 引用[n]\n"
           u"4) 完整旁白稿（可直接 TTS；可用 A:/B: 双声）\n"
           u"5) 片尾 CTA + 素材清单\n"
           u"仅依据来源；无字幕时明确「需补充转录」。禁止假称已渲染成片。\n"_ustr;
}

OUString NotebookStudioPipeline::studioFinishPackageInstruction()
{
    return u"在现有制品基础上，补一节「成品交付清单」："
           u"文件清单、朗读检查项、引用完整性、本地导出步骤（AIFF/成品包）。\n"_ustr;
}

OUString NotebookStudioPipeline::defaultPackageRoot()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return u"/tmp/kqoffice-studio-packages"_ustr;
    return OUString::fromUtf8(home) + u"/.config/kqoffice/notebook/studio-packages"_ustr;
}

OUString NotebookStudioPipeline::buildLiveSpeakShell(const std::vector<StudioSpeakTurn>& rTurns,
                                                     const OUString& rVoiceA,
                                                     const OUString& rVoiceB)
{
#if !(defined(MACOSX) || defined(__APPLE__))
    (void)rTurns;
    (void)rVoiceA;
    (void)rVoiceB;
    return OUString();
#else
    const OUString va = rVoiceA.isEmpty() ? defaultVoiceA() : rVoiceA;
    const OUString vb = rVoiceB.isEmpty() ? defaultVoiceB() : rVoiceB;
    OUStringBuffer sh;
    sh.append(u"#!/bin/bash\n# 可圈笔记 · dual-voice live\nset +e\n"_ustr);
    int n = 0;
    for (const auto& t : rTurns)
    {
        if (++n > 48)
            break;
        const OUString& voice = (t.speaker == 'B') ? vb : va;
        sh.append(u"/usr/bin/say -v "_ustr);
        sh.append(shellQuote(voice));
        sh.append(u' ');
        sh.append(shellQuote(t.text));
        sh.append(u"\n"_ustr);
    }
    return sh.makeStringAndClear();
#endif
}

OUString NotebookStudioPipeline::buildDualAiffExportShell(
    const std::vector<StudioSpeakTurn>& rTurns, const OUString& rOutAiff, const OUString& rWorkDir,
    const OUString& rVoiceA, const OUString& rVoiceB)
{
#if !(defined(MACOSX) || defined(__APPLE__))
    (void)rTurns;
    (void)rOutAiff;
    (void)rWorkDir;
    (void)rVoiceA;
    (void)rVoiceB;
    return OUString();
#else
    const OUString va = rVoiceA.isEmpty() ? defaultVoiceA() : rVoiceA;
    const OUString vb = rVoiceB.isEmpty() ? defaultVoiceB() : rVoiceB;
    OUStringBuffer sh;
    sh.append(u"#!/bin/bash\n# 可圈笔记 · dual-voice AIFF export (true A/B)\nset +e\n"_ustr);
    sh.append(u"WORK="_ustr);
    sh.append(shellQuote(rWorkDir));
    sh.append(u"\nOUT="_ustr);
    sh.append(shellQuote(rOutAiff));
    sh.append(u"\nmkdir -p \"$WORK\"\n"_ustr);
    sh.append(u"rm -f \"$WORK\"/turn-*.aiff \"$WORK\"/turn-*.txt \"$WORK\"/list.txt\n"_ustr);
    sh.append(u": > \"$WORK/list.txt\"\n"_ustr);
    int n = 0;
    for (const auto& t : rTurns)
    {
        if (++n > 48)
            break;
        const OUString& voice = (t.speaker == 'B') ? vb : va;
        char idx[16];
        std::snprintf(idx, sizeof(idx), "%02d", n);
        const OUString id = OUString::fromUtf8(idx);
        sh.append(u"printf '%s' "_ustr);
        sh.append(shellQuote(t.text));
        sh.append(u" > \"$WORK/turn-"_ustr);
        sh.append(id);
        sh.append(u".txt\"\n"_ustr);
        sh.append(u"/usr/bin/say -v "_ustr);
        sh.append(shellQuote(voice));
        sh.append(u" -f \"$WORK/turn-"_ustr);
        sh.append(id);
        sh.append(u".txt\" -o \"$WORK/turn-"_ustr);
        sh.append(id);
        sh.append(u".aiff\"\n"_ustr);
        sh.append(u"echo \"file '$WORK/turn-"_ustr);
        sh.append(id);
        sh.append(u".aiff'\" >> \"$WORK/list.txt\"\n"_ustr);
    }
    sh.append(u"if command -v ffmpeg >/dev/null 2>&1; then\n"_ustr);
    sh.append(u"  ffmpeg -y -f concat -safe 0 -i \"$WORK/list.txt\" -c copy \"$OUT\" 2>/dev/null "
              u"|| ffmpeg -y -f concat -safe 0 -i \"$WORK/list.txt\" \"$OUT\"\n"_ustr);
    sh.append(u"elif [ -f \"$WORK/turn-01.aiff\" ]; then\n"_ustr);
    sh.append(u"  cp \"$WORK/turn-01.aiff\" \"$OUT\"\n"_ustr);
    sh.append(u"fi\n"_ustr);
    sh.append(
        u"if [ -f \"$OUT\" ]; then open -R \"$OUT\"; echo OK \"$OUT\"; else echo FAIL; fi\n"_ustr);
    return sh.makeStringAndClear();
#endif
}

sal_Int32 NotebookStudioPipeline::scoreScriptQuality(const OUString& rScript)
{
    const auto turns = parseTurns(rScript);
    if (turns.empty())
        return 0;
    sal_Int32 score = 20;
    if (turns.size() >= 8)
        score += 25;
    else if (turns.size() >= 4)
        score += 15;
    else
        score += 5;
    sal_Int32 a = 0, b = 0;
    for (const auto& t : turns)
    {
        if (t.speaker == 'B')
            ++b;
        else
            ++a;
    }
    if (a > 0 && b > 0)
        score += 30;
    else
        score += 5;
    if (rScript.indexOf(u"["_ustr) >= 0)
        score += 15; // citations
    if (rScript.getLength() > 400)
        score += 10;
    if (score > 100)
        score = 100;
    return score;
}

OUString NotebookStudioPipeline::scoreScriptQualityZh(const OUString& rScript)
{
    const sal_Int32 s = scoreScriptQuality(rScript);
    const auto turns = parseTurns(rScript);
    OUStringBuffer b;
    b.append(u"成品度 "_ustr);
    b.append(OUString::number(s));
    b.append(u"/100 · 轮次="_ustr);
    b.append(OUString::number(static_cast<sal_Int32>(turns.size())));
    if (s >= 75)
        b.append(u" · 可导出双声 AIFF / 成品包"_ustr);
    else if (s >= 45)
        b.append(u" · 建议补 A/B 标签或轮次"_ustr);
    else
        b.append(u" · 请先生成「音频概览」脚本"_ustr);
    return b.makeStringAndClear();
}

StudioPackageResult NotebookStudioPipeline::exportPackage(const OUString& rTitle,
                                                          const OUString& rKind,
                                                          const OUString& rScriptBody,
                                                          const OUString& rSourcesIndex,
                                                          const OUString& rPackageRoot)
{
    StudioPackageResult out;
    const OUString root = rPackageRoot.isEmpty() ? defaultPackageRoot() : rPackageRoot;
    if (!ensureDir(root))
    {
        out.message = u"无法创建成品包根目录："_ustr + root;
        return out;
    }
    OUString safeTitle = rTitle.isEmpty() ? u"studio"_ustr : rTitle;
    safeTitle = safeTitle.replaceAll(u"/"_ustr, u"-"_ustr).replaceAll(u" "_ustr, u"-"_ustr);
    if (safeTitle.getLength() > 40)
        safeTitle = safeTitle.copy(0, 40);
    const OUString dir = root + u"/"_ustr + stampFolder() + u"-"_ustr + safeTitle;
    if (!ensureDir(dir))
    {
        out.message = u"无法创建成品包目录："_ustr + dir;
        return out;
    }

    const OUString normalized = normalizeDualVoiceScript(rScriptBody);
    const auto turns = parseTurns(normalized);
    out.turnCount = static_cast<sal_Int32>(turns.size());
    out.packageDir = dir;
    out.scriptPath = dir + u"/script.md"_ustr;
    out.readmePath = dir + u"/README.md"_ustr;
    out.speakScriptPath = dir + u"/speak-dual.sh"_ustr;
    out.aiffPath = dir + u"/audio-overview-dual.aiff"_ustr;

    if (!writeUtf8(out.scriptPath, normalized))
    {
        out.message = u"写入 script.md 失败"_ustr;
        return out;
    }

    OUStringBuffer readme;
    readme.append(u"# 可圈笔记 · Studio 成品包\n\n"_ustr);
    readme.append(u"- 标题："_ustr);
    readme.append(rTitle.isEmpty() ? u"（未命名）"_ustr : rTitle);
    readme.append(u"\n- 类型："_ustr);
    readme.append(rKind.isEmpty() ? u"audio-script"_ustr : rKind);
    readme.append(u"\n- 轮次："_ustr);
    readme.append(OUString::number(out.turnCount));
    readme.append(u"\n- "_ustr);
    readme.append(scoreScriptQualityZh(normalized));
    readme.append(u"\n\n## 本地使用\n\n"_ustr);
    readme.append(u"1. 打开 `script.md` 校对 A/B 台词\n"_ustr);
    readme.append(u"2. macOS：`bash speak-dual.sh` 双声试听\n"_ustr);
    readme.append(u"3. macOS：`bash export-aiff.sh` 导出双声 AIFF（需 `say`；有 ffmpeg 时拼接）\n"_ustr);
    readme.append(u"4. **不上传**；分享前请自行确认来源授权\n\n"_ustr);
    readme.append(u"## 文件\n\n"_ustr);
    readme.append(u"- `script.md` 规范化双声脚本\n"_ustr);
    readme.append(u"- `metadata.json` 元数据\n"_ustr);
    readme.append(u"- `sources.txt` 来源索引（若有）\n"_ustr);
    readme.append(u"- `speak-dual.sh` / `export-aiff.sh`\n"_ustr);
    if (!writeUtf8(out.readmePath, readme.makeStringAndClear()))
    {
        out.message = u"写入 README 失败"_ustr;
        return out;
    }

    OUStringBuffer meta;
    meta.append(u"{\n"_ustr);
    meta.append(u"  \"schema_version\": \"kq-studio-package-v1\",\n"_ustr);
    meta.append(u"  \"title\": \""_ustr);
    meta.append(jsonEscape(rTitle));
    meta.append(u"\",\n"_ustr);
    meta.append(u"  \"kind\": \""_ustr);
    meta.append(jsonEscape(rKind));
    meta.append(u"\",\n"_ustr);
    meta.append(u"  \"turn_count\": "_ustr);
    meta.append(OUString::number(out.turnCount));
    meta.append(u",\n"_ustr);
    meta.append(u"  \"quality_score\": "_ustr);
    meta.append(OUString::number(scoreScriptQuality(normalized)));
    meta.append(u",\n"_ustr);
    meta.append(u"  \"local_only\": true,\n"_ustr);
    meta.append(u"  \"upload\": false\n"_ustr);
    meta.append(u"}\n"_ustr);
    writeUtf8(dir + u"/metadata.json"_ustr, meta.makeStringAndClear());

    if (!rSourcesIndex.isEmpty())
        writeUtf8(dir + u"/sources.txt"_ustr, rSourcesIndex);

    const OUString live = buildLiveSpeakShell(turns);
    if (!live.isEmpty())
        writeUtf8(out.speakScriptPath, live);
    const OUString exportSh
        = buildDualAiffExportShell(turns, out.aiffPath, dir + u"/turns"_ustr);
    if (!exportSh.isEmpty())
        writeUtf8(dir + u"/export-aiff.sh"_ustr, exportSh);

    // chmod +x via shell snippet in message (UI may run bash)
    out.success = true;
    out.message = u"成品包已写入 · "_ustr + dir + u" · "_ustr + scoreScriptQualityZh(normalized)
                  + u" · 不上传"_ustr;
    return out;
}

} // namespace kqoffice::ai::notebook

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
