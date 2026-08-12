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
    // —— Voice (Spokenly-inspired) ——
    bool voiceEnabled = true;
    /// Default PushToTalk: local mic + STT. System dictation remains available.
    VoiceBackend voiceBackend = VoiceBackend::PushToTalkRecord;
    OUString voiceCmd; ///< local STT; may contain $AUDIO for recorded wav path
    bool voicePushToTalk = true; ///< click start / click end
    /// Hold-to-talk (Spokenly): key down = start, key up = stop+transcribe.
    /// false = toggle mode (press to start, press again to stop).
    bool voiceHoldToTalk = true;
    bool voiceAutoSend = false; ///< never default-true (approve/send explicit)
    bool voiceShowFnHint = true; ///< surface system Fn dictation tip
    bool voiceShowHud = true; ///< floating “正在听…” HUD while recording
    /// Spokenly-style hotkey set: all | f4 | cmd-shift-space | right-cmd
    OUString voiceHotkey = u"all"_ustr;
    sal_Int32 voiceMaxRecordSec = 120;

    // —— Screenshot ——
    bool screenshotEnabled = true;
    ScreenshotMode screenshotMode = ScreenshotMode::Region;
    bool screenshotAutoAttachChat = true; ///< insert @截图:path into prompt
    bool screenshotOpenAiPanel = true; ///< open AI deck after capture
    bool screenshotCopyClipboard = true;
    OUString screenshotDir; ///< empty → ~/.config/kqoffice/captures

    // —— Scheduled tasks (DuMate-style 定时任务) ——
    /// When true, due-task inject into AI prompt auto-submits. Default false:
    /// safer human-in-the-loop (user edits then sends).
    bool scheduleAutoSend = false;

    // —— Local material OCR / multi-format read (closed loop) ——
    /// Enable local OCR for @截图/@文件 images (default true; no cloud).
    bool ocrEnabled = true;
    /// Optional OCR command template; $IMAGE or $FILE = path. Empty → env
    /// KQOFFICE_AI_OCR_CMD or auto-detect `tesseract`.
    OUString ocrCmd;

    // —— Cursor-style inline edit / ghost complete ——
    /// When true, Ctrl+. / Ctrl+K complete asks for 2 variants; false = single line (faster).
    bool inlineMultiVariant = true;
    /// Max wait for ghost/complete light-slot (ms). Edit mode still uses longer budget.
    sal_Int32 inlineGhostTimeoutMs = 12000;
    /// Soft max chars of before-context fed to complete prompt.
    sal_Int32 inlineContextChars = 600;
    /// Reserved: typing-idle auto ghost (default off; requires edit-surface hook).
    bool inlineAutoGhost = false;
    /// Idle ms after last key before auto ghost fires (only if inlineAutoGhost).
    sal_Int32 inlineAutoGhostIdleMs = 900;
    /// Writer complete mode: try in-document ExtTextInput gray composition (feature flag).
    /// Default on; falls back to caret Popover tip. Still Tab-to-accept; reject clears without write.
    bool inlineExtTextGhost = true;

    // —— Apply evidence (local screenshots, no upload) ——
    /// Capture passive fullscreen PNG before/after approved write-back (local evidence).
    bool applyCaptureEvidence = true;
    /// After pre/post shots: light-slot *text* commentary on expected visual delta (no image upload).
    bool applyVisionDescribe = true;
    /// Prefer local multimodal (Ollama loopback / visionCmd) reading pre/post PNG bytes.
    /// Still never uploads images to public cloud; falls back to text describe.
    bool applyVisionLocalMultimodal = true;
    /// Optional vision model tag (empty → KQOFFICE_AI_VISION_MODEL or llava).
    OUString visionModel;
    /// Optional local command template with $PRE $POST (stdout = describe text).
    OUString visionCmd;

    // —— Chat streaming ——
    /// Prefer SSE/NDJSON token streaming in AI panel (default on; local-model friendly).
    bool chatStreamingDefault = true;

    // —— Task bootstrap (shengji semantic start) ——
    /// When rule confidence is low on Required turns, call light slot to refine restatement.
    bool taskBootstrapModelRefine = true;
    /// Confidence threshold below which model refine runs (0–100).
    sal_Int32 taskBootstrapRefineBelow = 55;

    // —— Enterprise connectors (default OFF) ——
    /// Master switch; even when true each connector needs enabled+granted+approval.
    bool enterpriseConnectorsEnabled = false;

    static OUString defaultConfigPath();
    static DocumentAIInputPrefs load();
    static bool save(const DocumentAIInputPrefs& r);

    /// Optional callback after a successful save (e.g. rebind Spokenly hotkeys).
    /// Set from sfx at startup; safe no-op if unset.
    using OnSavedCallback = void (*)();
    static void setOnSavedCallback(OnSavedCallback fn);

    static OUString voiceBackendToString(VoiceBackend e);
    static VoiceBackend voiceBackendFromString(const OUString& s);
    static OUString screenshotModeToString(ScreenshotMode e);
    static ScreenshotMode screenshotModeFromString(const OUString& s);
    static OUString resolveCaptureDir(const DocumentAIInputPrefs& r);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
