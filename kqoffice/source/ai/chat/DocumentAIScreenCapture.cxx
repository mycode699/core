/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <DocumentAIScreenCapture.hxx>

#include <osl/file.hxx>
#include <osl/process.h>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#if !defined(_WIN32)
#include <cstdio>
#include <unistd.h>
#endif

namespace kqoffice::ai::chat
{
namespace
{
OUString makeTimestampName(const OUString& rTag = OUString())
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    const std::time_t sec = static_cast<std::time_t>(tv.Seconds);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &sec);
#else
    localtime_r(&sec, &tm);
#endif
    char buf[96];
    std::snprintf(buf, sizeof(buf), "kq-shot-%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    OUString name = OUString::fromUtf8(buf);
    if (!rTag.isEmpty())
    {
        // Sanitize tag for filename (ASCII-ish).
        OUString tag = rTag;
        tag = tag.replaceAll(u"/"_ustr, u"-"_ustr).replaceAll(u" "_ustr, u"-"_ustr);
        if (tag.getLength() > 32)
            tag = tag.copy(0, 32);
        name += u"-"_ustr + tag;
    }
    name += u".png"_ustr;
    return name;
}

bool ensureDir(const OUString& rSysDir)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysDir, url) != osl::FileBase::E_None)
        return false;
    return osl::Directory::createPath(url) == osl::FileBase::E_None
           || osl::Directory::createPath(url) == osl::FileBase::E_EXIST;
}

bool fileExistsSys(const OUString& rSysPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    return osl::DirectoryItem::get(url, item) == osl::FileBase::E_None;
}

int runShell(const OUString& rCmd)
{
    const OString cmd = OUStringToOString(rCmd, RTL_TEXTENCODING_UTF8);
#if defined(_WIN32)
    return std::system(cmd.getStr());
#else
    return std::system(cmd.getStr());
#endif
}

bool commandExists(const char* name)
{
#if defined(_WIN32)
    (void)name;
    return false;
#else
    OUString cmd = u"command -v "_ustr + OUString::fromUtf8(name) + u" >/dev/null 2>&1"_ustr;
    return runShell(cmd) == 0;
#endif
}

OUString toFileUrl(const OUString& rSysPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) == osl::FileBase::E_None)
        return url;
    return OUString();
}

#if defined(MACOSX)
ScreenCaptureResult captureMac(ScreenshotMode eMode, const OUString& rOutPath)
{
    ScreenCaptureResult out;
    out.mode = DocumentAIInputPrefs::screenshotModeToString(eMode);
    // screencapture flags:
    //  -i interactive (region or window depending on UI)
    //  -s force selection rectangle (region)
    //  -w capture window (click window)
    //  -x no sound
    //  -c also copy to clipboard (we may set separately)
    OUStringBuffer cmd;
    cmd.append(u"screencapture -x "_ustr);
    if (eMode == ScreenshotMode::Region)
        cmd.append(u"-i -s "_ustr);
    else if (eMode == ScreenshotMode::Window)
        cmd.append(u"-i -w "_ustr);
    else
        cmd.append(u""_ustr); // fullscreen immediate
    // Quote path
    cmd.append(u"\""_ustr);
    cmd.append(rOutPath);
    cmd.append(u"\""_ustr);

    const int rc = runShell(cmd.makeStringAndClear());
    if (rc != 0 || !fileExistsSys(rOutPath))
    {
        // User cancel often returns non-zero and no file — soft fail.
        out.success = false;
        out.message = u"截图取消或失败（macOS screencapture）"_ustr;
        return out;
    }
    out.success = true;
    out.path = rOutPath;
    out.fileUrl = toFileUrl(rOutPath);
    out.promptAttachment = u"@截图:"_ustr + rOutPath;
    out.message = u"截图已保存 · "_ustr + rOutPath;
    return out;
}
#endif

#if !defined(MACOSX) && !defined(_WIN32)
ScreenCaptureResult captureLinux(ScreenshotMode eMode, const OUString& rOutPath)
{
    ScreenCaptureResult out;
    out.mode = DocumentAIInputPrefs::screenshotModeToString(eMode);
    OUString cmd;
    if (commandExists("gnome-screenshot"))
    {
        if (eMode == ScreenshotMode::Region)
            cmd = u"gnome-screenshot -a -f \""_ustr + rOutPath + u"\""_ustr;
        else if (eMode == ScreenshotMode::Window)
            cmd = u"gnome-screenshot -w -f \""_ustr + rOutPath + u"\""_ustr;
        else
            cmd = u"gnome-screenshot -f \""_ustr + rOutPath + u"\""_ustr;
    }
    else if (commandExists("import"))
    {
        // ImageMagick
        if (eMode == ScreenshotMode::Region || eMode == ScreenshotMode::Window)
            cmd = u"import \""_ustr + rOutPath + u"\""_ustr;
        else
            cmd = u"import -window root \""_ustr + rOutPath + u"\""_ustr;
    }
    else if (commandExists("scrot"))
    {
        if (eMode == ScreenshotMode::Region)
            cmd = u"scrot -s \""_ustr + rOutPath + u"\""_ustr;
        else
            cmd = u"scrot \""_ustr + rOutPath + u"\""_ustr;
    }
    else
    {
        out.message = u"未找到截图工具（gnome-screenshot / import / scrot）"_ustr;
        return out;
    }
    const int rc = runShell(cmd);
    if (rc != 0 || !fileExistsSys(rOutPath))
    {
        out.message = u"截图取消或失败"_ustr;
        return out;
    }
    out.success = true;
    out.path = rOutPath;
    out.fileUrl = toFileUrl(rOutPath);
    out.promptAttachment = u"@截图:"_ustr + rOutPath;
    out.message = u"截图已保存 · "_ustr + rOutPath;
    return out;
}
#endif
} // namespace

bool DocumentAIScreenCapture::isAvailable()
{
#if defined(MACOSX)
    return commandExists("screencapture");
#elif defined(_WIN32)
    // Win: use PowerShell later; mark available for prefs UI.
    return true;
#else
    return commandExists("gnome-screenshot") || commandExists("import") || commandExists("scrot");
#endif
}

OUString DocumentAIScreenCapture::statusHint()
{
    if (!isAvailable())
        return u"截图：当前环境无可用截图工具（macOS 需 screencapture）"_ustr;
    const auto prefs = DocumentAIInputPrefs::load();
    OUString modeZh = DocumentAIInputPrefs::screenshotModeToString(prefs.screenshotMode);
    if (modeZh == u"region"_ustr)
        modeZh = u"选区"_ustr;
    else if (modeZh == u"window"_ustr)
        modeZh = u"窗口"_ustr;
    else if (modeZh == u"fullscreen"_ustr)
        modeZh = u"全屏"_ustr;
    return u"截图：默认"_ustr + modeZh
           + u" · 本地 PNG · 自动附对话 · 不上传"_ustr;
}

ScreenCaptureResult DocumentAIScreenCapture::captureInteractive()
{
    const auto prefs = DocumentAIInputPrefs::load();
    return capture(prefs.screenshotMode);
}

ScreenCaptureResult DocumentAIScreenCapture::capture(ScreenshotMode eMode)
{
    ScreenCaptureResult out;
    out.mode = DocumentAIInputPrefs::screenshotModeToString(eMode);
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.screenshotEnabled)
    {
        out.message = u"截图已在设置中关闭"_ustr;
        return out;
    }
    if (!isAvailable())
    {
        out.message = u"截图工具不可用"_ustr;
        return out;
    }

    const OUString dir = DocumentAIInputPrefs::resolveCaptureDir(prefs);
    if (!ensureDir(dir))
    {
        out.message = u"无法创建截图目录："_ustr + dir;
        return out;
    }
    const OUString path = dir + u"/"_ustr + makeTimestampName();

#if defined(MACOSX)
    out = captureMac(eMode, path);
#elif defined(_WIN32)
    // PowerShell interactive snipping is limited; use full-screen PrintScreen fallback via .NET
    OUString cmd = u"powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
                   u"Add-Type -AssemblyName System.Drawing; "
                   u"$b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds; "
                   u"$bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height; "
                   u"$g=[System.Drawing.Graphics]::FromImage($bmp); "
                   u"$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size); "
                   u"$bmp.Save('"_ustr
                   + path
                   + u"',[System.Drawing.Imaging.ImageFormat]::Png)\""_ustr;
    if (eMode == ScreenshotMode::Region)
    {
        // Prefer Snipping Tool URI if available (Win10+)
        cmd = u"explorer ms-screenclip:"_ustr;
        runShell(cmd);
        out.success = false;
        out.message = u"已打开系统截图（Win+Shift+S）。完成后可将图片粘贴到 AI 输入框，"
                      u"或改用全屏模式自动保存文件。"_ustr;
        return out;
    }
    const int rc = runShell(cmd);
    if (rc == 0 && fileExistsSys(path))
    {
        out.success = true;
        out.path = path;
        out.fileUrl = toFileUrl(path);
        out.promptAttachment = u"@截图:"_ustr + path;
        out.message = u"截图已保存 · "_ustr + path;
    }
    else
        out.message = u"Windows 截图失败"_ustr;
