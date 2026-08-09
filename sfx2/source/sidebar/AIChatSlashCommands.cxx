/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈 office project (V4 M1: AI-native workspace).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatSlashCommands.hxx"

#include <DocumentAIScenarioStore.hxx>

#include <algorithm>

namespace sfx2::sidebar
{
namespace
{

std::vector<SlashCommand> BuildMergedSlashCommands()
{
    std::vector<SlashCommand> aCommands = {
        // Editing commands
        { u"/rewrite"_ustr, u"改写"_ustr, u"Rewrite the selected text"_ustr, true },
        { u"/expand"_ustr, u"扩写"_ustr, u"Expand the selected text"_ustr, true },
        { u"/shorten"_ustr, u"简写"_ustr, u"Shorten the selected text"_ustr, true },
        { u"/translate"_ustr, u"翻译"_ustr, u"Translate to specified language"_ustr, true },
        { u"/summarize"_ustr, u"总结"_ustr, u"Summarize the content"_ustr, false },
        { u"/outline"_ustr, u"大纲"_ustr, u"Document structure outline"_ustr, false },
        { u"/proofread"_ustr, u"审阅"_ustr, u"Full-document review checklist"_ustr, false },
        { u"/continue"_ustr, u"续写"_ustr, u"Continue writing after selection or end"_ustr, false },
        { u"/doc-summary"_ustr, u"全文总结"_ustr, u"Full-document summary and action items"_ustr, false },
        { u"/chart"_ustr, u"图表"_ustr, u"Generate a chart from data"_ustr, true },
        { u"/formula"_ustr, u"公式"_ustr, u"Generate a spreadsheet formula (=…)"_ustr, true },
        { u"/clean"_ustr, u"清洗"_ustr, u"Data clean checklist for selection"_ustr, false },
        { u"/interpret"_ustr, u"解读"_ustr, u"Interpret selected table data"_ustr, false },
        { u"/aggregate"_ustr, u"汇总"_ustr, u"Sum/avg/count formulas for selection"_ustr, true },
        { u"/format"_ustr, u"格式化"_ustr, u"Clean up formatting"_ustr, true },
        { u"/explain"_ustr, u"解释"_ustr, u"Explain the content"_ustr, false },
        { u"/review"_ustr, u"审核"_ustr, u"Review and suggest improvements"_ustr, false },
        { u"/layout"_ustr, u"排版"_ustr, u"Optimize the layout"_ustr, true },
        { u"/style"_ustr, u"样式"_ustr, u"Apply a style template"_ustr, true },
        // Pending-plan Diff (no mutation)
        { u"/diff"_ustr, u"查看差异"_ustr,
          u"Open Diff review for the pending apply plan (approve still required)"_ustr, false },
        // Undo last AI write-back (SID_UNDO; same as sidebar button)
        { u"/undo-apply"_ustr, u"撤销写回"_ustr,
          u"Undo the last AI write-back via document undo stack"_ustr, false },
        { u"/撤销写回"_ustr, u"撤销写回"_ustr,
          u"撤销最近一次 AI 写回（与侧栏按钮相同）"_ustr, false },

        // V6 File manager commands
        { u"/files"_ustr, u"文件"_ustr, u"Scan and list files in common directories"_ustr, false },
        { u"/find"_ustr, u"查找"_ustr, u"AI semantic search for files"_ustr, false },
        { u"/nav"_ustr, u"导航"_ustr, u"Generate quick-access navigation index file"_ustr, false },

        // V5 Canvas mode commands
        { u"/canvas"_ustr, u"画布"_ustr, u"Start AI canvas mode for guided document creation"_ustr, false },
        { u"/confirm"_ustr, u"确认"_ustr, u"Confirm current canvas step and proceed"_ustr, false },
        { u"/revise"_ustr, u"修改"_ustr, u"Request revision of current canvas step"_ustr, false },
        { u"/skip"_ustr, u"跳过"_ustr, u"Skip current canvas step"_ustr, false },

        // M11/M12 local knowledge FTS admin (no network)
        { u"/fts-status"_ustr, u"索引状态"_ustr, u"Show local FTS workspace index status"_ustr, false },
        { u"/fts-workspaces"_ustr, u"工作区列表"_ustr, u"List local FTS workspaces"_ustr, false },
        { u"/fts-reindex"_ustr, u"重建索引"_ustr, u"Force reindex open document into local FTS"_ustr, false },
        { u"/fts-index-materials"_ustr, u"索引材料"_ustr,
          u"Index @文件/@文件夹 materials from the prompt into local FTS"_ustr, false },
        { u"/fts-purge"_ustr, u"清理索引"_ustr,
          u"Delete local FTS workspace database (optional hash prefix)"_ustr, false },
        { u"/fts-watch"_ustr, u"监视材料"_ustr,
          u"Register material paths for bounded poll watch (max 256, no per-file FD)"_ustr, false },
        { u"/fts-watch-status"_ustr, u"监视状态"_ustr, u"Show bounded material path watch status"_ustr, false },
        { u"/fts-watch-poll"_ustr, u"轮询监视"_ustr,
          u"Poll watched material paths and reindex on mtime change"_ustr, false },
        { u"/fts-watch-clear"_ustr, u"清空监视"_ustr, u"Clear in-process material path watch list"_ustr, false },
        { u"/fts-search"_ustr, u"检索索引"_ustr,
          u"Search local FTS index offline (no Provider, no egress)"_ustr, false },
        // M13 propose-only write tool (ApplyPlan buffer; never mutates main doc)
        { u"/propose-replace"_ustr, u"提议替换"_ustr,
          u"Propose selection/block replace into ApplyPlan buffer (approval required)"_ustr, true },

        // Enterprise connectors (default OFF; status/grant only — no silent egress)
        { u"/connectors"_ustr, u"连接器"_ustr,
          u"Show enterprise connector status (default off, no silent network)"_ustr, false },
        { u"/connector-grant"_ustr, u"连接器授权"_ustr,
          u"Grant a connector id locally (still needs master switch + op approval)"_ustr, false },
        { u"/connector-revoke"_ustr, u"连接器撤销"_ustr,
          u"Revoke local grant for a connector id"_ustr, false },
        { u"/connector-auth"_ustr, u"连接器登录"_ustr,
          u"Start private OAuth device-code for a connector (requires approval)"_ustr, false },
        { u"/connector-auth-poll"_ustr, u"连接器登录轮询"_ustr,
          u"Poll device-code token once; stores secret locally on success"_ustr, false },
        { u"/vision-status"_ustr, u"视觉路由"_ustr,
          u"Show local vision model resolution (no upload)"_ustr, false },
    };

    // Merge configurable scenario slash commands (公文润色 / 公式助手 / …).
    try
    {
        const auto cat = kqoffice::ai::chat::DocumentAIScenarioStore::load();
        for (const auto& s : cat.items)
        {
            if (!s.enabled || s.slashCommand.isEmpty())
                continue;
            bool exists = false;
            for (const auto& c : aCommands)
            {
                if (c.command == s.slashCommand)
                {
                    exists = true;
                    break;
                }
            }
            if (exists)
                continue;
            SlashCommand sc;
            sc.command = s.slashCommand;
            sc.label = s.titleZh;
            sc.description = u"方案 "_ustr + s.id + u" · "_ustr + s.capabilityHint;
            sc.needsSelection = s.options.attachSelection;
            aCommands.push_back(sc);
        }
    }
    catch (...)
    {
        // Catalog load must never break slash registry.
    }
    return aCommands;
}

const std::vector<SlashCommand>& GetSlashCommands()
{
    // Rebuild each call so option-page CRUD edits apply without restart.
    // Cheap vs model inference; not on a hot rendering path.
    static std::vector<SlashCommand> aCache;
    aCache = BuildMergedSlashCommands();
    return aCache;
}

} // anonymous namespace

const std::vector<SlashCommand>& AIChatSlashCommands::All()
{
    return GetSlashCommands();
}

const SlashCommand* AIChatSlashCommands::Match(const OUString& rInput)
{
    if (rInput.isEmpty() || rInput[0] != u'/')
        return nullptr;

    const std::vector<SlashCommand>& aCommands = GetSlashCommands();

    for (const auto& rCmd : aCommands)
    {
        if (rInput.startsWith(rCmd.command))
            return &rCmd;
    }

    return nullptr;
}

bool AIChatSlashCommands::IsSlashCommand(const OUString& rInput)
{
    return Match(rInput) != nullptr;
}

OUString AIChatSlashCommands::ExtractCommandName(const OUString& rInput)
{
    const SlashCommand* pCmd = Match(rInput);
    return pCmd ? pCmd->command : OUString();
}

OUString AIChatSlashCommands::ExtractCommandArg(const OUString& rInput)
{
    const OUString sCmdName = ExtractCommandName(rInput);
    if (sCmdName.isEmpty() || rInput.getLength() <= sCmdName.getLength())
        return OUString();

    OUString sArg = rInput.copy(sCmdName.getLength());
    return sArg.trim();
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */