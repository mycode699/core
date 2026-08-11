/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 可圈 · 音视频转写与格式转换（本机 ffmpeg + whisper）
 * 对标：Otter / 飞书妙记 / 通义听悟 本地闭环
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_MEDIATRANSCRIPTSERVICE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_MEDIATRANSCRIPTSERVICE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <functional>
#include <vector>

namespace kqoffice::ai::notebook
{

struct MediaConvertResult
{
    bool ok = false;
    OUString outPath;
    OUString message;
};

/// Multi-format convert + STT for local media (no cloud).
class SAL_DLLPUBLIC_EXPORT MediaTranscriptService
{
public:
    /// mp3/wav/m4a/aac/flac/ogg/opus/aiff/wma + video containers
    static bool isAudioExt(const OUString& rExt);
    static bool isVideoExt(const OUString& rExt);
    static bool isMediaPath(const OUString& rSystemPath);
    static OUString supportedFormatsHint();

    static bool hasFfmpeg();
    static OUString ffmpegPath();

    /// Convert any ffmpeg-readable media → 16 kHz mono PCM wav (STT ready).
    static MediaConvertResult convertToWav16k(const OUString& rInPath,
                                              const OUString& rOutDir = OUString());

    /// Convert to target format: wav | mp3 | m4a | flac | ogg | aiff | opus
    static MediaConvertResult convertToFormat(const OUString& rInPath, const OUString& rFormat,
                                              const OUString& rOutPath = OUString());

    /// High-level: convert if needed + whisper STT. Returns plain text.
    static OUString
    transcribeMediaFile(const OUString& rMediaPath, OUString& rStatusOut,
                        sal_Int32 nTimeoutSec = 900,
                        const std::function<void()>& rOnTick = std::function<void()>());

    /// Soft clean transcript for editing (collapse blank lines, trim).
    static OUString normalizeTranscriptEdit(const OUString& rRaw);

    /// Heuristic chapter outline from transcript paragraphs (for 编排).
    static OUString buildChapterOutline(const OUString& rTranscript);

    /**
     * Speaker diarization (说话人分离).
     * Prefer whisperx if installed; else silence-segment + alternate speakers;
     * always returns labeled Markdown-ish text.
     */
    static bool hasWhisperX();
    static OUString diarizeMediaFile(const OUString& rMediaPath, OUString& rStatusOut,
                                     sal_Int32 nSpeakers = 2, sal_Int32 nTimeoutSec = 900,
                                     const std::function<void()>& rOnTick = std::function<void()>());
    /// Label plain transcript when no audio path (paragraph/sentence alternating speakers).
    static OUString diarizePlainText(const OUString& rTranscript, sal_Int32 nSpeakers = 2);

    /// One turn in a labeled diarization / timeline document.
    struct TranscriptCue
    {
        OUString speaker; ///< e.g. 说话人1 or 张三
        double startSec = -1; ///< <0 if unknown
        OUString text;
        sal_Int32 startChar = 0; ///< UTF-16 offset in full document
        sal_Int32 endChar = 0;
    };

    /// Parse diarized Markdown into cues (for timeline UI + jump).
    static std::vector<TranscriptCue> parseSpeakerTimeline(const OUString& rLabeled);

    /**
     * Rename 说话人1..N → custom names (comma/顿号/space separated).
     * Also maps SPEAKER_00 style from whisperx when possible.
     */
    static OUString renameSpeakers(const OUString& rLabeled, const OUString& rNamesCsv);

    /// Rebuild labeled document from cues (keeps timestamps when present).
    static OUString serializeTimeline(const std::vector<TranscriptCue>& rCues);

    /// Merge consecutive cues that share the same speaker label.
    static std::vector<TranscriptCue>
    mergeAdjacentSameSpeaker(const std::vector<TranscriptCue>& rCues);

    /// Merge cue at index with the next one (force same speaker as first).
    static std::vector<TranscriptCue> mergeCueWithNext(std::vector<TranscriptCue> aCues,
                                                       sal_Int32 nIndex);

    /// Reassign speaker for one cue; empty rNewSpeaker cycles 说话人1..nSpeakers.
    static std::vector<TranscriptCue> reassignCueSpeaker(std::vector<TranscriptCue> aCues,
                                                         sal_Int32 nIndex,
                                                         const OUString& rNewSpeaker,
                                                         sal_Int32 nSpeakers = 2);

    static std::vector<TranscriptCue> deleteCue(std::vector<TranscriptCue> aCues, sal_Int32 nIndex);
    static std::vector<TranscriptCue> moveCue(std::vector<TranscriptCue> aCues, sal_Int32 nIndex,
                                              sal_Int32 nDelta);

    /// Work dir under notebook root
    static OUString convertWorkDir();
};

} // namespace kqoffice::ai::notebook

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
