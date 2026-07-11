/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include <DocumentAIContext.hxx>
#include <AgentChatMentionResolver.hxx>

#include <rtl/ustrbuf.hxx>

#include <algorithm>

namespace kqoffice::ai::chat
{
namespace
{
OUString clipText(const OUString& s, sal_Int32 nMax)
{
    if (nMax <= 0 || s.getLength() <= nMax)
        return s;
    return s.copy(0, nMax) + u"…"_ustr;
}

OUString surfaceLabelZh(const OUString& surface)
{
    if (surface == u"writer"_ustr)
        return u"文字"_ustr;
    if (surface == u"calc"_ustr)
        return u"表格"_ustr;
    if (surface == u"impress"_ustr)
        return u"演示"_ustr;
    if (surface == u"none"_ustr || surface.isEmpty())
        return u"无文档"_ustr;
    return surface;
}
} // namespace

OUString DocumentAIContext::compactContextField(const SelectionContext& rSelection,
                                                sal_Int32 nMaxChars)
{
    OUStringBuffer b;
    b.append(u"surface="_ustr);
    b.append(rSelection.surface.isEmpty() ? u"unknown"_ustr : rSelection.surface);
    if (!rSelection.position.isEmpty())
    {
        b.append(u" position="_ustr);
        b.append(rSelection.position);
    }
    b.append(u" length="_ustr);
    b.append(rSelection.length);
    if (!rSelection.text.isEmpty())
    {
        b.append(u"\n--- selection ---\n"_ustr);
        b.append(clipText(rSelection.text, nMaxChars));
    }
    return b.makeStringAndClear();
}

DocumentAIBinding DocumentAIContext::bindUserInput(const OUString& rUserInput)
{
    DocumentAIBinding out;
    out.selection = AgentChatSelectionCapture::captureCurrent();
    out.hasDocument = out.selection.surface != u"none"_ustr
                      && out.selection.surface != u"unknown"_ustr
                      && !out.selection.surface.isEmpty();
    out.hasSelection = !out.selection.text.isEmpty();

    // Parse @mentions; selection is always auto-attached via SelectionCapture.
    const std::vector<MentionContext> mentions = AgentChatMentionResolver::parse(rUserInput);

    out.chat = AgentChatContextBuilder::build(rUserInput, mentions, out.selection);
    out.chat.selectionPosition = out.selection.position;
    out.chat.userQuery = AgentChatContextBuilder::extractUserQuery(rUserInput, mentions);
    if (out.chat.userQuery.isEmpty())
        out.chat.userQuery = rUserInput.trim();

    out.enrichedPrompt = AgentChatContextBuilder::toPromptString(out.chat);
    out.providerContext = compactContextField(out.selection);

    OUStringBuffer status;
    status.append(surfaceLabelZh(out.selection.surface));
    if (out.hasSelection)
    {
        status.append(u" · 已附带选区 "_ustr);
        status.append(out.selection.length > 0 ? out.selection.length
                                               : out.selection.text.getLength());
        status.append(u" 字"_ustr);
        if (!out.selection.position.isEmpty())
        {
            status.append(u" ("_ustr);
            status.append(out.selection.position);
            status.append(u")"_ustr);
        }
    }
    else if (out.hasDocument)
    {
        status.append(u" · 无选区（将按当前文档类型回答）"_ustr);
    }
    else
    {
        status.append(u" · 未打开文档"_ustr);
    }
    out.statusLabel = status.makeStringAndClear();
    return out;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
