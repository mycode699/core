/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: error decks).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Free-form host / provider / apply errors → product decks for UI + diagnostics.
 * Never stores raw API keys; classification is pure string heuristics.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_ERRORCLASSIFIER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_ERRORCLASSIFIER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::control
{

enum class ErrorDeck : sal_uInt8
{
    Auth = 0, ///< Login / membership / API key
    Network, ///< DNS / timeout / connect
    Provider, ///< LLM gateway / model / rate limit
    Apply, ///< Document write-back / stale / engine
    Permission, ///< Human approval / path / capability
    Resource, ///< Memory / stream budget / envelope
    Crash, ///< SIGABRT / lock / multi-instance
    Unknown,
};

struct ClassifiedError
{
    ErrorDeck deck = ErrorDeck::Unknown;
    /// Stable short code (ascii), e.g. auth-401, apply-stale.
    OUString code;
    OUString titleZh;
    OUString actionZh;
    bool retriable = false;
    /// Redacted one-line detail (no secrets).
    OUString detailZh;
};

class SAL_DLLPUBLIC_EXPORT ErrorClassifier
{
public:
    static ClassifiedError classify(const OUString& message);
    static ClassifiedError classify(const OUString& message, const OUString& errorCodeHint);

    static OUString deckLabelZh(ErrorDeck deck);
    static OUString deckId(ErrorDeck deck);

    /// Strip common secret shapes from free text for diagnostics.
    static OUString redact(const OUString& text);
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
