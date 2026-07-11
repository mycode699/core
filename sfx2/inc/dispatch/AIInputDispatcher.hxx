/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Global voice + screenshot entry (WeChat-like efficiency).
 */
#pragma once

#include <rtl/ustring.hxx>
#include <sal/types.h>
#include <sfx2/dllapi.h>

class SfxViewFrame;

namespace sfx2
{
class SFX2_DLLPUBLIC AIInputDispatcher
{
public:
    static AIInputDispatcher& Get();

    /// Toggle push-to-talk / trigger voice; fills AI prompt when text ready.
    void TriggerVoice(SfxViewFrame* pFrame);

    /// Interactive screenshot (default prefs mode).
    void TriggerScreenshot(SfxViewFrame* pFrame);

    /// Explicit region / window / fullscreen.
    void TriggerScreenshotRegion(SfxViewFrame* pFrame);
    void TriggerScreenshotWindow(SfxViewFrame* pFrame);
    void TriggerScreenshotFull(SfxViewFrame* pFrame);

private:
    AIInputDispatcher() = default;
    void openAiDeck(SfxViewFrame* pFrame);
    void injectIntoAiPrompt(const OUString& rText, bool bOpenDeck, SfxViewFrame* pFrame);
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
