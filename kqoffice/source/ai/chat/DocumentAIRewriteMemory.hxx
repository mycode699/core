/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Per-document rewrite memory + compaction (Grok memory/compaction analogue).
 *
 * Local-only sidecar keyed by document hash (same identity family as chat history).
 * Captures user constraints/corrections and a short summary so long multi-turn
 * edit sessions do not drop "别动数据区" style rules. Never uploads.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIREWRITEMEMORY_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIREWRITEMEMORY_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// Structured memory card bound to one document identity hash.
struct RewriteMemoryCard
{
    OUString documentKey;
    OUString summaryZh; ///< compacted narrative for prompt injection
    std::vector<OUString> constraints; ///< hard user rules (max ~12)
    std::vector<OUString> corrections; ///< 用户纠偏 / direction changes
    OUString lastSkillId;
    OUString lastSkillTitle;
    OUString lastObjective;
    OUString brandTone; ///< optional tone note
    sal_Int32 turnCount = 0; ///< user turns since clear
    sal_Int32 compactGeneration = 0;
    bool dirty = false;
};

enum class RewriteMemoryAction
{
    None,
    Show, ///< /memory /改稿记忆
    Remember, ///< /记住 …
    Forget, ///< /忘记 … | /忘记全部
    Compact ///< /compact /压缩记忆
};

class SAL_DLLPUBLIC_EXPORT DocumentAIRewriteMemory
{
public:
    /// ~/.config/kqoffice/ai-rewrite-memory/ or KQOFFICE_AI_REWRITE_MEMORY dir.
    static OUString storageRootPath();
    static OUString cardPathForKey(const OUString& rDocumentKey);

    static RewriteMemoryCard load(const OUString& rDocumentKey);
    static bool save(const RewriteMemoryCard& rCard);
    static bool clear(const OUString& rDocumentKey);

    /// Pull constraints / tone / remember-facts from a user utterance.
    static void ingestUserTurn(RewriteMemoryCard& rCard, const OUString& rUserPrompt);

    /// Explicit constraint (e.g. /记住 …); de-duped, max kMaxConstraints.
    static void addConstraint(RewriteMemoryCard& rCard, const OUString& rConstraint);

    /// Record last skill / objective (from skill match or work plan).
    static void noteSkill(RewriteMemoryCard& rCard, const OUString& rSkillId,
                          const OUString& rSkillTitle);
    static void noteObjective(RewriteMemoryCard& rCard, const OUString& rObjective);

    /// Merge work-plan scopeOut / revise notes into constraints.
    static void ingestWorkPlanNotes(RewriteMemoryCard& rCard, const OUString& rScopeOut,
                                    const OUString& rReviseNotes, const OUString& rObjective);

    /// After assistant reply: bump compact if due (rule summary, no network).
    static void afterAssistantTurn(RewriteMemoryCard& rCard, const OUString& rAssistantSnippet,
                                   const OUString& rRecentTurnsText);

    /// Force rebuild summaryZh from constraints + recent turns.
    static void compact(RewriteMemoryCard& rCard, const OUString& rRecentTurnsText);

    /// Block injected into provider prompt (empty if nothing useful).
    static OUString formatPromptBlock(const RewriteMemoryCard& rCard);

    /// User-visible markdown for /memory.
    static OUString formatUserVisible(const RewriteMemoryCard& rCard);

    static RewriteMemoryAction classifyAction(const OUString& rPrompt);
    static OUString extractRememberFact(const OUString& rPrompt);
    static OUString extractForgetTarget(const OUString& rPrompt);

    /// Auto-compact when turnCount crosses multiples of this (default 4).
    static sal_Int32 compactEveryNTurns() { return 4; }

    static constexpr sal_Int32 kMaxConstraints = 12;
    static constexpr sal_Int32 kMaxCorrections = 8;
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
