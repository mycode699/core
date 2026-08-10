/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 资料盘 ↔ FTS5 bridge (sfx2). User-facing name: 资料盘.
 */

#pragma once

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace sfx2::sidebar
{
struct AIChatVaultHit
{
    OUString Title;
    OUString Snippet;
    OUString Path;
    sal_Int32 Rank = 0;
};

struct AIChatVaultSearchResult
{
    bool Success = false;
    OUString WorkspaceId;
    std::vector<AIChatVaultHit> Hits;
    OUString MessageZh;
    OUString PromptBlock;
};

struct AIChatVaultIndexResult
{
    bool Success = false;
    sal_Int32 Indexed = 0;
    OUString MessageZh;
};

/// Stable FTS workspace identity for the 资料盘.
OUString AIChatVaultWorkspaceId();

/// Ensure vault layout, then index all raw/imports snippets into FTS.
AIChatVaultIndexResult AIChatVaultReindexAll();

/// Index a single local text file into the vault FTS workspace.
AIChatVaultIndexResult AIChatVaultIndexPath(const OUString& rSystemPath, const OUString& rBody);

/// Search vault FTS.
AIChatVaultSearchResult AIChatVaultSearch(const OUString& rQuery, sal_Int32 nTopK = 6);

/// Related materials for current document title/skeleton keywords.
AIChatVaultSearchResult AIChatVaultRelated(const OUString& rSeedQuery, sal_Int32 nTopK = 3);

/// One-line status for UI.
OUString AIChatVaultStatusLineZh();

} // namespace sfx2::sidebar
