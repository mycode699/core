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

#include <vector>

namespace sfx2::sidebar
{

/** A single slash command available in the composer. */
struct SlashCommand
{
    OUString command; // e.g. "/rewrite"
    OUString label; // e.g. "改写"
    OUString description; // e.g. "Rewrite the selected text"
    bool needsSelection; // true when the command requires document selection
};

/** Registry of all slash commands available in the chat composer.

    Commands are triggered by typing "/" in the input entry. The
    composer auto-completes the command name and the panel dispatches
    the matched command to the AI runtime with the appropriate system
    prompt prefix.
*/
class AIChatSlashCommands
{
public:
    /** Return the static list of all available slash commands. */
    static const std::vector<SlashCommand>& All();

    /** Return the matching command for the given input, or nullptr. */
    static const SlashCommand* Match(const OUString& rInput);

    /** Check whether the input starts with a slash command. */
    static bool IsSlashCommand(const OUString& rInput);

    /** Extract the command name from the input (e.g. "/rewrite" from "/rewrite some text"). */
    static OUString ExtractCommandName(const OUString& rInput);

    /** Extract the argument text after the command name. */
    static OUString ExtractCommandArg(const OUString& rInput);
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */