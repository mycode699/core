/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Desktop screenshot capture for AI chat (WeChat-like):
 * region / window / fullscreen. Local file only; no cloud upload.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAISCREENCAPTURE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAISCREENCAPTURE_HXX

#include <DocumentAIInputPrefs.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

struct ScreenCaptureResult
{
    bool success = false;
    OUString mode; ///< region | window | fullscreen
    OUString path; ///< system path to PNG
    OUString fileUrl; ///< file:// URL when available
    OUString message;
    /// Markup to insert into AI prompt (e.g. @截图:/path)
    OUString promptAttachment;
};

class SAL_DLLPUBLIC_EXPORT DocumentAIScreenCapture
{
public:
    /// Capture using prefs default mode.
    static ScreenCaptureResult captureInteractive();

    /// Capture with explicit mode.
    static ScreenCaptureResult capture(ScreenshotMode eMode);

    /// True if platform capture tool appears available.
    static bool isAvailable();

    /// Short UI hint.
    static OUString statusHint();
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
