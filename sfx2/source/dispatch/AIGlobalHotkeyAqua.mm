/* -*- Mode: ObjC++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Global hotkeys — Spokenly-inspired voice UX (macOS).
 *
 *   F4 / Cmd+Shift+Space  → hold-to-talk (Carbon hotkey press/release)
 *   Right Command alone   → hold-to-talk (FlagsChanged local+global monitor)
 *   Cmd+Shift+A           → region screenshot
 *   Cmd+Shift+E           → work pendant
 *
 * Prefs (~/.config/kqoffice/ai-input-prefs.json):
 *   voiceHoldToTalk, voiceShowHud, voiceHotkey=all|f4|cmd-shift-space|right-cmd
 *
 * Hot-reload: kqoffice_ai_reload_global_hotkeys() re-reads prefs and
 * rebinds voice hotkeys without restart (wired from DocumentAIInputPrefs::save).
 *
 * Right-⌘ global monitor needs Accessibility trust when app is not focused.
 */

#include <sal/config.h>
#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <comphelper/solarmutex.hxx>

#include <premac.h>
#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include <postmac.h>

#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <unistd.h>

extern "C" void kqoffice_ai_trigger_voice();
extern "C" void kqoffice_ai_trigger_voice_press();
extern "C" void kqoffice_ai_trigger_voice_release();
extern "C" void kqoffice_ai_trigger_screenshot_region();
extern "C" void kqoffice_work_show_pendant();

namespace
{
EventHotKeyRef g_hotkeyShot = nullptr;
EventHotKeyRef g_hotkeyVoiceF4 = nullptr;
EventHotKeyRef g_hotkeyVoiceSpace = nullptr;
EventHotKeyRef g_hotkeyPendant = nullptr;
EventHandlerRef g_handlerRef = nullptr;
EventHandlerUPP g_handlerUPP = nullptr;
bool g_baseInstalled = false; ///< handler + shot/pendant once
bool g_voiceKeyDown = false;
bool g_rightCmdDown = false;
bool g_rebindInFlight = false;
id g_localFlagsMon = nil;
id g_globalFlagsMon = nil;
dispatch_source_t g_prefsWatch = nullptr;
int g_prefsFd = -1;

NSPanel* g_voiceHud = nil;
NSTextField* g_voiceHudLabel = nil;

// Device-dependent right command mask (common on macOS)
constexpr NSUInteger kRightCmdDeviceMask = 0x00000010;

struct VoiceHotPrefs
{
    bool hold = true;
    bool hud = true;
    bool useF4 = true;
    bool useSpace = true;
    bool useRightCmd = true;
};

std::string readPrefsFile()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return {};
    NSString* path = [NSString stringWithFormat:@"%s/.config/kqoffice/ai-input-prefs.json", home];
    NSError* err = nil;
    NSString* body = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding
                                                  error:&err];
    if (!body)
        return {};
    return std::string([body UTF8String]);
}

bool jsonBool(const std::string& j, const char* key, bool def)
{
    const std::string needle = std::string("\"") + key + "\"";
    auto p = j.find(needle);
    if (p == std::string::npos)
        return def;
    p = j.find(':', p);
    if (p == std::string::npos)
        return def;
    auto t = j.find("true", p);
    auto f = j.find("false", p);
    if (t != std::string::npos && (f == std::string::npos || t < f) && t < p + 16)
        return true;
    if (f != std::string::npos && f < p + 16)
        return false;
    return def;
}

std::string jsonString(const std::string& j, const char* key)
{
    const std::string needle = std::string("\"") + key + "\"";
    auto p = j.find(needle);
    if (p == std::string::npos)
        return {};
    p = j.find(':', p);
    if (p == std::string::npos)
        return {};
    p = j.find('"', p + 1);
    if (p == std::string::npos)
        return {};
    auto q = j.find('"', p + 1);
    if (q == std::string::npos)
        return {};
    return j.substr(p + 1, q - p - 1);
}

