/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 可圈语音中枢 · local-first
 * 会议录音 / 通用 TTS / 能力探测（对标 NotebookLM + 会议助手体验）
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_LOCALSPEECHHUB_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_LOCALSPEECHHUB_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::notebook
{

struct SpeechCapability
{
    bool hasMicRecord = false; ///< ffmpeg/rec/arecord
    bool hasStt = false; ///< whisper family or voiceCmd
    bool hasTts = false; ///< macOS say / espeak
    OUString summary;
    OUString installHint;
};

/// Meeting record + plain TTS helpers (no cloud).
class SAL_DLLPUBLIC_EXPORT LocalSpeechHub
{
public:
    static SpeechCapability diagnose();

    /// Long-form meeting capture (default up to 2h). Toggle: start if idle, stop if active.
    static bool isMeetingRecording();
    static OUString meetingStatusHint();
    /// Start meeting mic capture → rStatusOut message. Returns false if failed.
    static bool startMeeting(OUString& rStatusOut, sal_Int32 nMaxSec = 7200);
    /// Stop; rAudioPath = wav path (may be empty). Returns true if file usable.
    static bool stopMeeting(OUString& rAudioPath, OUString& rStatusOut);
    static void cancelMeeting();

    /**
     * Live meeting: ffmpeg segment capture + progressive STT (边录边出字).
     * Prefer this when whisper is available. Poll pollLiveDelta() on a timer.
     */
    static bool isLiveMeeting();
    static bool startLiveMeeting(OUString& rStatusOut, sal_Int32 nChunkSec = 6,
                                 sal_Int32 nMaxSec = 7200);
    /// Newly transcribed text since last poll (may be empty). Non-blocking if no chunk ready.
    static OUString pollLiveDelta(OUString& rStatusOut);
    /// Stop live session; rAudioPath = concatenated/last wav; rFullText = all deltas + tail.
    static bool stopLiveMeeting(OUString& rAudioPath, OUString& rFullText, OUString& rStatusOut);
    static OUString liveSessionDir();

    /// TTS: speak plain text with system voice (macOS say). Non-blocking.
    static OUString speakPlain(const OUString& rText, const OUString& rVoice = OUString());
    /// TTS export AIFF to Downloads. Returns status path message.
    static OUString exportPlainAiff(const OUString& rText, const OUString& rVoice = OUString());

    static OUString meetingsDir();
};

} // namespace kqoffice::ai::notebook

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
