/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * WeChat-like local-first voice input for AI chat:
 * - System dictation (Fn Fn / Win+H) with in-app guidance
 * - Local STT command (prefs.voiceCmd / KQOFFICE_AI_VOICE_CMD)
 * - Push-to-talk: start → record → stop → transcribe ($AUDIO)
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIVOICEINPUT_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIVOICEINPUT_HXX

#include <DocumentAIInputPrefs.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

struct VoiceCaptureResult
{
    bool success = false;
    /// env-text | voice-cmd | push-to-talk | system-dictation | listening | idle | error
    OUString source;
    OUString text;
    OUString message;
    bool listening = false; ///< true while PTT session active
};

/// Voice entry with push-to-talk state (process-local).
class SAL_DLLPUBLIC_EXPORT DocumentAIVoiceInput
{
public:
    static bool isEnabled();
    static OUString statusHint();

    /// One-shot: env text / local cmd / or system-dictation guidance.
    static VoiceCaptureResult captureOnce();

    /// WeChat-style toggle: start listening if idle, stop+transcribe if active.
    static VoiceCaptureResult togglePushToTalk();

    static bool isListening();
    static void cancelListening();

    /// Try to open OS dictation UI (best-effort, platform-specific).
    static bool requestSystemDictation();
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