VoiceHotPrefs loadVoiceHotPrefs()
{
    VoiceHotPrefs p;
    const std::string j = readPrefsFile();
    if (j.empty())
        return p;
    p.hold = jsonBool(j, "voiceHoldToTalk", true);
    p.hud = jsonBool(j, "voiceShowHud", true);
    std::string hk = jsonString(j, "voiceHotkey");
    for (auto& c : hk)
        c = static_cast<char>(tolower(c));
    if (hk == "f4")
    {
        p.useF4 = true;
        p.useSpace = false;
        p.useRightCmd = false;
    }
    else if (hk == "cmd-shift-space")
    {
        p.useF4 = false;
        p.useSpace = true;
        p.useRightCmd = false;
    }
    else if (hk == "right-cmd")
    {
        p.useF4 = false;
        p.useSpace = false;
        p.useRightCmd = true;
    }
    else
    {
        p.useF4 = p.useSpace = p.useRightCmd = true;
    }
    return p;
}

/// Lightweight status for smoke / live verification (no PII).
void writeHotkeyStatus(const VoiceHotPrefs& pref, const char* reason)
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return;
    NSString* dir = [NSString stringWithFormat:@"%s/.config/kqoffice", home];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    NSString* path = [dir stringByAppendingPathComponent:@"hotkey-status.json"];
    NSMutableArray* keys = [NSMutableArray array];
    if (pref.useF4)
        [keys addObject:@"f4"];
    if (pref.useSpace)
        [keys addObject:@"cmd-shift-space"];
    if (pref.useRightCmd)
        [keys addObject:@"right-cmd"];
    NSString* keysJoined = [keys componentsJoinedByString:@","];
    const long ts = static_cast<long>(std::time(nullptr));
    NSString* body = [NSString
        stringWithFormat:
            @"{\n  \"schema\": \"v1-hotkey-status\",\n  \"reason\": \"%s\",\n  \"keys\": \"%@\",\n"
            @"  \"hold\": %s,\n  \"hud\": %s,\n  \"ts\": %ld\n}\n",
            reason ? reason : "unknown", keysJoined, pref.hold ? "true" : "false",
            pref.hud ? "true" : "false", ts];
    [body writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

void ensureVoiceHud()
{
    if (g_voiceHud)
        return;
    NSRect frame = NSMakeRect(0, 0, 360, 68);
    g_voiceHud = [[NSPanel alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    g_voiceHud.level = NSFloatingWindowLevel;
    g_voiceHud.opaque = NO;
    g_voiceHud.backgroundColor = [[NSColor blackColor] colorWithAlphaComponent:0.80];
    g_voiceHud.hasShadow = YES;
    g_voiceHud.ignoresMouseEvents = YES;
    g_voiceHud.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                                    | NSWindowCollectionBehaviorFullScreenAuxiliary;

    g_voiceHudLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 18, 336, 32)];
    g_voiceHudLabel.bezeled = NO;
    g_voiceHudLabel.drawsBackground = NO;
    g_voiceHudLabel.editable = NO;
    g_voiceHudLabel.selectable = NO;
    g_voiceHudLabel.alignment = NSTextAlignmentCenter;
    g_voiceHudLabel.textColor = [NSColor whiteColor];
    g_voiceHudLabel.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    g_voiceHudLabel.stringValue = @"🎤 正在听… 松开结束";
    [g_voiceHud.contentView addSubview:g_voiceHudLabel];
}

void showVoiceHud(NSString* msg)
{
    const VoiceHotPrefs pref = loadVoiceHotPrefs();
    if (!pref.hud)
        return;
    ensureVoiceHud();
    if (msg.length)
        g_voiceHudLabel.stringValue = msg;
    NSScreen* screen = [NSScreen mainScreen];
    NSRect vis = screen.visibleFrame;
    NSRect wf = g_voiceHud.frame;
    wf.origin.x = vis.origin.x + (vis.size.width - wf.size.width) / 2.0;
    wf.origin.y = vis.origin.y + vis.size.height * 0.12;
    [g_voiceHud setFrame:wf display:YES];
    [g_voiceHud orderFrontRegardless];
}

void hideVoiceHud()
{
    if (g_voiceHud)
        [g_voiceHud orderOut:nil];
}

void flashVoiceHud(NSString* msg)
{
    const VoiceHotPrefs pref = loadVoiceHotPrefs();
    if (!pref.hud)
        return;
    showVoiceHud(msg);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.1 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     hideVoiceHud();
                   });
}

void voiceStartFromHotkey()
{
    if (g_voiceKeyDown)
        return;
    g_voiceKeyDown = true;
    showVoiceHud(@"🎤 正在听… 松开快捷键出字（本机 · 未上传）");
    const VoiceHotPrefs pref = loadVoiceHotPrefs();
    if (pref.hold)
        kqoffice_ai_trigger_voice_press();
    else
        kqoffice_ai_trigger_voice();
}

void voiceEndFromHotkey()
{
    if (!g_voiceKeyDown)
        return;
    g_voiceKeyDown = false;
    hideVoiceHud();
    const VoiceHotPrefs pref = loadVoiceHotPrefs();
    if (pref.hold)
    {
        kqoffice_ai_trigger_voice_release();
        flashVoiceHud(@"✓ 语音处理中…");
    }
}

void handleRightCommandFlags(NSEvent* e)
{
    const VoiceHotPrefs pref = loadVoiceHotPrefs();
    if (!pref.useRightCmd)
        return;

    // Prefer keyCode for Right Command (kVK_RightCommand = 54)
    BOOL isRightCmdEvent = (e.keyCode == kVK_RightCommand);
    BOOL rightDown = NO;
    if (isRightCmdEvent)
    {
        const NSUInteger flags = e.modifierFlags;
        if ((flags & kRightCmdDeviceMask) != 0)
            rightDown = YES;
        else if (e.keyCode == kVK_RightCommand)
            rightDown = NO;
        else
            rightDown = (flags & NSEventModifierFlagCommand) != 0;
    }
    else
    {
        rightDown = (e.modifierFlags & kRightCmdDeviceMask) != 0;
    }

    if (rightDown && !g_rightCmdDown)
    {
        g_rightCmdDown = YES;
        dispatch_async(dispatch_get_main_queue(), ^{
          SolarMutexGuard aGuard;
          voiceStartFromHotkey();
        });
    }
    else if (!rightDown && g_rightCmdDown)
    {
        g_rightCmdDown = NO;
        dispatch_async(dispatch_get_main_queue(), ^{
          SolarMutexGuard aGuard;
          voiceEndFromHotkey();
        });
    }
}

OSStatus hotKeyHandler(EventHandlerCallRef /*next*/, EventRef event, void* /*userData*/)
{
    EventHotKeyID hkId{};
    GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hkId),
                      nullptr, &hkId);

    const UInt32 kind = GetEventKind(event);
    const bool isPress = (kind == kEventHotKeyPressed);
    const bool isRelease = (kind == kEventHotKeyReleased);
    const VoiceHotPrefs pref = loadVoiceHotPrefs();

    dispatch_async(dispatch_get_main_queue(), ^{
      SolarMutexGuard aGuard;
      if (hkId.id == 1 && isPress)
      {
          kqoffice_ai_trigger_screenshot_region();
          return;
      }
      if (hkId.id == 3 && isPress)
      {
          kqoffice_work_show_pendant();
          return;
      }
      // Voice: 2=F4, 4=Cmd+Shift+Space
      if (hkId.id == 2 || hkId.id == 4)
      {
          if (hkId.id == 2 && !pref.useF4)
              return;
          if (hkId.id == 4 && !pref.useSpace)
              return;
          if (isPress)
              voiceStartFromHotkey();
          else if (isRelease)
              voiceEndFromHotkey();
      }
    });
    return noErr;
}

