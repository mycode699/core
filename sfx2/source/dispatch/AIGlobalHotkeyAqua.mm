/* -*- Mode: ObjC++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Global hotkeys for AI voice + screenshot (macOS).
 * Works while 可圈office is running (including background).
 *
 *   Cmd+Shift+A  → region screenshot
 *   F4           → voice push-to-talk toggle
 *   Cmd+Shift+E  → efficiency pendant (工作挂坠)
 *
 * Note: true "Fn long-press" is owned by system Dictation preferences;
 * we document + guide users there. Carbon hotkeys cover app-level WeChat-like keys.
 */

#include <sal/config.h>
#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <comphelper/solarmutex.hxx>

#include <premac.h>
#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <postmac.h>

extern "C" void kqoffice_ai_trigger_voice();
extern "C" void kqoffice_ai_trigger_screenshot_region();
extern "C" void kqoffice_work_show_pendant();

namespace
{
EventHotKeyRef g_hotkeyShot = nullptr;
EventHotKeyRef g_hotkeyVoice = nullptr;
EventHotKeyRef g_hotkeyPendant = nullptr;
EventHandlerRef g_handlerRef = nullptr;
bool g_registered = false;

OSStatus hotKeyHandler(EventHandlerCallRef /*next*/, EventRef event, void* /*userData*/)
{
    EventHotKeyID hkId{};
    GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hkId),
                      nullptr, &hkId);

    // Dispatch on main with SolarMutex (LibreOffice UI thread).
    dispatch_async(dispatch_get_main_queue(), ^{
      SolarMutexGuard aGuard;
      if (hkId.id == 1)
          kqoffice_ai_trigger_screenshot_region();
      else if (hkId.id == 2)
          kqoffice_ai_trigger_voice();
      else if (hkId.id == 3)
          kqoffice_work_show_pendant();
    });
    return noErr;
}
} // namespace

extern "C" void kqoffice_ai_register_global_hotkeys()
{
    if (g_registered)
        return;
    g_registered = true;

    EventTypeSpec specs[2];
    specs[0].eventClass = kEventClassKeyboard;
    specs[0].eventKind = kEventHotKeyPressed;
    specs[1].eventClass = kEventClassKeyboard;
    specs[1].eventKind = kEventHotKeyReleased; // reserved for future hold-to-talk

    InstallApplicationEventHandler(NewEventHandlerUPP(hotKeyHandler), 2, specs, nullptr,
                                   &g_handlerRef);

    // Cmd+Shift+A — region screenshot (WeChat-like)
    // kVK_ANSI_A = 0x00; cmdKey=256, shiftKey=512
    EventHotKeyID idShot{};
    idShot.signature = 'kqAi';
    idShot.id = 1;
    RegisterEventHotKey(0x00 /* A */, cmdKey | shiftKey, idShot, GetApplicationEventTarget(), 0,
                        &g_hotkeyShot);

    // F4 — voice (kVK_F4 = 0x76)
    EventHotKeyID idVoice{};
    idVoice.signature = 'kqAi';
    idVoice.id = 2;
    RegisterEventHotKey(0x76 /* F4 */, 0, idVoice, GetApplicationEventTarget(), 0, &g_hotkeyVoice);

    // Cmd+Shift+E — efficiency pendant (效率挂坠)
    // kVK_ANSI_E = 0x0E
    EventHotKeyID idPendant{};
    idPendant.signature = 'kqAi';
    idPendant.id = 3;
    RegisterEventHotKey(0x0E /* E */, cmdKey | shiftKey, idPendant, GetApplicationEventTarget(), 0,
                        &g_hotkeyPendant);

    SAL_INFO("sfx.appl",
             "kqoffice AI global hotkeys registered (Cmd+Shift+A, F4, Cmd+Shift+E pendant)");
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
