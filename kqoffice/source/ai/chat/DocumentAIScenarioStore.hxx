/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Configurable AI scenario packs with CRUD + checkbox options.
 * Persist: ~/.config/kqoffice/ai-scenarios.json
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAISCENARIOSTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAISCENARIOSTORE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// Runtime / persisted checkbox options for a scenario.
struct ScenarioOptions
{
    bool attachSelection = true; ///< Inject current selection into prompt
    bool includeDocContext = true; ///< Document AI Fabric context binding
    bool autoSubmit = true; ///< Side panel: run immediately on button
    bool useAgentPipeline = false; ///< Force plan→act→review
    bool requireApproval = true; ///< Stage apply; never auto-mutate main doc
    bool showAsButton = true; ///< Appear as executable button in AI panel
    bool pinned = false; ///< 常用/置顶：侧栏「常用」条 + 网格内靠前
};

/// One configurable scenario (default or user-defined).
/// Quality-core scenarios ship as Grok-style **skill packs**: rich promptTemplate
/// (steps / hard rules / output / trust chain) plus description + whenToUse for
/// natural-language matching (not only slash commands).
struct DocumentAIScenario
{
    OUString id; ///< unique stable id
    OUString titleZh;
    OUString category; ///< writer | calc | impress | general
    OUString preferredSurface; ///< writer | calc | impress | any
    OUString capabilityHint; ///< rewrite | chat | plan | review | agent | summarize
    OUString promptTemplate; ///< may contain {selection}; skill packs use full process body
    OUString slashCommand; ///< optional, e.g. /公文润色
    /// Short skill blurb: what it does (UI + matching context).
    OUString description;
    /// Trigger phrases for auto-match, separated by | (e.g. 公文润色|庄重|删繁就简).
    OUString whenToUse;
    ScenarioOptions options;
    sal_Int32 sortOrder = 100;
    /// Bumped when factory skill pack text changes; load() refreshes builtins below this.
    sal_Int32 skillVersion = 0;
    bool builtin = false; ///< factory default; cannot hard-delete (only disable)
    bool enabled = true; ///< master enable; false → hidden from buttons
};

/// Full catalog snapshot.
struct ScenarioCatalog
{
    OUString schemaVersion; ///< v1-scenarios
    std::vector<DocumentAIScenario> items;
};

class SAL_DLLPUBLIC_EXPORT DocumentAIScenarioStore
{
public:
    /// Path: KQOFFICE_AI_SCENARIOS or ~/.config/kqoffice/ai-scenarios.json
    static OUString defaultConfigPath();

    /// Built-in defaults (many packs across Writer/Calc/Impress).
    static std::vector<DocumentAIScenario> builtinDefaults();

    /// Load file or seed defaults; never throws.
    static ScenarioCatalog load();

    /// Ensure template file exists (merge missing builtins).
    static bool ensureDefaultTemplate();

    /// Persist full catalog.
    static bool save(const ScenarioCatalog& rCatalog);

    /// CRUD helpers (mutate catalog in place + optional save).
    static bool upsert(ScenarioCatalog& rCatalog, const DocumentAIScenario& rItem);
    static bool removeById(ScenarioCatalog& rCatalog, const OUString& rId);
    static DocumentAIScenario* findMutable(ScenarioCatalog& rCatalog, const OUString& rId);
    static const DocumentAIScenario* find(const ScenarioCatalog& rCatalog, const OUString& rId);

    /// Enabled + showAsButton, sorted by sortOrder.
    static std::vector<DocumentAIScenario> listExecutableButtons(const ScenarioCatalog& rCatalog);

    /// Same as listExecutableButtons, filtered by document surface
    /// (writer|calc|impress). Empty / "any" / "all" → no surface filter.
    /// Matches preferredSurface == filter OR "any" OR empty.
    static std::vector<DocumentAIScenario>
    listExecutableButtonsForSurface(const ScenarioCatalog& rCatalog,
                                    const OUString& rSurfaceFilter);

    /// Filter executable buttons by category tab:
    /// writer | calc | impress | general (general includes empty/any).
    /// Order: pinned first, then sortOrder ascending.
    static std::vector<DocumentAIScenario>
    listExecutableButtonsForCategory(const ScenarioCatalog& rCatalog,
                                     const OUString& rCategory);

    /// Enabled + showAsButton + pinned, global (all categories).
    /// Used by the side-panel 「常用」 strip. Order: sortOrder asc.
    static std::vector<DocumentAIScenario>
    listPinnedButtons(const ScenarioCatalog& rCatalog);

    /// Count executable buttons for a category (for tab badges).
    static sal_Int32 countExecutableButtonsForCategory(const ScenarioCatalog& rCatalog,
                                                        const OUString& rCategory);

    /// Toggle pin; returns false if id missing.
    static bool setPinned(ScenarioCatalog& rCatalog, const OUString& rId, bool bPinned);

    /// Reorder: move item up/down in sortOrder chain; renumbers 10,20,30…
    /// Returns false if id missing or already at edge.
    static bool moveUp(ScenarioCatalog& rCatalog, const OUString& rId);
    static bool moveDown(ScenarioCatalog& rCatalog, const OUString& rId);
    /// Compact sortOrder to 10, 20, 30… preserving relative order
    /// (pinned items keep their pin flag; order among all items preserved).
    static void renumberSortOrders(ScenarioCatalog& rCatalog);

    /// Apply absolute order from a drag-reorder / external list of ids.
    /// Unknown ids ignored; remaining items keep relative order after listed ones.
    /// Renumbers sortOrder to 10,20,30… Returns false if rOrderedIds empty.
    static bool reorderByIds(ScenarioCatalog& rCatalog, const std::vector<OUString>& rOrderedIds);

    /// Sort key: pinned first, then lower sortOrder.
    static bool lessByPinThenOrder(const DocumentAIScenario& a, const DocumentAIScenario& b);

    /// Expand prompt with selection / options.
    static OUString expandPrompt(const DocumentAIScenario& rScenario, const OUString& rSelectionText);

    /// Expand skill pack and append the user's free-form utterance (NL path).
    static OUString expandSkillWithUtterance(const DocumentAIScenario& rScenario,
                                             const OUString& rSelectionText,
                                             const OUString& rUserUtterance);

    /// Match slash command against catalog (enabled only).
    static const DocumentAIScenario* matchSlash(const ScenarioCatalog& rCatalog,
                                                const OUString& rUserInput);

    /// Match free-form Chinese/English against skill whenToUse / title / slash stem.
    /// rSurfaceFilter: writer|calc|impress|any — prefers same surface, allows general.
    /// Returns best enabled skill with score >= threshold, or nullptr.
    /// Optional pScoreOut receives match score (higher = stronger).
    static const DocumentAIScenario* matchNaturalLanguage(const ScenarioCatalog& rCatalog,
                                                          const OUString& rUserInput,
                                                          const OUString& rSurfaceFilter,
                                                          sal_Int32* pScoreOut = nullptr);

    /// Current factory skill-pack revision (builtins with lower skillVersion get refreshed).
    static sal_Int32 skillPackVersion();

    /// Serialize / parse (for tests & options export).
    static OUString serializeJson(const ScenarioCatalog& rCatalog);
    static ScenarioCatalog parseJson(const OUString& rJson);

    /// Queue a scenario to auto-run when the AI panel next opens/refreshes.
    /// Sources: ~/.config/kqoffice/pending-scenario-run, env KQOFFICE_AI_RUN_SCENARIO,
    /// or kqoffice_ai_queue_scenario_run() C ABI. takePendingRun clears the queue.
    static bool queuePendingRun(const OUString& rScenarioId);
    static OUString takePendingRun();
    static OUString pendingRunPath();
};

/// C ABI: queue scenario id (UTF-8) for next AI panel open. Safe from scripts.
extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_queue_scenario_run(const char* pUtf8Id);

// Backward-compatible facade used by older call sites.
class SAL_DLLPUBLIC_EXPORT DocumentAIScenarios
{
public:
    static std::vector<DocumentAIScenario> all();
    static DocumentAIScenario findBySlashOrId(const OUString& rToken);
    static OUString expandPrompt(const DocumentAIScenario& rScenario,
                                 const OUString& rSelectionText);
    static bool isScenarioSlash(const OUString& rUserInput);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
