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
        { u"/chart"_ustr, u"图表"_ustr, u"Generate a chart from data"_ustr, true },
        { u"/formula"_ustr, u"公式"_ustr, u"Generate a formula"_ustr, true },
        { u"/format"_ustr, u"格式化"_ustr, u"Clean up formatting"_ustr, true },
        { u"/explain"_ustr, u"解释"_ustr, u"Explain the content"_ustr, true },
        { u"/review"_ustr, u"审核"_ustr, u"Review and suggest improvements"_ustr, true },
        { u"/layout"_ustr, u"排版"_ustr, u"Optimize the layout"_ustr, true },
        { u"/style"_ustr, u"样式"_ustr, u"Apply a style template"_ustr, true },

        // V6 File manager commands
        { u"/files"_ustr, u"文件"_ustr, u"Scan and list files in common directories"_ustr, false },
        { u"/find"_ustr, u"查找"_ustr, u"AI semantic search for files"_ustr, false },
        { u"/nav"_ustr, u"导航"_ustr, u"Generate quick-access navigation index file"_ustr, false },

        // V5 Canvas mode commands
        { u"/canvas"_ustr, u"画布"_ustr, u"Start AI canvas mode for guided document creation"_ustr, false },
        { u"/confirm"_ustr, u"确认"_ustr, u"Confirm current canvas step and proceed"_ustr, false },
        { u"/revise"_ustr, u"修改"_ustr, u"Request revision of current canvas step"_ustr, false },
        { u"/skip"_ustr, u"跳过"_ustr, u"Skip current canvas step"_ustr, false },
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