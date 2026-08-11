/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: busy-time message queue).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ComposerQueue.hxx"

#include <osl/time.h>

namespace kqoffice::ai::control
{

namespace
{
sal_Int64 nowMs()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
           + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}
} // namespace

ComposerQueue::ComposerQueue(sal_Int32 maxItems)
    : m_maxItems(maxItems > 0 ? maxItems : kDefaultMaxItems)
{
}

sal_Int32 ComposerQueue::size() const
{
    return static_cast<sal_Int32>(m_items.size());
}

bool ComposerQueue::empty() const
{
    return m_items.empty();
}

bool ComposerQueue::enqueue(const OUString& prompt, bool replaceCurrent, bool* overflow)
{
    if (overflow)
        *overflow = false;
    const OUString t = prompt.trim();
    if (t.isEmpty())
        return false;

    if (replaceCurrent)
        m_items.clear();

    if (static_cast<sal_Int32>(m_items.size()) >= m_maxItems)
    {
        if (!m_items.empty())
            m_items.erase(m_items.begin());
        if (overflow)
            *overflow = true;
    }

    ComposerQueuedItem item;
    item.prompt = t;
    item.enqueuedAtMs = nowMs();
    item.replaceCurrent = replaceCurrent;
    m_items.push_back(item);
    return true;
}

std::optional<ComposerQueuedItem> ComposerQueue::dequeue()
{
    if (m_items.empty())
        return std::nullopt;
    ComposerQueuedItem item = m_items.front();
    m_items.erase(m_items.begin());
    return item;
}

std::optional<ComposerQueuedItem> ComposerQueue::peek() const
{
    if (m_items.empty())
        return std::nullopt;
    return m_items.front();
}

void ComposerQueue::clear()
{
    m_items.clear();
}

OUString ComposerQueue::statusLineZh() const
{
    if (m_items.empty())
        return u"队列空"_ustr;
    return u"排队 "_ustr + OUString::number(size()) + u" 条 · 下一条已就绪"_ustr;
}

std::vector<OUString> ComposerQueue::prompts() const
{
    std::vector<OUString> out;
    out.reserve(m_items.size());
    for (const auto& it : m_items)
        out.push_back(it.prompt);
    return out;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
