/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 资料盘管理（迅雷/夸克/WPS 式理念）：
 *   - 安装/首次启动：默认路径 + 建盘 + 授权
 *   - 多盘创建 / 切换 / 改路径 / 打开目录
 *   - 文件夹授权走 PermissionCenter（非静默全盘）
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTMANAGER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTMANAGER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::vault
{
struct VaultRecord
{
    OUString id; ///< short id e.g. default, v-…
    OUString name; ///< display name 默认资料盘
    OUString rootPath; ///< absolute system path
    OUString kind; ///< install-default | user-created | user-relocated
    OUString createdAt; ///< unix seconds string
    bool permissionGranted = false; ///< last known PermissionCenter grant
};

struct VaultManagerResult
{
    bool ok = false;
    OUString messageZh;
    OUString vaultId;
    OUString rootPath;
};

/// Multi-vault registry + permission + install defaults (download-path style management).
class SAL_DLLPUBLIC_EXPORT VaultManager
{
public:
    /// ~/.config/kqoffice/vaults/registry.json
    static OUString registryPath();
    static OUString registryDir();

    /// Suggested install default root (like 下载路径):
    ///   macOS/Linux: ~/Documents/可圈资料盘
    ///   Windows:     %USERPROFILE%\\Documents\\可圈资料盘 (or OneDrive\\Documents)
    ///   fallback:    <config>/vault  (%APPDATA%\\kqoffice\\vault on Win)
    /// Override: KQOFFICE_VAULT_INSTALL_DEFAULT
    static OUString installDefaultRootSuggestion();

    /// First-run / install seed:
    /// 1) ensure registry
    /// 2) create default vault layout at preferred root (Documents if writable, else config)
    /// 3) grant PermissionCenter FolderScan root for that path
    /// Idempotent. Safe to call every launch.
    static VaultManagerResult ensureInstallDefaults();

    static std::vector<VaultRecord> listVaults();
    static VaultRecord activeVault();
    static OUString activeRoot();

    /// Create a new vault at rRootPath (or suggestion). Grants folder permission.
    static VaultManagerResult createVault(const OUString& rName, const OUString& rRootPath);

    /// Switch active vault by id.
    static VaultManagerResult switchVault(const OUString& rId);

    /// Relocate active (or id) vault root — like changing download directory.
    /// Does not move files automatically; creates layout at new path and updates registry.
    static VaultManagerResult setVaultLocation(const OUString& rId, const OUString& rNewRoot);

    /// Re-grant PermissionCenter access for vault root (user picked path).
    static VaultManagerResult authorizeVault(const OUString& rId);

    static VaultManagerResult revokeVaultAuth(const OUString& rId);

    /// True if vault root is under an authorized PermissionCenter directory.
    static bool isVaultAuthorized(const OUString& rId);

    /// Rename display name only.
    static VaultManagerResult renameVault(const OUString& rId, const OUString& rNewName);

    /// Open root in Finder/Explorer (best-effort shell).
    static VaultManagerResult revealInFileManager(const OUString& rId = OUString());

    /// Multi-line management surface (settings / slash), Chinese, WPS/Quark style.
    static OUString managementSummaryZh();

    /// One-line chip for sidebar.
    static OUString statusChipZh();

    /// Mark that product first-run vault step was shown/acked.
    static bool setOnboardingAcked(bool bAcked);
    static bool isOnboardingAcked();
};

} // namespace kqoffice::ai::vault

#endif
