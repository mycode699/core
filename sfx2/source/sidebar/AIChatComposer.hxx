/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M1: AI-native workspace).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>
#include <tools/link.hxx>

#include <memory>
#include <vector>

class KeyEvent;

namespace weld
{
class Entry;
class Label;
class Menu;
}

namespace sfx2::sidebar
{

/** Individual parsed @mention extracted from the composer input. */
struct AIChatMention
{
    OUString type; // "doc", "selection", "connector"
    OUString id; // empty for doc/selection; connector slug for connector
};

/** Handles chat input composer with @mention support and auto-complete.

    The composer sits on top of a weld::Entry and:
    - Detects @mentions as the user types (@doc, @selection, @connector:ID)
    - Shows a completion popup when the caret is inside a mention token
    - Supports keyboard navigation (Enter to select, Escape to dismiss)
    - Exposes parsed mentions for the caller to consume
*/
class AIChatComposer
{
public:
    explicit AIChatComposer(weld::Entry* pInput);
    ~AIChatComposer();

    AIChatComposer(const AIChatComposer&) = delete;
    AIChatComposer& operator=(const AIChatComposer&) = delete;

    /** Parse all valid @mentions from the current input text. */
    std::vector<AIChatMention> ParseMentions() const;

    /** Parse all valid @mentions from an arbitrary string. */
    static std::vector<AIChatMention> ParseMentions(const OUString& rText);

    /** Check whether the given text contains any @mention. */
    static bool HasMention(const OUString& rText);

    /** Strip all @mentions from the text, returning the clean text. */
    static OUString StripMentions(const OUString& rText);

    /** Returns true when the caret is inside a mention token. */
    bool IsInsideMention() const;

    /** Get the current mention token under the caret, empty if none. */
    OUString GetCurrentMentionToken() const;

    /** Show the auto-complete popup for the current mention prefix. */
    void ShowMentionCompletion();

    /** Dismiss the auto-complete popup. */
    void DismissMentionCompletion();

    /** Whether the completion popup is currently visible. */
    bool IsCompletionVisible() const { return m_bCompletionVisible; }

    /** Select the highlighted completion and insert it. */
    void SelectCompletion();

private:
    DECL_LINK(OnInputChanged, weld::Entry&, void);
    DECL_LINK(OnKeyPress, const KeyEvent&, bool);

    /** Find the mention token boundaries around the caret position. */
    void FindMentionBounds(sal_Int32& rStart, sal_Int32& rEnd) const;

    /** Build the list of completion suggestions for the given prefix. */
    std::vector<OUString> BuildSuggestions(const OUString& rPrefix) const;

    weld::Entry* m_pInput;
    bool m_bCompletionVisible = false;
    std::vector<OUString> m_aCurrentSuggestions;
    sal_Int32 m_nHighlightedSuggestion = 0;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */