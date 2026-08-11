/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: ledger-based handoff).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "HandoffBrief.hxx"

#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::control
{

HandoffBrief HandoffBriefBuilder::fromEvents(const std::vector<LedgerEvent>& events,
                                             const OUString& titleZh)
{
    HandoffBrief b;
    b.eventCount = static_cast<sal_Int32>(events.size());
    OUStringBuffer md;
    md.append(u"## "_ustr);
    md.append(titleZh.isEmpty() ? u"会话交接简报（账本）"_ustr : titleZh);
    md.append(u"\n\n"_ustr);
    md.append(u"_来源：EventLedger 事实，非模型自述。_\n\n"_ustr);
    if (events.empty())
    {
        md.append(u"暂无账本事件。\n"_ustr);
        b.markdownZh = md.makeStringAndClear();
        return b;
    }
    b.lastSequence = events.back().sequence;
    md.append(u"事件数："_ustr);
    md.append(OUString::number(b.eventCount));
    md.append(u" · 最后序号："_ustr);
    md.append(OUString::number(b.lastSequence));
    md.append(u"\n\n"_ustr);
    // Newest last in vector — show last 15 chronological
    const size_t start = events.size() > 15 ? events.size() - 15 : 0;
    for (size_t i = start; i < events.size(); ++i)
    {
        const auto& e = events[i];
        md.append(u"- `#"_ustr);
        md.append(OUString::number(e.sequence));
        md.append(u"` **"_ustr);
        md.append(e.type);
        md.append(u"** "_ustr);
        md.append(e.payload);
        md.append(u"\n"_ustr);
    }
    md.append(u"\n### 硬约束提醒\n"_ustr);
    md.append(u"- 主文档写回须人工批准（Ask）\n"_ustr);
    md.append(u"- 不自动 git push / publish / deploy\n"_ustr);
    md.append(u"- 继续任务请基于上述事实，勿编造已完成步骤\n"_ustr);
    b.markdownZh = md.makeStringAndClear();
    return b;
}

HandoffBrief HandoffBriefBuilder::fromLedgerTail(sal_Int32 maxEvents)
{
    EventLedger ledger;
    auto evs = ledger.replaySince(0, maxEvents > 0 ? maxEvents : 30);
    return fromEvents(evs);
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
