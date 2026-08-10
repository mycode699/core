/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTLINT_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTLINT_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::vault
{
struct VaultLintResult
{
    bool ok = false;
    sal_Int32 emptyPages = 0;
    sal_Int32 orphanConcepts = 0;
    sal_Int32 sourcesWithoutCite = 0;
    OUString reportPath; ///< outputs/health/...
    OUString messageZh;
};

class SAL_DLLPUBLIC_EXPORT VaultLint
{
public:
    static VaultLintResult run(const OUString& rVaultRoot = OUString());
};

} // namespace kqoffice::ai::vault

#endif
