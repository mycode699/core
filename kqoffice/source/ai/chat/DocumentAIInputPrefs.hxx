/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Input efficiency prefs: voice + screenshot (WeChat-like workflow).
 * Persist: ~/.config/kqoffice/ai-input-prefs.json
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIINPUTPREFS_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIINPUTPREFS_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

/// Voice transcription backend preference.
enum class VoiceBackend
{
    SystemDictation, ///< OS dictation (Fn Fn / Win+H); default, no cloud from us
    LocalCommand, ///< KQOFFICE_AI_VOICE_CMD or prefs.voiceCmd → stdout text
    PushToTalkRecord ///< Record audio then run STT command with $AUDIO
};

/// Default screenshot capture mode.
enum class ScreenshotMode
{
    Region, ///< Interactive region (like WeChat)
    Window,
    Fullscreen
};

struct DocumentAIInputPrefs
{
    // —— Voice ——
    bool voiceEnabled = true;
    VoiceBackend voiceBackend = VoiceBackend::SystemDictation;
    OUString voiceCmd; ///< local STT; may contain $AUDIO for recorded wav path
    bool voicePushToTalk = true; ///< click start / click end (WeChat-style)
    bool voiceAutoSend = false; ///< never default-true (approve/send explicit)
    bool voiceShowFnHint = true; ///< surface system Fn dictation tip
    sal_Int32 voiceMaxRecordSec = 60;

    // —— Screenshot ——
    bool screenshotEnabled = true;
    ScreenshotMode screenshotMode = ScreenshotMode::Region;
    bool screenshotAutoAttachChat = true; ///< insert @截图:path into prompt
    bool screenshotOpenAiPanel = true; ///< open AI deck after capture
    bool screenshotCopyClipboard = true;
    OUString screenshotDir; ///< empty → ~/.config/kqoffice/captures

    static OUString defaultConfigPath();
    static DocumentAIInputPrefs load();
    static bool save(const DocumentAIInputPrefs& r);
    static OUString voiceBackendToString(VoiceBackend e);
    static VoiceBackend voiceBackendFromString(const OUString& s);
    static OUString screenshotModeToString(ScreenshotMode e);
    static ScreenshotMode screenshotModeFromString(const OUString& s);
    static OUString resolveCaptureDir(const DocumentAIInputPrefs& r);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
