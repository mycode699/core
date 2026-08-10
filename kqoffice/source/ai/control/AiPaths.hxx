/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Cross-platform paths for 可圈 AI / 资料盘 (macOS + Windows + Linux).
 * Prefer these over raw getenv("HOME") so Win installers get %APPDATA% / %USERPROFILE%.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_AIPATHS_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_AIPATHS_HXX

#include <rtl/ustring.hxx>

namespace kqoffice::ai
{
/// User home: $HOME, else %USERPROFILE% / HOMEDRIVE+HOMEPATH.
SAL_DLLPUBLIC_EXPORT OUString kqofficeUserHomeDir();

/// Config root for kqoffice:
///   Unix:  ~/.config/kqoffice
///   Win:   %APPDATA%\\kqoffice  (fallback %USERPROFILE%\\.config\\kqoffice)
SAL_DLLPUBLIC_EXPORT OUString kqofficeAiConfigDir();

/// Documents folder for install-default 资料盘:
///   Unix:  ~/Documents
///   Win:   %USERPROFILE%\\Documents  (or OneDrive Documents if present)
SAL_DLLPUBLIC_EXPORT OUString kqofficeUserDocumentsDir();

/// Process temp dir: TMPDIR / TMP / TEMP / platform default.
SAL_DLLPUBLIC_EXPORT OUString kqofficeTempDir();

/// Join path segments with '/' (osl accepts on Win; registry stays portable).
SAL_DLLPUBLIC_EXPORT OUString kqofficePathJoin(const OUString& rA, const OUString& rB);

/// Parent directory of a system path (handles / and \\).
SAL_DLLPUBLIC_EXPORT OUString kqofficeParentDir(const OUString& rPath);

/// Last path component.
SAL_DLLPUBLIC_EXPORT OUString kqofficeFileName(const OUString& rPath);

} // namespace kqoffice::ai

#endif