void removeRightCommandMonitors()
{
    if (g_localFlagsMon)
    {
        [NSEvent removeMonitor:g_localFlagsMon];
        g_localFlagsMon = nil;
    }
    if (g_globalFlagsMon)
    {
        [NSEvent removeMonitor:g_globalFlagsMon];
        g_globalFlagsMon = nil;
    }
}

void installRightCommandMonitors()
{
    if (g_localFlagsMon)
        return;
    g_localFlagsMon = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskFlagsChanged
                                     handler:^NSEvent*(NSEvent* e) {
                                       handleRightCommandFlags(e);
                                       return e;
                                     }];
    // Global: requires Accessibility when not focused (Spokenly same)
    g_globalFlagsMon = [NSEvent
        addGlobalMonitorForEventsMatchingMask:NSEventMaskFlagsChanged
                                      handler:^(NSEvent* e) {
                                        handleRightCommandFlags(e);
                                      }];
}

void unregisterVoiceHotkeysOnly()
{
    // Finish in-flight hold so we don't leave mic stuck after rebind
    if (g_voiceKeyDown)
        voiceEndFromHotkey();
    g_rightCmdDown = false;

    if (g_hotkeyVoiceF4)
    {
        UnregisterEventHotKey(g_hotkeyVoiceF4);
        g_hotkeyVoiceF4 = nullptr;
    }
    if (g_hotkeyVoiceSpace)
    {
        UnregisterEventHotKey(g_hotkeyVoiceSpace);
        g_hotkeyVoiceSpace = nullptr;
    }
    removeRightCommandMonitors();
}

void registerVoiceHotkeysFromPrefs(const VoiceHotPrefs& pref)
{
    if (pref.useF4 && !g_hotkeyVoiceF4)
    {
        EventHotKeyID idVoice{};
        idVoice.signature = 'kqAi';
        idVoice.id = 2;
        RegisterEventHotKey(0x76 /* F4 */, 0, idVoice, GetApplicationEventTarget(), 0,
                            &g_hotkeyVoiceF4);
    }
    if (pref.useSpace && !g_hotkeyVoiceSpace)
    {
        EventHotKeyID idVoice2{};
        idVoice2.signature = 'kqAi';
        idVoice2.id = 4;
        RegisterEventHotKey(0x31 /* Space */, cmdKey | shiftKey, idVoice2,
                            GetApplicationEventTarget(), 0, &g_hotkeyVoiceSpace);
    }
    if (pref.useRightCmd)
        installRightCommandMonitors();
}

// Forward decl — defined after namespace close as extern C, used by prefs watch
void rebindVoiceHotkeysNow(const char* reason);

void startPrefsFileWatch(); // re-arm after atomic replace

void armPrefsFileWatch()
{
    if (g_prefsWatch)
        return;
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return;
    NSString* dir = [NSString stringWithFormat:@"%s/.config/kqoffice", home];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    NSString* path = [dir stringByAppendingPathComponent:@"ai-input-prefs.json"];
    // Only create a shell when truly missing — never clobber existing prefs
    if (![[NSFileManager defaultManager] fileExistsAtPath:path])
    {
        [@"{\n  \"schema_version\": \"v1-input-prefs\"\n}\n" writeToFile:path
                                                              atomically:YES
                                                                encoding:NSUTF8StringEncoding
                                                                   error:nil];
    }
    g_prefsFd = open([path fileSystemRepresentation], O_EVTONLY);
    if (g_prefsFd < 0)
        return;
    g_prefsWatch = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE, g_prefsFd,
                                          DISPATCH_VNODE_WRITE | DISPATCH_VNODE_RENAME
                                              | DISPATCH_VNODE_DELETE | DISPATCH_VNODE_ATTRIB,
                                          dispatch_get_main_queue());
    if (!g_prefsWatch)
    {
        close(g_prefsFd);
        g_prefsFd = -1;
        return;
    }
    dispatch_source_set_event_handler(g_prefsWatch, ^{
      const unsigned long flags = dispatch_source_get_data(g_prefsWatch);
      // Debounce rapid double-writes (save callback + vnode)
      static NSTimeInterval s_last = 0;
      const NSTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
      if (now - s_last < 0.20)
          return;
      s_last = now;

      rebindVoiceHotkeysNow("prefs-watch");

      // Atomic writes rename → old inode dies; re-arm watch on new file
      if (flags & (DISPATCH_VNODE_DELETE | DISPATCH_VNODE_RENAME))
      {
          dispatch_source_t old = g_prefsWatch;
          g_prefsWatch = nullptr;
          if (old)
              dispatch_source_cancel(old);
          // Re-arm after short delay so new file exists
          dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)),
                         dispatch_get_main_queue(), ^{ armPrefsFileWatch(); });
      }
    });
    dispatch_source_set_cancel_handler(g_prefsWatch, ^{
      if (g_prefsFd >= 0)
      {
          close(g_prefsFd);
          g_prefsFd = -1;
      }
    });
    dispatch_resume(g_prefsWatch);
}

void startPrefsFileWatch() { armPrefsFileWatch(); }

void ensureBaseHotkeys()
{
    if (g_baseInstalled)
        return;

    EventTypeSpec specs[2];
    specs[0].eventClass = kEventClassKeyboard;
    specs[0].eventKind = kEventHotKeyPressed;
    specs[1].eventClass = kEventClassKeyboard;
    specs[1].eventKind = kEventHotKeyReleased;

    g_handlerUPP = NewEventHandlerUPP(hotKeyHandler);
    InstallApplicationEventHandler(g_handlerUPP, 2, specs, nullptr, &g_handlerRef);

    EventHotKeyID idShot{};
    idShot.signature = 'kqAi';
    idShot.id = 1;
    RegisterEventHotKey(0x00 /* A */, cmdKey | shiftKey, idShot, GetApplicationEventTarget(), 0,
                        &g_hotkeyShot);

    EventHotKeyID idPendant{};
    idPendant.signature = 'kqAi';
    idPendant.id = 3;
    RegisterEventHotKey(0x0E /* E */, cmdKey | shiftKey, idPendant, GetApplicationEventTarget(), 0,
                        &g_hotkeyPendant);

    startPrefsFileWatch();
    g_baseInstalled = true;
}

void rebindVoiceHotkeysNow(const char* reason)
{
    if (g_rebindInFlight)
        return;
    g_rebindInFlight = true;
    ensureBaseHotkeys();
    unregisterVoiceHotkeysOnly();
    const VoiceHotPrefs pref = loadVoiceHotPrefs();
    registerVoiceHotkeysFromPrefs(pref);
    writeHotkeyStatus(pref, reason ? reason : "reload");
    g_rebindInFlight = false;
    SAL_INFO("sfx.appl",
             "kqoffice Spokenly hotkeys reloaded (F4 / ⌘⇧Space / 右⌘ · no restart)");
}
} // namespace

extern "C" void kqoffice_ai_reload_global_hotkeys()
{
    // Must run on main for Carbon / NSEvent monitors
    auto apply = ^{ rebindVoiceHotkeysNow("reload"); };

    if ([NSThread isMainThread])
    {
        apply();
    }
    else
    {
        dispatch_sync(dispatch_get_main_queue(), apply);
    }
}

extern "C" void kqoffice_ai_register_global_hotkeys()
{
    auto apply = ^{
      ensureBaseHotkeys();
      rebindVoiceHotkeysNow("register");
      SAL_INFO("sfx.appl",
               "kqoffice Spokenly hotkeys ready (F4 / ⌘⇧Space / 右⌘ hold-to-talk, ⌘⇧A, ⌘⇧E)");
    };

    if ([NSThread isMainThread])
    {
        apply();
    }
    else
    {
        dispatch_async(dispatch_get_main_queue(), apply);
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
