/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Global voice + screenshot entry (WeChat-like efficiency).
 */
#pragma once

#include <rtl/ustring.hxx>
#include <sal/types.h>
#include <sfx2/dllapi.h>
#include <tools/link.hxx>
#include <vcl/timer.hxx>

class SfxViewFrame;

namespace sfx2
{
class SFX2_DLLPUBLIC AIInputDispatcher
{
public:
    static AIInputDispatcher& Get();

    /// Toggle push-to-talk / trigger voice; fills AI prompt when text ready.
    void TriggerVoice(SfxViewFrame* pFrame);

    /// Spokenly-style hold-to-talk (global hotkey press / release).
    void TriggerVoicePress(SfxViewFrame* pFrame);
    void TriggerVoiceRelease(SfxViewFrame* pFrame);

    /// Re-read voiceHotkey prefs and rebind F4 / ⌘⇧Space / 右⌘ without restart.
    void ReloadHotkeys();

    /// Interactive screenshot (default prefs mode).
    void TriggerScreenshot(SfxViewFrame* pFrame);

    /// Explicit region / window / fullscreen.
    void TriggerScreenshotRegion(SfxViewFrame* pFrame);
    void TriggerScreenshotWindow(SfxViewFrame* pFrame);
    void TriggerScreenshotFull(SfxViewFrame* pFrame);

    /// Select-to-act: open AI deck and run a one-shot intent on current selection.
    /// rIntentId: rewrite | formal | shorten | expand | summarize (default formal).
    void SendSelectionToAi(SfxViewFrame* pFrame, const OUString& rIntentId = OUString());

    /// Document-level Writer assist (no selection required).
    /// rIntentId: outline | proofread | continue | doc-summary | structure.
    void RunDocumentAssist(SfxViewFrame* pFrame, const OUString& rIntentId);

    /// Calc assist (selection preferred).
    /// rIntentId: formula | clean | interpret | aggregate.
    void RunCalcAssist(SfxViewFrame* pFrame, const OUString& rIntentId);

    /// Impress assist (controlled outline / speaker notes / page rewrite).
    /// rIntentId: outline | notes | page.
    void RunImpressAssist(SfxViewFrame* pFrame, const OUString& rIntentId);

private:
    AIInputDispatcher();
    void openAiDeck(SfxViewFrame* pFrame);
    void injectIntoAiPrompt(const OUString& rText, bool bOpenDeck, SfxViewFrame* pFrame);
    /// Poll pending-prompt-inject for membership slash when AI panel is not open.
    void PollMembershipInject();
    DECL_LINK(OnMembershipInjectPoll, Timer*, void);

    AutoTimer m_aMembershipInjectPoll;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
