/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 资料盘整理（内部 compile）：raw → wiki sources/concepts/index/log。
 * 默认不自动跑；用户触发。主文档永不修改。
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTCOMPILE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTCOMPILE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::vault
{
struct VaultCompileResult
{
    bool ok = false;
    sal_Int32 processed = 0;
    sal_Int32 sourcesWritten = 0;
    sal_Int32 conceptsTouched = 0;
    OUString messageZh;
};

class SAL_DLLPUBLIC_EXPORT VaultCompile
{
public:
    /// Process up to nMax uncompiled raw import snippets (uses membership LLM if available).
    /// On LLM failure, still writes a local extract-based source stub.
    static VaultCompileResult compilePending(const OUString& rVaultRoot = OUString(),
                                             sal_Int32 nMax = 8);

    /// True when auto-compile preference is on (state.json); default false.
    static bool isAutoCompileEnabled(const OUString& rVaultRoot = OUString());
    static bool setAutoCompileEnabled(bool bOn, const OUString& rVaultRoot = OUString());
};

} // namespace kqoffice::ai::vault

#endif