#else
    out = captureLinux(eMode, path);
#endif

    if (out.success && prefs.screenshotCopyClipboard)
    {
#if defined(MACOSX)
        // Also copy image to clipboard for paste-anywhere efficiency
        runShell(u"osascript -e 'set the clipboard to (read (POSIX file \""_ustr + path
                 + u"\") as «class PNGf»)' 2>/dev/null"_ustr);
#endif
    }
    return out;
}

ScreenCaptureResult DocumentAIScreenCapture::capturePassiveEvidence(const OUString& rTag)
{
    ScreenCaptureResult out;
    out.mode = u"evidence-fullscreen"_ustr;
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.applyCaptureEvidence)
    {
        out.message = u"写回截屏证据已在设置中关闭"_ustr;
        return out;
    }
    // Evidence path does not require interactive screenshotEnabled — separate toggle.
    if (!isAvailable())
    {
        out.message = u"截图工具不可用 · 跳过写回证据"_ustr;
        return out;
    }
    const OUString dir = DocumentAIInputPrefs::resolveCaptureDir(prefs) + u"/evidence"_ustr;
    if (!ensureDir(dir))
    {
        out.message = u"无法创建证据目录："_ustr + dir;
        return out;
    }
    const OUString path = dir + u"/"_ustr + makeTimestampName(rTag.isEmpty() ? u"apply"_ustr : rTag);

#if defined(MACOSX)
    // Non-interactive fullscreen — no user click required.
    OUStringBuffer cmd;
    cmd.append(u"screencapture -x \""_ustr);
    cmd.append(path);
    cmd.append(u"\""_ustr);
    const int rc = runShell(cmd.makeStringAndClear());
    if (rc == 0 && fileExistsSys(path))
    {
        out.success = true;
        out.path = path;
        out.fileUrl = toFileUrl(path);
        out.promptAttachment = u"@截图:"_ustr + path;
        out.message = u"写回证据截屏 · "_ustr + path;
    }
    else
        out.message = u"写回证据截屏失败（macOS）"_ustr;
#elif defined(_WIN32)
    OUString cmd = u"powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
                   u"Add-Type -AssemblyName System.Drawing; "
                   u"$b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds; "
                   u"$bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height; "
                   u"$g=[System.Drawing.Graphics]::FromImage($bmp); "
                   u"$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size); "
                   u"$bmp.Save('"_ustr
                   + path + u"',[System.Drawing.Imaging.ImageFormat]::Png)\""_ustr;
    const int rc = runShell(cmd);
    if (rc == 0 && fileExistsSys(path))
    {
        out.success = true;
        out.path = path;
        out.fileUrl = toFileUrl(path);
        out.promptAttachment = u"@截图:"_ustr + path;
        out.message = u"写回证据截屏 · "_ustr + path;
    }
    else
        out.message = u"写回证据截屏失败（Windows）"_ustr;
#else
    // Prefer non-interactive root/fullscreen tools.
    OUString cmd;
    if (commandExists("import"))
        cmd = u"import -window root \""_ustr + path + u"\""_ustr;
    else if (commandExists("gnome-screenshot"))
        cmd = u"gnome-screenshot -f \""_ustr + path + u"\""_ustr;
    else if (commandExists("scrot"))
        cmd = u"scrot \""_ustr + path + u"\""_ustr;
    else
    {
        out.message = u"无非交互截图工具 · 跳过写回证据"_ustr;
        return out;
    }
    const int rc = runShell(cmd);
    if (rc == 0 && fileExistsSys(path))
    {
        out.success = true;
        out.path = path;
        out.fileUrl = toFileUrl(path);
        out.promptAttachment = u"@截图:"_ustr + path;
        out.message = u"写回证据截屏 · "_ustr + path;
    }
    else
        out.message = u"写回证据截屏失败"_ustr;
#endif
    return out;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
