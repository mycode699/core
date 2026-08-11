/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Task Bootstrap Restatement + Capability Schedule (shengji / Grok-style).
 *
 * 语义启动：用户消息 + 上下文 → 短任务复述（用户可见）→ 能力调度合同
 * 再工具 / Agent / MCP。规则只做路由；复述尽量规范化（中文 UI）。
 *
 * 不写主文档。不上传。
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAITASKBOOTSTRAP_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAITASKBOOTSTRAP_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

/// How much bootstrap work this turn needs.
enum class TaskBootstrapMode
{
    Skip, ///< pure QA / empty / slash full contract — no forced restatement card
    Thin, ///< continue prior task — short restatement anchored on last goal
    Required ///< development / edit / agent turn — full restatement before tools
};

/// User-visible short restatement (1–2 sentences shape).
struct TaskRestatement
{
    OUString objective; ///< 核心交付
    OUString scopeIn; ///< 范围 in
    OUString scopeOut; ///< 明确不做什么
    OUString nextStep; ///< 立刻第一步
    /// Compact one-liner for transcript / status.
    OUString userVisibleZh;
    /// Internal compact token for logs: task_restatement:…
    OUString journalToken;
    /// 0–100 heuristic confidence of rule restatement (low → may call light model).
    sal_Int32 confidence = 100;
    /// "rule" | "model" | "thin" | "skip"
    OUString source;
};

/// Capability schedule driven by restatement (not keyword soup alone).
struct CapabilitySchedule
{
    OUString primaryCapability; ///< chat|rewrite|plan|agent|review|…
    bool preferAgentPipeline = false;
    bool useDocumentTools = true;
    bool useFormulaSandbox = false;
    bool useLocalRag = false;
    bool useMcpTools = false; ///< advertise MCP-shaped tools in prompt (read-only)
    bool stageApplyOnSuccess = true;
    /// Ordered lane labels for UI (max ~6).
    std::vector<OUString> lanes;
    /// MCP tool names the task may need (read-only / preview first).
    std::vector<OUString> mcpTools;
    OUString summaryZh;
    /// True when schedule was rebuilt after model restatement.
    bool secondaryRefined = false;
};

struct TaskBootstrapInput
{
    OUString userPrompt;
    OUString surface; ///< writer|calc|impress|none
    bool hasSelection = false;
    sal_Int32 selectionChars = 0;
    OUString lastPrompt; ///< previous user turn (for 继续)
    OUString lastRestatement; ///< previous restatement objective
    OUString forcedCapability; ///< chip / scenario override
    bool agentCheckbox = false;
};

struct TaskBootstrapResult
{
    TaskBootstrapMode mode = TaskBootstrapMode::Skip;
    TaskRestatement restatement;
    CapabilitySchedule schedule;
    /// Normalized user prompt for Provider (may equal original).
    OUString normalizedPrompt;
    bool skipRestatementCard = false;
    /// True when rule confidence is low and light-slot refine is recommended.
    bool wantsModelRefine = false;
    /// Prompt for light slot (summarize) — only when wantsModelRefine.
    OUString modelRefinePrompt;
};

/// Pure-logic bootstrap + schedule (no document mutation).
/// Optional model refine is invoked by the panel via refineWithModelOutput().
class SAL_DLLPUBLIC_EXPORT DocumentAITaskBootstrap
{
public:
    /// Route + restate + schedule in one call (rule path; may set wantsModelRefine).
    static TaskBootstrapResult bootstrap(const TaskBootstrapInput& rIn);

    /// Route only (skip / thin / required).
    static TaskBootstrapMode routeMode(const TaskBootstrapInput& rIn);

    /// Build restatement for a required/thin turn.
    static TaskRestatement buildRestatement(const TaskBootstrapInput& rIn,
                                            TaskBootstrapMode eMode);

    /// Build capability schedule from restatement + input.
    static CapabilitySchedule buildSchedule(const TaskBootstrapInput& rIn,
                                            const TaskRestatement& rRest,
                                            TaskBootstrapMode eMode);

    /// Confidence 0–100 for current rule restatement / prompt clarity.
    static sal_Int32 confidenceScore(const TaskBootstrapInput& rIn, TaskBootstrapMode eMode);

    /// True when Required mode + low confidence → call light slot.
    static bool shouldRefineWithModel(const TaskBootstrapResult& rBoot,
                                      sal_Int32 nConfidenceThreshold = 55);

    /// Prompt for light model: force 3-line 理解/范围/下一步 Chinese shape.
    static OUString buildModelRefinePrompt(const TaskBootstrapInput& rIn);

    /// Merge light-model body into bootstrap result; rebuild schedule; source=model.
    static TaskBootstrapResult refineWithModelOutput(const TaskBootstrapResult& rBoot,
                                                     const TaskBootstrapInput& rIn,
                                                     const OUString& rModelBody);

    /// Parse model free text into restatement fields (best-effort).
    static TaskRestatement parseModelRestatement(const OUString& rModelBody,
                                                 const TaskRestatement& rFallback);

    /// True if prompt looks like pure consult Q&A (no write-back).
    static bool looksLikePureQa(const OUString& rPrompt);

    /// True if prompt is continue / 继续 / jixu.
    static bool looksLikeContinue(const OUString& rPrompt);

    /// True if multi-step / agent / plan development turn.
    static bool looksLikeDevOrAgent(const OUString& rPrompt, bool bAgentCheckbox,
                                    const OUString& rForcedCap);

    /// Rebuild userVisibleZh + journalToken from fields.
    static void finalizeRestatementStrings(TaskRestatement& rRest);

    /// Prompt prefix: task contract for Provider / Agent (keeps model on restatement).
    static OUString buildContractBlock(const TaskRestatement& rRest,
                                       const CapabilitySchedule& rSched);

    /// Prepend contract block to user/work prompt (no mutation of journal).
    static OUString applyContractToPrompt(const OUString& rWorkPrompt,
                                          const TaskBootstrapResult& rBoot);

    /// Secondary schedule pass after restatement is final (rule or model).
    static CapabilitySchedule refineScheduleSecondary(const TaskBootstrapInput& rIn,
                                                      const TaskRestatement& rRest,
                                                      const CapabilitySchedule& rPrimary);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
