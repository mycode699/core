/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTPACK_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTPACK_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::vault
{
struct VaultPackResult
{
    bool ok = false;
    OUString packDir; ///< outputs/packs/<id>-title/
    OUString messageZh;
};

class SAL_DLLPUBLIC_EXPORT VaultPack
{
public:
    /// Export a 资料包 directory (md + manifest). No vector index, no secrets.
    static VaultPackResult exportThemePack(const OUString& rTitle,
                                           const OUString& rVaultRoot = OUString());
};

} // namespace kqoffice::ai::vault

#endif
