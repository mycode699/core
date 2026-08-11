/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: busy-time message queue).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * FIFO follow-up prompts while a turn is streaming / tools open.
 * Default product behavior: queue, do not silently drop; optional replace-all.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_COMPOSERQUEUE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_COMPOSERQUEUE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <optional>
#include <vector>

namespace kqoffice::ai::control
{

struct ComposerQueuedItem
{
    OUString prompt;
    sal_Int64 enqueuedAtMs = 0;
    /// True when user asked to cancel current turn and run this next (stop-and-go).
    bool replaceCurrent = false;
};

/// Process-local FIFO for sidebar composer follow-ups (no disk).
class SAL_DLLPUBLIC_EXPORT ComposerQueue
{
public:
    static constexpr sal_Int32 kDefaultMaxItems = 8;

    explicit ComposerQueue(sal_Int32 maxItems = kDefaultMaxItems);

    sal_Int32 maxItems() const { return m_maxItems; }
    sal_Int32 size() const;
    bool empty() const;

    /// Enqueue trimmed prompt. Empty prompt → false.
    /// If full, drops oldest (fairness) and returns true with overflow=true out-param.
    bool enqueue(const OUString& prompt, bool replaceCurrent = false, bool* overflow = nullptr);

    /// Pop front; nullopt if empty.
    std::optional<ComposerQueuedItem> dequeue();

    /// Peek front without removing.
    std::optional<ComposerQueuedItem> peek() const;

    void clear();

    /// One-line status for chip / step bar.
    OUString statusLineZh() const;

    /// Snapshot prompts (for diagnostics; no secrets assumed in user text).
    std::vector<OUString> prompts() const;

private:
    sal_Int32 m_maxItems;
    std::vector<ComposerQueuedItem> m_items;
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
