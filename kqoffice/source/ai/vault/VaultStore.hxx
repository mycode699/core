/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 可圈资料盘 (user-facing). Internal LLM-Wiki layers: raw / wiki / outputs.
 * Phase 0/1: layout ensure + state; no compile worker here.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTSTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_VAULT_VAULTSTORE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::vault
{
/// User-visible product name (never "RAG" / "vector store").
inline constexpr OUStringLiteral kVaultProductName = u"资料盘";

struct VaultPaths
{
    OUString root;
    OUString raw;
    OUString wiki;
    OUString outputs;
    OUString internalState; ///< .kq/
    OUString vaultMd;
    OUString stateJson;
};

class SAL_DLLPUBLIC_EXPORT VaultStore
{
public:
    /// Default: ~/.config/kqoffice/vault (override later via prefs / env).
    static OUString defaultRootDir();

    /// Resolve root: env KQOFFICE_VAULT_ROOT, else defaultRootDir().
    static OUString rootDir();

    static VaultPaths pathsFor(const OUString& rRoot);

    /// Create raw/wiki/outputs/.kq + stub wiki files + VAULT.md if missing.
    /// Returns false only on I/O failure (never throws).
    static bool ensureLayout(const OUString& rRoot = OUString());

    /// True if raw/ and wiki/ exist under root.
    static bool isInitialized(const OUString& rRoot = OUString());

    /// Phase 0 policy defaults (mirrors vault-layout schema).
    static bool autoCompileDefault();
    static bool ftsIndexOnIngestDefault();
    /// "link" | "copy"
    static OUString officeLargeFileDefault();
};

} // namespace kqoffice::ai::vault

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
