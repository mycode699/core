/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <dispatch/AIInputDispatcher.hxx>

#include <DocumentAIInputPrefs.hxx>
#include <DocumentAIScreenCapture.hxx>
#include <DocumentAIVoiceInput.hxx>

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/util/URL.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <comphelper/processfactory.hxx>
#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <sfx2/viewfrm.hxx>
#include <vcl/svapp.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/MessageDialog.hxx>
#include <vcl/weld/Builder.hxx>
#include <cstdlib>

using namespace kqoffice::ai::chat;

namespace sfx2
{
namespace
{
// Last captured screenshot path for panel / prompt injection via env bridge
// (panel polls pending injection file — simple & cross-module).
void queuePromptInjection(const OUString& rText)
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return;
    const OUString path
        = OUString::fromUtf8(home) + u"/.config/kqoffice/pending-prompt-inject"_ustr;
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(path.copy(0, slash), dirUrl)
            == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return;
    osl::File f(url);
    osl::FileBase::RC e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return;
    f.setSize(0);
    const OString utf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
}
} // namespace

#if defined(MACOSX)
extern "C" void kqoffice_ai_register_global_hotkeys();
#endif

AIInputDispatcher& AIInputDispatcher::Get()
{
    static AIInputDispatcher a;
#if defined(MACOSX)
    // Lazy-register global hotkeys once (background-capable on macOS).
    static bool bHotkeys = false;
    if (!bHotkeys)
    {
        bHotkeys = true;
        kqoffice_ai_register_global_hotkeys();
    }
#endif
    return a;
}

void AIInputDispatcher::openAiDeck(SfxViewFrame* pFrame)
{
    if (!pFrame)
        pFrame = SfxViewFrame::Current();
    if (!pFrame)
        return;
    try
    {
        css::util::URL aUrl;
        aUrl.Complete = u".uno:SidebarDeck.AIChatDeck"_ustr;
        auto xTrans = css::util::URLTransformer::create(comphelper::getProcessComponentContext());
        if (xTrans.is())
            xTrans->parseStrict(aUrl);
        css::uno::Reference<css::frame::XDispatchProvider> xProv(
            pFrame->GetFrame().GetFrameInterface(), css::uno::UNO_QUERY);
        if (!xProv.is())
            return;
        auto xDisp = xProv->queryDispatch(aUrl, u"_self"_ustr, 0);
        if (xDisp.is())
            xDisp->dispatch(aUrl, {});
    }
    catch (...)
    {
    }
}

void AIInputDispatcher::injectIntoAiPrompt(const OUString& rText, bool bOpenDeck,
                                           SfxViewFrame* pFrame)
{
    if (rText.isEmpty())
        return;
    queuePromptInjection(rText);
    if (bOpenDeck)
        openAiDeck(pFrame);
}

void AIInputDispatcher::TriggerVoice(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.voiceEnabled)
    {
        weld::Window* pParent = Application::GetDefDialogParent();
        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
            pParent, VclMessageType::Info, VclButtonsType::Ok,
            u"语音输入已关闭。请到「工具 → 选项 → 可圈 AI」开启。"_ustr));
        xBox->run();
        return;
    }

    VoiceCaptureResult cap;
    if (prefs.voicePushToTalk || prefs.voiceBackend == VoiceBackend::PushToTalkRecord)
        cap = DocumentAIVoiceInput::togglePushToTalk();
    else
        cap = DocumentAIVoiceInput::captureOnce();

    if (cap.success && !cap.text.isEmpty())
    {
        injectIntoAiPrompt(cap.text, true, pFrame);
        if (prefs.voiceAutoSend)
        {
            // Still require user glance — do not auto-send by default.
            // Optional future: queue auto-send flag.
        }
    }
    else if (cap.listening)
    {
        openAiDeck(pFrame);
        queuePromptInjection(u""_ustr); // no text yet; panel may show listening status via re-click
    }
    else
    {
        openAiDeck(pFrame);
        // Surface guidance into pending inject as comment? better status only
        weld::Window* pParent = Application::GetDefDialogParent();
        if (!cap.message.isEmpty())
        {
            std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
                pParent, VclMessageType::Info, VclButtonsType::Ok, cap.message));
            xBox->run();
        }
    }
}

void AIInputDispatcher::TriggerScreenshot(SfxViewFrame* pFrame)
{
    TriggerScreenshotRegion(pFrame); // WeChat default: region
}

void AIInputDispatcher::TriggerScreenshotRegion(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.screenshotEnabled)
    {
        weld::Window* pParent = Application::GetDefDialogParent();
        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
            pParent, VclMessageType::Info, VclButtonsType::Ok,
            u"截图已关闭。请到「工具 → 选项 → 可圈 AI」开启。"_ustr));
        xBox->run();
        return;
    }
    // Yield UI so interactive capture can grab screen
    Application::Reschedule(true);
    const ScreenCaptureResult shot
        = DocumentAIScreenCapture::capture(ScreenshotMode::Region);
    if (shot.success && prefs.screenshotAutoAttachChat)
    {
        injectIntoAiPrompt(shot.promptAttachment + u"\n请结合截图说明："_ustr,
                           prefs.screenshotOpenAiPanel, pFrame);
    }
    else if (!shot.success && !shot.message.isEmpty())
    {
        // Cancel is silent-enough; only show hard errors
        if (shot.message.indexOf(u"取消"_ustr) < 0)
        {
            weld::Window* pParent = Application::GetDefDialogParent();
            std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
                pParent, VclMessageType::Warning, VclButtonsType::Ok, shot.message));
            xBox->run();
        }
    }
}

void AIInputDispatcher::TriggerScreenshotWindow(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.screenshotEnabled)
        return;
    Application::Reschedule(true);
    const ScreenCaptureResult shot
        = DocumentAIScreenCapture::capture(ScreenshotMode::Window);
    if (shot.success && prefs.screenshotAutoAttachChat)
        injectIntoAiPrompt(shot.promptAttachment + u"\n请结合截图说明："_ustr,
                           prefs.screenshotOpenAiPanel, pFrame);
}

void AIInputDispatcher::TriggerScreenshotFull(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.screenshotEnabled)
        return;
    Application::Reschedule(true);
    const ScreenCaptureResult shot
        = DocumentAIScreenCapture::capture(ScreenshotMode::Fullscreen);
    if (shot.success && prefs.screenshotAutoAttachChat)
        injectIntoAiPrompt(shot.promptAttachment + u"\n请结合截图说明："_ustr,
                           prefs.screenshotOpenAiPanel, pFrame);
}

} // namespace sfx2

// C ABI for systray / global hotkey (same library; no hard module edges).
extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_trigger_voice()
{
    SolarMutexGuard aGuard;
    sfx2::AIInputDispatcher::Get().TriggerVoice(SfxViewFrame::Current());
}

extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_trigger_screenshot_region()
{
    SolarMutexGuard aGuard;
    sfx2::AIInputDispatcher::Get().TriggerScreenshotRegion(SfxViewFrame::Current());
}

extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_trigger_screenshot_window()
{
    SolarMutexGuard aGuard;
    sfx2::AIInputDispatcher::Get().TriggerScreenshotWindow(SfxViewFrame::Current());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
