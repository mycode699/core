/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 资料盘投喂：raw 只增；Office 默认 link。
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTINGEST_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTINGEST_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::vault
{
struct VaultIngestResult
{
    bool ok = false;
    OUString id; ///< ving-… or empty
    OUString title;
    OUString kind; ///< file-copy|file-link|folder-link|paste-text|notebook-material
    OUString rawRelativePath;
    OUString sourcePath;
    OUString snippetPath; ///< under raw/ for FTS body (may equal copy path)
    OUString contentHash; ///< sha256:hex
    OUString messageZh;
    sal_Int32 indexedChars = 0;
};

class SAL_DLLPUBLIC_EXPORT VaultIngest
{
public:
    /// Prefer link for office/binary; copy text-ish into raw/imports.
    static VaultIngestResult ingestPath(const OUString& rSystemPath,
                                        const OUString& rVaultRoot = OUString());

    static VaultIngestResult ingestText(const OUString& rTitle, const OUString& rBody,
                                        const OUString& rVaultRoot = OUString());

    /// Bridge notebook materials index into vault raw/notebook pointers.
    static sal_Int32 ingestNotebookMaterials(const OUString& rVaultRoot = OUString(),
                                             sal_Int32 nMax = 200);

    /// List raw ingest records (from .kq/ingest-log.jsonl last N lines).
    static std::vector<OUString> listRecentTitles(const OUString& rVaultRoot = OUString(),
                                                  sal_Int32 nMax = 40);
};

} // namespace kqoffice::ai::vault

#endif
