/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AgentChatContextBuilder.
 */

#include <AgentChatContextBuilder.hxx>
#include <AgentChatMentionResolver.hxx>
#include <AgentChatSelectionCapture.hxx>

#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <algorithm>

using namespace kqoffice::ai::chat;

namespace
{
/// Product trust line shared by all surfaces (local-first · approve-before-write).
OUString trustLine()
{
    return u"产品信任：本地优先；「批准写回」指用户确认后才把 AI 草案写入主文档，"_ustr
           u"一次批准不是永久静默改稿授权。禁止声称已自动修改用户主文档。"_ustr
           u"若用户问「批准写回」，按本产品写回链路解释，不要按会计坏账术语解释。"_ustr;
}

/// Build the system prompt based on document type (可圈office Chinese-first).
OUString buildSystemPromptImpl(const OUString& docType,
                               const std::vector<MentionContext>& /*mentions*/)
{
    const OUString trust = trustLine();
    if (docType.equalsIgnoreAsciiCase("writer"))
    {
        return u"你是可圈办公文字处理助手。根据用户选区与文档上下文，提供改写、"_ustr
               u"润色、扩写、简写、翻译与结构建议。若建议修改正文，优先输出可解析的"_ustr
               u"ApplyPlan/段落替换 JSON；否则给出清晰可执行的文案。"_ustr
               + trust;
    }
    if (docType.equalsIgnoreAsciiCase("calc"))
    {
        return u"你是可圈办公表格助手。根据选中单元格/区域，解释数据、给出公式、"_ustr
               u"清洗与汇总建议。引用单元格请用标准地址（如 B2、A1:C10）。"_ustr
               + trust;
    }
    if (docType.equalsIgnoreAsciiCase("impress"))
    {
        return u"你是可圈办公演示助手。根据当前幻灯/对象文案，优化标题、要点与讲稿，"_ustr
               u"给出版式与结构建议。"_ustr
               + trust;
    }

    return u"你是可圈办公助手。结合当前文档类型与选区帮助用户完成编辑任务。"_ustr + trust;
}

/// Build the document context section for the prompt string.
OUString buildDocumentSectionImpl(const ChatContext& ctx)
{
    OUStringBuffer buf;

    buf.append(u"--- Document Context ---\n");
    buf.append(u"Title: ");
    buf.append(ctx.documentTitle.isEmpty() ? u"(untitled)"_ustr : ctx.documentTitle);
    buf.append(u"\nType: ");
    buf.append(ctx.documentType.isEmpty() ? u"unknown"_ustr : ctx.documentType);
    if (!ctx.selectionPosition.isEmpty())
    {
        buf.append(u"\nPosition: ");
        buf.append(ctx.selectionPosition);
    }

    if (ctx.hasSelection())
    {
        buf.append(u"\n--- Selection ---\n");
        buf.append(ctx.selectionText);
    }

    buf.append(u"\n");

    return buf.makeStringAndClear();
}
} // anonymous namespace

ChatContext AgentChatContextBuilder::build(const OUString& userInput,
                                          const std::vector<MentionContext>& mentions,
                                          const SelectionContext& selection)
{
    ChatContext ctx;

    ctx.systemPrompt = AgentChatContextBuilder::buildSystemPrompt(selection.surface, mentions);
    ctx.documentTitle = u"Current Document"_ustr;
    ctx.documentType = selection.surface;
    ctx.selectionText = selection.text;
    ctx.selectionPosition = selection.position;
    ctx.userQuery = AgentChatContextBuilder::extractUserQuery(userInput, mentions);
    if (ctx.userQuery.isEmpty())
        ctx.userQuery = userInput.trim();
    ctx.recentMessages.clear();

    SAL_INFO("kqoffice.ai.chat",
             "Built ChatContext: docType=" << selection.surface
                 << ", hasSelection=" << (selection.text.isEmpty() ? 0 : 1)
                 << ", input=[" << userInput << "]");

    return ctx;
}

OUString AgentChatContextBuilder::toPromptString(const ChatContext& ctx)
{
    OUStringBuffer buf;

    buf.append(u"=== System Instruction ===\n");
    buf.append(ctx.systemPrompt);
    buf.append(u"\n\n");

    buf.append(buildDocumentSectionImpl(ctx));

    if (!ctx.recentMessages.empty())
    {
        buf.append(u"--- Conversation History ---\n");
        const sal_Int32 start = std::max(static_cast<sal_Int32>(0),
                                         static_cast<sal_Int32>(ctx.recentMessages.size()) - 20);
        for (sal_Int32 i = start; i < static_cast<sal_Int32>(ctx.recentMessages.size()); ++i)
        {
            buf.append(ctx.recentMessages[i]);
            buf.append(u"\n");
        }
    }

    buf.append(u"--- User Request ---\n");
    buf.append(ctx.userQuery);
    buf.append(u"\n");

    SAL_INFO("kqoffice.ai.chat",
             "Generated prompt string, length=" << buf.getLength());

    return buf.makeStringAndClear();
}

OUString AgentChatContextBuilder::extractUserQuery(const OUString& input,
                                                   const std::vector<MentionContext>& mentions)
{
    // Start with the full input
    OUString result = input;

    // Strip each mention in reverse order (to preserve positions during removal)
    for (auto it = mentions.rbegin(); it != mentions.rend(); ++it)
    {
        const sal_Int32 idx = result.indexOf(it->rawText);
        if (idx >= 0)
        {
            // Remove the mention token and any trailing whitespace
            sal_Int32 end = idx + it->rawText.getLength();
            // Skip trailing whitespace
            while (end < result.getLength() && result[end] == ' ')
                ++end;
            result = result.replaceAt(idx, end - idx, u""_ustr);
        }
    }

    // Trim leading/trailing whitespace
    result = result.trim();

    return result;
}

OUString AgentChatContextBuilder::buildSystemPrompt(const OUString& docType,
                                                    const std::vector<MentionContext>& mentions)
{
    return buildSystemPromptImpl(docType, mentions);
}

OUString AgentChatContextBuilder::buildDocumentSection(const ChatContext& ctx)
{
    return buildDocumentSectionImpl(ctx);
}

OUString ChatContext::debugString() const
{
    OUStringBuffer buf;
    buf.append(u"ChatContext{");
    buf.append(u"title=\"");
    buf.append(documentTitle);
    buf.append(u"\", type=\"");
    buf.append(documentType);
    buf.append(u"\", hasSelection=");
    buf.append(hasSelection() ? u"true"_ustr : u"false"_ustr);
    buf.append(u", messages=");
    buf.append(static_cast<sal_Int32>(recentMessages.size()));
    buf.append(u", maxTokens=");
    buf.append(maxTokens);
    buf.append(u"}");
    return buf.makeStringAndClear();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
