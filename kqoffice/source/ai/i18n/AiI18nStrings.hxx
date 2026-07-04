/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 AI i18n: string provider).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Header-only i18n string provider for the V2 AI module.
 * Locale detection: checks KQOFFICE_AI_LOCALE env var; if "en-US" returns
 * English, otherwise returns zh-CN (default). Falls back to the raw key ID
 * when the string is not found in the lookup table.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_I18N_AII18NSTRINGS_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_I18N_AII18NSTRINGS_HXX

#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <cstdlib>
#include <cstring>

namespace kqoffice::ai::i18n
{

namespace detail
{
/// Number of entries in the string table.
constexpr sal_Int32 kEntryCount = 53;

/// One locale-variant entry keyed by stable ASCII id.
struct AiStringEntry
{
    const char* id;   ///< stable ASCII key, e.g. "provider.error.connection"
    const char* zhCN; ///< zh-CN / default Chinese
    const char* enUS; ///< en-US translation
};

/// Lookup table — ordered by id for readability; linear scan is fine for
/// the expected call volume (tens of calls per user gesture, not thousands).
inline const AiStringEntry* entryTable()
{
    static const AiStringEntry s_aTable[kEntryCount] = {
        // ── Provider module ─────────────────────────────────────────
        { "provider.error.connection",       "连接AI服务失败",            "AI service connection failed" },
        { "provider.error.timeout",          "AI服务请求超时",            "AI service request timed out" },
        { "provider.error.model_not_found",  "未找到AI模型",              "AI model not found" },
        { "provider.error.no_connection",    "未连接AI服务",              "Not connected to AI service" },
        { "provider.status.ready",           "AI服务就绪",                "AI service ready" },
        { "provider.status.connecting",      "正在连接AI服务...",         "Connecting to AI service..." },
        { "provider.service_mode.local",     "本地",                     "Local" },
        { "provider.service_mode.cloud",     "云端",                     "Cloud" },

        // ── ApplyPlan validator module ──────────────────────────────
        { "applyplan.error.not_json",        "响应格式错误",              "Invalid response format" },
        { "applyplan.error.missing_field",   "缺少必要字段",              "Missing required field" },
        { "applyplan.error.schema_mismatch", "数据格式不匹配",            "Data format mismatch" },
        { "applyplan.error.id_pattern",      "ID格式不正确",              "Invalid ID format" },
        { "applyplan.error.revision_bad",    "文档版本不匹配",            "Document version mismatch" },
        { "applyplan.error.undo_label_empty","缺少撤销标签",              "Missing undo label" },
        { "applyplan.error.failure_empty",   "缺少失败提示",              "Missing failure message" },
        { "applyplan.error.summary_empty",   "缺少操作说明",              "Missing operation summary" },

        // ── ApplyPlan validator — extra granular codes ─────────────
        { "applyplan.error.deterministic",       "未声明确定性执行，已取消本次应用",     "Deterministic execution not declared, application cancelled" },
        { "applyplan.error.rollback_required",   "未声明回滚支持，已取消本次应用",       "Rollback support not declared, application cancelled" },
        { "applyplan.error.undo_group_mode",     "撤销分组模式不合法，已取消本次应用",   "Invalid undo group mode, application cancelled" },
        { "applyplan.error.failure_behavior_bad","失败处理策略不合法，已取消本次应用",   "Invalid failure behavior policy, application cancelled" },
        { "applyplan.error.repeated_diagnostics","未声明重新诊断要求，已取消本次应用",   "Repeated diagnostics requirement not declared, application cancelled" },
        { "applyplan.error.not_json_long",       "AI 回包不是合法的 JSON 对象，已取消本次应用。",    "AI response is not a valid JSON object, application cancelled." },
        { "applyplan.error.missing_field_long",  "AI 回包缺少必填字段，已取消本次应用。",           "AI response missing required field, application cancelled." },
        { "applyplan.error.schema_mismatch_long","AI 回包版本与当前可圈office 不兼容，已取消本次应用。", "AI response version incompatible with current KQOffice, application cancelled." },
        { "applyplan.error.id_pattern_long",     "AI 回包的标识字段格式不合法，已取消本次应用。",     "AI response identifier field format invalid, application cancelled." },
        { "applyplan.error.revision_bad_long",   "文档版本前置条件��匹配，已取消本次应用。",         "Document revision precondition mismatch, application cancelled." },

        // ── Cowork module ──
        { "cowork.title",                     "异步任务",                  "Async Tasks" },
        { "cowork.btn.new_task",              "新建任务",                  "New Task" },
        { "cowork.btn.accept_task",           "接受任务",                  "Accept Task" },
        { "cowork.status.this_month",         "任务列表（本月）",          "Tasks (This Month)" },
        { "cowork.status.pending",            "等待中",                    "Pending" },
        { "cowork.status.running",            "执行中",                    "Running" },
        { "cowork.status.awaiting_review",    "待审核",                    "Awaiting Review" },
        { "cowork.status.applied",            "已应用",                    "Applied" },
        { "cowork.status.failed",             "失败",                      "Failed" },
        { "cowork.status.cancelled",          "已取消",                    "Cancelled" },
        { "cowork.error.enqueue_failed",      "任务创建失败",              "Task creation failed" },
        { "cowork.error.cancel_failed",       "任务取消失败",              "Task cancellation failed" },
        { "cowork.error.no_provider",         "AI服务未连接，无法创建任务","AI service not connected, cannot create task" },
        { "cowork.notify.task_complete",      "任务完成：%1",              "Task complete: %1" },
        { "cowork.notify.task_failed",        "任务失败：%1",              "Task failed: %1" },
        { "cowork.notify.awaiting_review",    "任务 %1 等待审核",          "Task %1 awaiting review" },
        { "cowork.notify.ready_for_review",   "任务已完成，等待审批",      "Task completed, awaiting review" },
        { "cowork.notify.review_ready_body",  "%1 已准备好打开 review",    "%1 ready for review" },
        { "cowork.task.stub_title",           "新任务（等待审批）",        "New Task (Pending)" },
        { "cowork.task.stub_prompt",          "从异步任务面板启动",        "Launched from async task panel" },

        // ── Formatting helper ──
        { "i18n.field_suffix",                "（字段：%1）",              " (field: %1)" },

        // ── About dialog (cui) ──
        { "about.calc_mode.multithreaded",    "多线程",                    "Multi-threaded" },
        { "about.calc_mode.jumbo",            "大表格",                    "Jumbo Sheets" },
        { "about.calc_mode.default",          "默认",                      "Default" },
        { "about.calc_engine_label",          "表格引擎：",                "Calc Engine: " },
        { "about.ai_section_header",          "\n\nAI 功能：",             "\n\nAI Features: " },
        { "about.ai_features",                "Provider 运行时 / Select-to-Act / Cowork 异步任务", "Provider Runtime / Select-to-Act / Cowork Async Tasks" },
    };
    return s_aTable;
}

/// Returns true when the en-US locale is explicitly requested.
inline bool isEnUS()
{
    const char* loc = std::getenv("KQOFFICE_AI_LOCALE");
    return loc && std::strcmp(loc, "en-US") == 0;
}
} // namespace detail

/// Return the locale-appropriate string for a stable ASCII id.
///
/// Locale detection (first-match wins):
///   1. KQOFFICE_AI_LOCALE == "en-US"  →  English
///   2. Everything else                →  Chinese (zh-CN, default)
///
/// Fallback chain when id is not found in the table:
///   1. Locale-specific entry
///   2. zh-CN entry (when en-US requested but not found)
///   3. The raw id itself (so callers never see an empty string)
inline OUString get(const OUString& id)
{
    const bool bEn = detail::isEnUS();
    const detail::AiStringEntry* pTable = detail::entryTable();

    // First pass: look for a match, returning locale-specific string.
    for (sal_Int32 i = 0; i < detail::kEntryCount; ++i)
    {
        if (id.equalsAscii(pTable[i].id))
        {
            return bEn ? OUString::createFromAscii(pTable[i].enUS)
                       : OUString::createFromAscii(pTable[i].zhCN);
        }
    }

    // Not found: return raw key as last-resort fallback.
    return id;
}

/// Convenience: format a string by replacing the first "%1" with `arg`.
inline OUString format(const OUString& id, const OUString& arg)
{
    OUString tmpl = get(id);
    sal_Int32 p = tmpl.indexOf(u"%1"_ustr);
    if (p < 0)
        return tmpl;
    OUStringBuffer buf(tmpl);
    buf.remove(p, 2);
    buf.insert(p, arg);
    return buf.makeStringAndClear();
}

} // namespace kqoffice::ai::i18n

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
